#pragma once

#include "s3g_smoothing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#if !defined(S3G_CRCLTR_EXTERNAL_MEMORY_ONLY)
#include <vector>
#endif

namespace s3g {

constexpr float kCrcltrMaximumRecordSeconds = 32.0f;
constexpr float kCrcltrMinimumRecordSeconds = 0.1f;

enum class CrcltrCrossfadeMode : uint32_t {
    Manual = 0u,
    Sine = 1u,
    Trapezoid = 2u,
    RandomWalk = 3u,
    Triangle = 4u,
    RampAToB = 5u,
    RampBToA = 6u,
    SampleHold = 7u,
    Square = 8u,
};

enum class CrcltrCrossfadeShape : uint32_t {
    EqualPower = 0u,
    Linear = 1u,
    Wide = 2u,
    Tight = 3u,
    Smooth = 4u,
    FullOverlap = 5u,
    DeepDip = 6u,
    Plateau = 7u,
    Cut = 8u,
};

struct CrcltrCrossfadeGains {
    float a = 1.0f;
    float b = 0.0f;
};

inline CrcltrCrossfadeGains crcltrCrossfadeGains(
    CrcltrCrossfadeShape shape, float amount)
{
    constexpr float kHalfPi = 1.57079632679489661923f;
    amount = std::max(0.0f, std::min(1.0f,
        std::isfinite(amount) ? amount : 0.0f));
    if (shape == CrcltrCrossfadeShape::Linear)
        return { 1.0f - amount, amount };
    if (shape == CrcltrCrossfadeShape::Smooth) {
        const float smooth = amount * amount * (3.0f - 2.0f * amount);
        return { 1.0f - smooth, smooth };
    }
    if (shape == CrcltrCrossfadeShape::FullOverlap)
        return { std::min(1.0f, 2.0f * (1.0f - amount)),
            std::min(1.0f, 2.0f * amount) };
    const float a = std::max(0.0f, std::cos(amount * kHalfPi));
    const float b = std::max(0.0f, std::sin(amount * kHalfPi));
    if (shape == CrcltrCrossfadeShape::Wide)
        return { std::pow(a, 0.65f), std::pow(b, 0.65f) };
    if (shape == CrcltrCrossfadeShape::Tight)
        return { std::pow(a, 1.5f), std::pow(b, 1.5f) };
    if (shape == CrcltrCrossfadeShape::DeepDip)
        return { a * a * a, b * b * b };
    if (shape == CrcltrCrossfadeShape::Plateau) {
        constexpr float kThird = 1.0f / 3.0f;
        constexpr float kRootHalf = 0.7071067811865475f;
        if (amount < kThird) {
            const float local = amount * 1.5f;
            return { std::max(0.0f, std::cos(local * kHalfPi)),
                std::max(0.0f, std::sin(local * kHalfPi)) };
        }
        if (amount <= 2.0f * kThird) return { kRootHalf, kRootHalf };
        const float local = 0.5f + (amount - 2.0f * kThird) * 1.5f;
        return { std::max(0.0f, std::cos(local * kHalfPi)),
            std::max(0.0f, std::sin(local * kHalfPi)) };
    }
    if (shape == CrcltrCrossfadeShape::Cut) {
        const float local = std::clamp((amount - 0.485f) / 0.03f,
            0.0f, 1.0f);
        return { std::max(0.0f, std::cos(local * kHalfPi)),
            std::max(0.0f, std::sin(local * kHalfPi)) };
    }
    return { a, b };
}

enum class CrcltrPlaybackModel : uint32_t {
    Classic = 0u,
    Dual = 1u,
};

enum class CrcltrRecordMode : uint32_t {
    Replace = 0u,
    Overdub = 1u,
    Punch = 2u,
};

enum class CrcltrLoopJoin : uint32_t {
    Seam = 0u,
    Duck = 1u,
};

enum class CrcltrRecordTarget : uint32_t {
    Loop1 = 0u,
    Both = 1u,
    Loop2 = 2u,
};

enum class CrcltrMonitorMode : uint32_t {
    Loop1 = 0u,
    Mute = 1u,
    Loop2 = 2u,
};

struct CrcltrParams {
    float loop1Rate = 1.0f;
    float loop2Rate = 1.0f;
    CrcltrCrossfadeMode crossfadeMode = CrcltrCrossfadeMode::Manual;
    CrcltrCrossfadeShape crossfadeShape = CrcltrCrossfadeShape::EqualPower;
    float crossfade = 0.5f;
    float blend = 0.5f;
    float inputGain = 1.0f;
    float outputGain = 1.0f;
    bool record = false;
    CrcltrRecordTarget recordTarget = CrcltrRecordTarget::Both;
    CrcltrMonitorMode monitorMode = CrcltrMonitorMode::Mute;
    CrcltrPlaybackModel playbackModel = CrcltrPlaybackModel::Classic;
    CrcltrRecordMode recordMode = CrcltrRecordMode::Replace;
    float overdubFeedback = 0.8f;
    bool loop1Reverse = false;
    bool loop2Reverse = false;
    float loop1Start = 0.0f;
    float loop1End = 1.0f;
    float loop2Start = 0.0f;
    float loop2End = 1.0f;
    CrcltrLoopJoin loop1Join = CrcltrLoopJoin::Seam;
    CrcltrLoopJoin loop2Join = CrcltrLoopJoin::Seam;
    bool playing = true;
};

// Daisy targets can supply buffers in external SDRAM. Desktop clients may omit
// this structure and let Crcltr own the same fixed-capacity storage.
struct CrcltrMemory {
    float* loop1Left = nullptr;
    float* loop1Right = nullptr;
    float* loop2Left = nullptr;
    float* loop2Right = nullptr;
    uint32_t loopCapacityFrames = 0u;

    float* preRoll1Left = nullptr;
    float* preRoll1Right = nullptr;
    float* preRoll2Left = nullptr;
    float* preRoll2Right = nullptr;
    uint32_t preRollCapacityFrames = 0u;
};

class Crcltr {
public:
    static uint32_t requiredLoopCapacity(double sampleRate)
    {
        return sampleRate > 1.0
            ? static_cast<uint32_t>(std::ceil(sampleRate
                * static_cast<double>(kCrcltrMaximumRecordSeconds)))
            : 0u;
    }

    static uint32_t requiredPreRollCapacity(double sampleRate)
    {
        return sampleRate > 1.0
            ? std::max<uint32_t>(1u, static_cast<uint32_t>(std::ceil(sampleRate
                * static_cast<double>(kCrcltrMinimumRecordSeconds))))
            : 0u;
    }

    bool prepare(double sampleRate, uint32_t maxBlockFrames,
                 const CrcltrMemory* externalMemory = nullptr)
    {
        (void)maxBlockFrames;
        if (!std::isfinite(sampleRate) || sampleRate <= 1.0) return false;

        sampleRate_ = sampleRate;
        loopCapacityFrames_ = requiredLoopCapacity(sampleRate_);
        preRollCapacityFrames_ = requiredPreRollCapacity(sampleRate_);
        if (loopCapacityFrames_ < 2u || preRollCapacityFrames_ == 0u) return false;

        if (externalMemory) {
            if (!validExternalMemory(*externalMemory)) return false;
            assignExternalMemory(*externalMemory);
            usingExternalMemory_ = true;
        } else {
#if defined(S3G_CRCLTR_EXTERNAL_MEMORY_ONLY)
            return false;
#else
            if (!allocateOwnedMemory()) return false;
            assignOwnedMemory();
            usingExternalMemory_ = false;
#endif
        }

        loops_[0].recordedFrames = 0u;
        loops_[1].recordedFrames = 0u;
        prepared_ = true;
        reset();
        return true;
    }

    void reset()
    {
        gateWasHigh_ = false;
        latchedTarget_ = CrcltrRecordTarget::Both;
        latchedRecordMode_ = CrcltrRecordMode::Replace;
        xfadeLfoPhase_ = 0.0;
        randomWalkValue_ = 0.5f;
        randomWalkTarget_ = 0.5f;
        randomWalkCounter_ = 0u;
        randomState_ = 0x6d2b79f5u;
        sampleHoldValue_ = 0.5f;
        sampleHoldInitialized_ = false;
        currentCrossfade_ = params_.crossfade;
        previousCrossfade_ = currentCrossfade_;
        crossfadeDirection_ = 0.0f;
        inputDc_[0].reset();
        inputDc_[1].reset();
        for (uint32_t index = 0u; index < loops_.size(); ++index) {
            auto& loop = loops_[index];
            loop.phase = 0.0;
            loop.writePosition = 0u;
            loop.preRollFrames = 0u;
            loop.armed = false;
            loop.capturing = false;
            loop.windowTransitionRemaining = 0u;
            loop.windowTransitionTotal = 0u;
            applyRequestedLoopWindow(index);
        }

        loop1Rate_.reset(static_cast<float>(sampleRate_), 10.0f, params_.loop1Rate);
        loop2Rate_.reset(static_cast<float>(sampleRate_), 10.0f, params_.loop2Rate);
        crossfade_.reset(static_cast<float>(sampleRate_), 5.0f, params_.crossfade);
        blend_.reset(static_cast<float>(sampleRate_), 10.0f, params_.blend);
        inputGain_.reset(static_cast<float>(sampleRate_), 10.0f, params_.inputGain);
        outputGain_.reset(static_cast<float>(sampleRate_), 10.0f, params_.outputGain);
        recordMix_[0].reset(static_cast<float>(sampleRate_), 10.0f, 0.0f);
        recordMix_[1].reset(static_cast<float>(sampleRate_), 10.0f, 0.0f);
    }

    void setParams(const CrcltrParams& params)
    {
        params_ = params;
        params_.loop1Rate = clamp(params_.loop1Rate, 0.25f, 2.75f);
        params_.loop2Rate = clamp(params_.loop2Rate, 0.25f, 2.75f);
        params_.crossfadeMode = static_cast<CrcltrCrossfadeMode>(
            std::min<uint32_t>(static_cast<uint32_t>(params_.crossfadeMode), 8u));
        params_.crossfadeShape = static_cast<CrcltrCrossfadeShape>(
            std::min<uint32_t>(static_cast<uint32_t>(params_.crossfadeShape), 8u));
        params_.crossfade = clamp(params_.crossfade, 0.0f, 1.0f);
        params_.blend = clamp(params_.blend, 0.0f, 1.0f);
        params_.inputGain = clamp(params_.inputGain, 0.0f, 1.0f);
        params_.outputGain = clamp(params_.outputGain, 0.0f, 1.0f);
        params_.recordTarget = static_cast<CrcltrRecordTarget>(
            std::min<uint32_t>(static_cast<uint32_t>(params_.recordTarget), 2u));
        params_.monitorMode = static_cast<CrcltrMonitorMode>(
            std::min<uint32_t>(static_cast<uint32_t>(params_.monitorMode), 2u));
        params_.playbackModel = static_cast<CrcltrPlaybackModel>(
            std::min<uint32_t>(static_cast<uint32_t>(params_.playbackModel), 1u));
        params_.recordMode = static_cast<CrcltrRecordMode>(
            std::min<uint32_t>(static_cast<uint32_t>(params_.recordMode), 2u));
        params_.overdubFeedback = clamp(params_.overdubFeedback, 0.0f, 1.0f);
        params_.loop1Start = clamp(params_.loop1Start, 0.0f, 0.999f);
        params_.loop1End = clamp(params_.loop1End,
            params_.loop1Start + 0.001f, 1.0f);
        params_.loop2Start = clamp(params_.loop2Start, 0.0f, 0.999f);
        params_.loop2End = clamp(params_.loop2End,
            params_.loop2Start + 0.001f, 1.0f);
        params_.loop1Join = static_cast<CrcltrLoopJoin>(
            std::min<uint32_t>(static_cast<uint32_t>(params_.loop1Join), 1u));
        params_.loop2Join = static_cast<CrcltrLoopJoin>(
            std::min<uint32_t>(static_cast<uint32_t>(params_.loop2Join), 1u));

        for (uint32_t index = 0u; index < loops_.size(); ++index)
            requestLoopWindow(index);

        loop1Rate_.setTarget(params_.loop1Rate);
        loop2Rate_.setTarget(params_.loop2Rate);
        blend_.setTarget(params_.blend);
        inputGain_.setTarget(params_.inputGain);
        outputGain_.setTarget(params_.outputGain);
    }

    const CrcltrParams& params() const { return params_; }

    // A host can call this before audio resumes so edits made while processing
    // was stopped do not have to wait for an inaudible old-window wrap.
    void applyPendingLoopWindows()
    {
        for (uint32_t index = 0u; index < loops_.size(); ++index)
            if (loops_[index].windowPending)
                commitPendingLoopWindow(index, false, {});
    }

    void process(const float* inputLeft, const float* inputRight,
                 float* outputLeft, float* outputRight, uint32_t frames)
    {
        if (!prepared_ || !outputLeft || !outputRight || frames == 0u) return;

        for (uint32_t i = 0u; i < frames; ++i) {
            const float dryLeft = inputLeft ? finiteOrZero(inputLeft[i]) : 0.0f;
            const float dryRight = inputRight ? finiteOrZero(inputRight[i]) : dryLeft;
            const float inputGain = inputGain_.next();
            const float recordLeft = inputDc_[0].process(softGain(dryLeft, inputGain));
            const float recordRight = inputDc_[1].process(softGain(dryRight, inputGain));

            updateRecordGate(params_.record, recordLeft, recordRight);

            const float rate1 = loop1Rate_.next();
            const float rate2 = loop2Rate_.next();
            const float signedRate1 = params_.loop1Reverse ? -rate1 : rate1;
            const float signedRate2 = params_.loop2Reverse ? -rate2 : rate2;
            const StereoFrame loop1 = params_.playing
                ? readLoop(0u, signedRate1) : StereoFrame {};
            const StereoFrame loop2 = params_.playing
                ? readLoop(1u, signedRate2) : StereoFrame {};

            const bool replacing = latchedRecordMode_
                == CrcltrRecordMode::Replace;
            recordMix_[0].setTarget(
                loops_[0].capturing && replacing ? 1.0f : 0.0f);
            recordMix_[1].setTarget(
                loops_[1].capturing && replacing ? 1.0f : 0.0f);
            const float recordMix1 = recordMix_[0].next();
            const float recordMix2 = recordMix_[1].next();
            const StereoFrame monitor1 = monitorFrame(0u, dryLeft, dryRight);
            const StereoFrame monitor2 = monitorFrame(1u, dryLeft, dryRight);
            const StereoFrame slot1 {
                lerp(loop1.left, monitor1.left, recordMix1),
                lerp(loop1.right, monitor1.right, recordMix1),
            };
            const StereoFrame slot2 {
                lerp(loop2.left, monitor2.left, recordMix2),
                lerp(loop2.right, monitor2.right, recordMix2),
            };

            const float xfade = nextCrossfade(rate1, rate2);
            const StereoFrame wet = crossfadeLoops(slot1, slot2, xfade);
            const float blend = blend_.next();
            const float dryWeight = std::cos(blend * kHalfPi);
            const float wetWeight = std::sin(blend * kHalfPi);
            const float outputGain = outputGain_.next();
            outputLeft[i] = flushDenormal(softGain(
                dryLeft * dryWeight + wet.left * wetWeight, outputGain));
            outputRight[i] = flushDenormal(softGain(
                dryRight * dryWeight + wet.right * wetWeight, outputGain));
        }
    }

    void clearLoop(uint32_t index)
    {
        if (index >= loops_.size()) return;
        auto& loop = loops_[index];
        loop.recordedFrames = 0u;
        loop.phase = 0.0;
        loop.writePosition = 0u;
        loop.preRollFrames = 0u;
        loop.armed = false;
        loop.capturing = false;
        loop.windowTransitionRemaining = 0u;
        loop.windowTransitionTotal = 0u;
        applyRequestedLoopWindow(index);
    }

    void clear()
    {
        clearLoop(0u);
        clearLoop(1u);
    }

    uint32_t recordedFrames(uint32_t index) const
    {
        return index < loops_.size() ? loops_[index].recordedFrames : 0u;
    }

    uint32_t playbackFrames(uint32_t index) const
    {
        return loopWindow(index).frames;
    }

    bool loopWindowPending(uint32_t index) const
    {
        return index < loops_.size() && loops_[index].windowPending;
    }

    float activeLoopStart(uint32_t index) const
    {
        return index < loops_.size() ? loops_[index].activeStart : 0.0f;
    }

    float activeLoopEnd(uint32_t index) const
    {
        return index < loops_.size() ? loops_[index].activeEnd : 1.0f;
    }

    bool isRecording(uint32_t index) const
    {
        return index < loops_.size() && loops_[index].capturing;
    }

    bool usingExternalMemory() const { return usingExternalMemory_; }
    uint32_t loopCapacityFrames() const { return loopCapacityFrames_; }
    double sampleRate() const { return sampleRate_; }

    // Realtime observers may read these immediately after process() on the
    // same thread to publish bounded GUI summaries. They deliberately avoid
    // exposing the loop storage to the editor thread.
    float loopSample(uint32_t index, uint32_t channel, uint32_t frame) const
    {
        if (index >= loops_.size() || channel > 1u
            || frame >= loops_[index].recordedFrames) return 0.0f;
        const float* samples = channel == 0u
            ? loops_[index].left : loops_[index].right;
        return samples ? finiteOrZero(samples[frame]) : 0.0f;
    }

    float playbackPosition(uint32_t index) const
    {
        if (index >= loops_.size()) return 0.0f;
        const auto window = loopWindow(index);
        const uint32_t recordedFrames = loops_[index].recordedFrames;
        if (window.frames < 2u || recordedFrames < 2u) return 0.0f;

        const bool seam = (index == 0u
                ? params_.loop1Join : params_.loop2Join)
            == CrcltrLoopJoin::Seam;
        double offset = 0.0;
        if (seam) {
            const uint32_t fadeFrames = std::min<uint32_t>(window.frames / 4u,
                std::max<uint32_t>(1u, static_cast<uint32_t>(
                    std::round(sampleRate_ * 0.010))));
            const uint32_t periodFrames = window.frames - fadeFrames;
            offset = wrapPhase(loops_[index].phase,
                static_cast<double>(periodFrames)) + fadeFrames;
        } else {
            offset = wrapPhase(loops_[index].phase,
                static_cast<double>(window.frames));
        }
        const double sourcePosition = static_cast<double>(window.start)
            + std::min(offset, static_cast<double>(window.frames - 1u));
        return clamp(static_cast<float>(sourcePosition
            / static_cast<double>(recordedFrames)), 0.0f, 1.0f);
    }

    float currentCrossfade() const { return currentCrossfade_; }
    float crossfadeDirection() const { return crossfadeDirection_; }

    bool copyLoop(uint32_t index, float* left, float* right,
                  uint32_t capacityFrames) const
    {
        if (index >= loops_.size() || !left || !right) return false;
        const auto& loop = loops_[index];
        if (capacityFrames < loop.recordedFrames) return false;
        std::copy_n(loop.left, loop.recordedFrames, left);
        std::copy_n(loop.right, loop.recordedFrames, right);
        return true;
    }

    bool restoreLoop(uint32_t index, const float* left, const float* right,
                     uint32_t frames, double sourceSampleRate)
    {
        if (!prepared_ || index >= loops_.size() || !left || !right
            || frames < 2u || !std::isfinite(sourceSampleRate)
            || sourceSampleRate <= 1.0) return false;
        auto& loop = loops_[index];
        const double scale = sampleRate_ / sourceSampleRate;
        const uint32_t restoredFrames = std::min<uint32_t>(
            loopCapacityFrames_, std::max<uint32_t>(2u,
                static_cast<uint32_t>(std::llround(
                    static_cast<double>(frames) * scale))));
        for (uint32_t frame = 0u; frame < restoredFrames; ++frame) {
            const double sourcePosition = static_cast<double>(frame) / scale;
            const uint32_t first = std::min<uint32_t>(frames - 1u,
                static_cast<uint32_t>(sourcePosition));
            const uint32_t second = std::min<uint32_t>(frames - 1u,
                first + 1u);
            const float amount = static_cast<float>(sourcePosition
                - std::floor(sourcePosition));
            loop.left[frame] = lerp(finiteOrZero(left[first]),
                finiteOrZero(left[second]), amount);
            loop.right[frame] = lerp(finiteOrZero(right[first]),
                finiteOrZero(right[second]), amount);
        }
        loop.recordedFrames = restoredFrames;
        loop.writePosition = 0u;
        loop.preRollFrames = 0u;
        loop.phase = 0.0;
        loop.armed = false;
        loop.capturing = false;
        loop.windowTransitionRemaining = 0u;
        loop.windowTransitionTotal = 0u;
        applyRequestedLoopWindow(index);
        return true;
    }

private:
    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr float kHalfPi = 1.57079632679489661923f;

    struct StereoFrame {
        float left = 0.0f;
        float right = 0.0f;
    };

    struct LoopState {
        float* left = nullptr;
        float* right = nullptr;
        float* preRollLeft = nullptr;
        float* preRollRight = nullptr;
        uint32_t recordedFrames = 0u;
        uint32_t writePosition = 0u;
        uint32_t preRollFrames = 0u;
        double phase = 0.0;
        float activeStart = 0.0f;
        float activeEnd = 1.0f;
        float pendingStart = 0.0f;
        float pendingEnd = 1.0f;
        StereoFrame windowTransitionFrom {};
        uint32_t windowTransitionRemaining = 0u;
        uint32_t windowTransitionTotal = 0u;
        bool armed = false;
        bool capturing = false;
        bool windowInitialized = false;
        bool windowPending = false;
    };

    struct LoopWindow {
        uint32_t start = 0u;
        uint32_t frames = 0u;
    };

    class DcBlocker {
    public:
        void reset() { previousInput_ = 0.0f; previousOutput_ = 0.0f; }
        float process(float input)
        {
            const float output = input - previousInput_ + 0.995f * previousOutput_;
            previousInput_ = input;
            previousOutput_ = flushDenormal(output);
            return previousOutput_;
        }
    private:
        float previousInput_ = 0.0f;
        float previousOutput_ = 0.0f;
    };

    static float clamp(float value, float low, float high)
    {
        return std::max(low, std::min(high, value));
    }

    static float lerp(float a, float b, float amount)
    {
        return a + (b - a) * amount;
    }

    static float finiteOrZero(float value)
    {
        return std::isfinite(value) ? value : 0.0f;
    }

    static float flushDenormal(float value)
    {
        return std::abs(value) < 1.0e-20f ? 0.0f : finiteOrZero(value);
    }

    static float softGain(float sample, float gain)
    {
        return 0.5f * std::tanh(2.0f * sample * gain);
    }

    static StereoFrame equalPower(const StereoFrame& a,
                                  const StereoFrame& b, float amount)
    {
        amount = clamp(amount, 0.0f, 1.0f);
        const float aWeight = std::cos(amount * kHalfPi);
        const float bWeight = std::sin(amount * kHalfPi);
        return StereoFrame {
            a.left * aWeight + b.left * bWeight,
            a.right * aWeight + b.right * bWeight,
        };
    }

    StereoFrame crossfadeLoops(const StereoFrame& a,
                               const StereoFrame& b, float amount) const
    {
        const auto gains = crcltrCrossfadeGains(params_.crossfadeShape, amount);
        return StereoFrame {
            a.left * gains.a + b.left * gains.b,
            a.right * gains.a + b.right * gains.b,
        };
    }

    static bool sameLoopBounds(float firstStart, float firstEnd,
                               float secondStart, float secondEnd)
    {
        return std::abs(firstStart - secondStart) <= 1.0e-6f
            && std::abs(firstEnd - secondEnd) <= 1.0e-6f;
    }

    void requestedLoopBounds(uint32_t index, float& start, float& end) const
    {
        start = index == 0u ? params_.loop1Start : params_.loop2Start;
        end = index == 0u ? params_.loop1End : params_.loop2End;
    }

    void applyRequestedLoopWindow(uint32_t index)
    {
        if (index >= loops_.size()) return;
        auto& loop = loops_[index];
        requestedLoopBounds(index, loop.activeStart, loop.activeEnd);
        loop.pendingStart = loop.activeStart;
        loop.pendingEnd = loop.activeEnd;
        loop.windowInitialized = true;
        loop.windowPending = false;
    }

    void requestLoopWindow(uint32_t index)
    {
        if (index >= loops_.size()) return;
        auto& loop = loops_[index];
        float requestedStart = 0.0f;
        float requestedEnd = 1.0f;
        requestedLoopBounds(index, requestedStart, requestedEnd);
        loop.pendingStart = requestedStart;
        loop.pendingEnd = requestedEnd;
        const bool canDefer = loop.windowInitialized
            && loop.recordedFrames >= 4u && params_.playing
            && loopWindow(index).frames >= 4u
            && !loop.armed && !loop.capturing;
        if (!canDefer) {
            applyRequestedLoopWindow(index);
        } else {
            loop.windowPending = !sameLoopBounds(loop.activeStart,
                loop.activeEnd, requestedStart, requestedEnd);
        }
    }

    LoopWindow loopWindowForBounds(uint32_t index, float startNorm,
                                   float endNorm) const
    {
        if (index >= loops_.size()) return {};
        uint32_t sourceFrames = loops_[index].recordedFrames;
        if (params_.playbackModel == CrcltrPlaybackModel::Classic
            && index == 0u) sourceFrames /= 2u;
        if (sourceFrames < 2u) return {};

        const uint32_t start = std::min<uint32_t>(sourceFrames - 2u,
            static_cast<uint32_t>(startNorm * sourceFrames));
        const uint32_t end = std::clamp<uint32_t>(
            static_cast<uint32_t>(std::ceil(endNorm * sourceFrames)),
            start + 2u, sourceFrames);
        return { start, end - start };
    }

    LoopWindow loopWindow(uint32_t index) const
    {
        if (index >= loops_.size()) return {};
        return loopWindowForBounds(index, loops_[index].activeStart,
            loops_[index].activeEnd);
    }

    uint32_t loopPeriodFrames(uint32_t index,
                              const LoopWindow& window) const
    {
        if (window.frames < 2u) return 0u;
        const auto join = index == 0u ? params_.loop1Join
            : params_.loop2Join;
        if (join == CrcltrLoopJoin::Duck) return window.frames;
        const uint32_t requestedFade = static_cast<uint32_t>(
            std::round(sampleRate_ * 0.010));
        const uint32_t fadeFrames = std::min<uint32_t>(
            std::max<uint32_t>(1u, requestedFade), window.frames / 4u);
        return window.frames - fadeFrames;
    }

    void commitPendingLoopWindow(uint32_t index, bool smooth,
                                 const StereoFrame& transitionFrom)
    {
        if (index >= loops_.size()) return;
        auto& loop = loops_[index];
        if (!loop.windowPending) return;
        const uint32_t oldPeriod = loopPeriodFrames(index, loopWindow(index));
        const double phaseUnit = oldPeriod > 0u
            ? wrapPhase(loop.phase, static_cast<double>(oldPeriod))
                / static_cast<double>(oldPeriod)
            : 0.0;
        loop.activeStart = loop.pendingStart;
        loop.activeEnd = loop.pendingEnd;
        loop.windowPending = false;
        const uint32_t newPeriod = loopPeriodFrames(index, loopWindow(index));
        loop.phase = newPeriod > 0u
            ? phaseUnit * static_cast<double>(newPeriod) : 0.0;
        if (!smooth || newPeriod == 0u) {
            loop.windowTransitionRemaining = 0u;
            loop.windowTransitionTotal = 0u;
            return;
        }
        const uint32_t requestedTransition = std::max<uint32_t>(1u,
            static_cast<uint32_t>(std::round(sampleRate_ * 0.005)));
        loop.windowTransitionTotal = std::min<uint32_t>(requestedTransition,
            std::max<uint32_t>(1u, newPeriod / 4u));
        loop.windowTransitionRemaining = loop.windowTransitionTotal;
        loop.windowTransitionFrom = transitionFrom;
    }

    bool validExternalMemory(const CrcltrMemory& memory) const
    {
        return memory.loop1Left && memory.loop1Right
            && memory.loop2Left && memory.loop2Right
            && memory.preRoll1Left && memory.preRoll1Right
            && memory.preRoll2Left && memory.preRoll2Right
            && memory.loopCapacityFrames >= loopCapacityFrames_
            && memory.preRollCapacityFrames >= preRollCapacityFrames_;
    }

    void assignExternalMemory(const CrcltrMemory& memory)
    {
        loops_[0].left = memory.loop1Left;
        loops_[0].right = memory.loop1Right;
        loops_[1].left = memory.loop2Left;
        loops_[1].right = memory.loop2Right;
        loops_[0].preRollLeft = memory.preRoll1Left;
        loops_[0].preRollRight = memory.preRoll1Right;
        loops_[1].preRollLeft = memory.preRoll2Left;
        loops_[1].preRollRight = memory.preRoll2Right;
    }

#if !defined(S3G_CRCLTR_EXTERNAL_MEMORY_ONLY)
    bool allocateOwnedMemory()
    {
        try {
            for (auto& channel : ownedLoopMemory_)
                channel.resize(loopCapacityFrames_);
            for (auto& channel : ownedPreRollMemory_)
                channel.resize(preRollCapacityFrames_);
        } catch (...) {
            return false;
        }
        return true;
    }

    void assignOwnedMemory()
    {
        loops_[0].left = ownedLoopMemory_[0].data();
        loops_[0].right = ownedLoopMemory_[1].data();
        loops_[1].left = ownedLoopMemory_[2].data();
        loops_[1].right = ownedLoopMemory_[3].data();
        loops_[0].preRollLeft = ownedPreRollMemory_[0].data();
        loops_[0].preRollRight = ownedPreRollMemory_[1].data();
        loops_[1].preRollLeft = ownedPreRollMemory_[2].data();
        loops_[1].preRollRight = ownedPreRollMemory_[3].data();
    }
#endif

    bool targetIncludes(uint32_t loopIndex) const
    {
        if (latchedTarget_ == CrcltrRecordTarget::Both) return true;
        return loopIndex == 0u
            ? latchedTarget_ == CrcltrRecordTarget::Loop1
            : latchedTarget_ == CrcltrRecordTarget::Loop2;
    }

    void armSelectedLoops()
    {
        latchedTarget_ = params_.recordTarget;
        latchedRecordMode_ = params_.recordMode;
        for (uint32_t index = 0u; index < loops_.size(); ++index) {
            if (!targetIncludes(index)) continue;
            auto& loop = loops_[index];
            if (latchedRecordMode_ != CrcltrRecordMode::Replace
                && loop.recordedFrames >= 4u) {
                loop.writePosition = static_cast<uint32_t>(loop.phase)
                    % loop.recordedFrames;
                loop.preRollFrames = 0u;
                loop.armed = false;
                loop.capturing = true;
                continue;
            }
            loop.preRollFrames = 0u;
            loop.writePosition = 0u;
            loop.armed = true;
            loop.capturing = false;
        }
    }

    void beginCapture(LoopState& loop)
    {
        loop.recordedFrames = 0u;
        loop.writePosition = std::min(loop.preRollFrames, loopCapacityFrames_);
        std::copy_n(loop.preRollLeft, loop.writePosition, loop.left);
        std::copy_n(loop.preRollRight, loop.writePosition, loop.right);
        loop.phase = 0.0;
        loop.armed = false;
        loop.capturing = true;
        if (loop.writePosition >= loopCapacityFrames_) finishCapture(loop);
    }

    void finishCapture(LoopState& loop)
    {
        if (latchedRecordMode_ == CrcltrRecordMode::Replace
            && loop.capturing && loop.writePosition >= preRollCapacityFrames_)
            loop.recordedFrames = loop.writePosition;
        loop.phase = 0.0;
        loop.armed = false;
        loop.capturing = false;
        loop.preRollFrames = 0u;
    }

    void captureSample(LoopState& loop, float left, float right)
    {
        if (loop.capturing
            && latchedRecordMode_ != CrcltrRecordMode::Replace) {
            if (loop.recordedFrames < 2u) return;
            const uint32_t frame = loop.writePosition % loop.recordedFrames;
            if (latchedRecordMode_ == CrcltrRecordMode::Overdub) {
                loop.left[frame] = clamp(
                    loop.left[frame] * params_.overdubFeedback + left,
                    -4.0f, 4.0f);
                loop.right[frame] = clamp(
                    loop.right[frame] * params_.overdubFeedback + right,
                    -4.0f, 4.0f);
            } else {
                loop.left[frame] = left;
                loop.right[frame] = right;
            }
            loop.writePosition = (frame + 1u) % loop.recordedFrames;
            return;
        }
        if (loop.armed) {
            if (loop.preRollFrames < preRollCapacityFrames_) {
                loop.preRollLeft[loop.preRollFrames] = left;
                loop.preRollRight[loop.preRollFrames] = right;
                ++loop.preRollFrames;
            }
            if (loop.preRollFrames >= preRollCapacityFrames_) beginCapture(loop);
            return;
        }
        if (!loop.capturing) return;
        if (loop.writePosition < loopCapacityFrames_) {
            loop.left[loop.writePosition] = left;
            loop.right[loop.writePosition] = right;
            ++loop.writePosition;
        }
        if (loop.writePosition >= loopCapacityFrames_) finishCapture(loop);
    }

    void updateRecordGate(bool high, float left, float right)
    {
        if (high && !gateWasHigh_) armSelectedLoops();

        if (high) {
            for (uint32_t index = 0u; index < loops_.size(); ++index)
                if (targetIncludes(index)) captureSample(loops_[index], left, right);
        } else if (gateWasHigh_) {
            for (uint32_t index = 0u; index < loops_.size(); ++index) {
                if (!targetIncludes(index)) continue;
                if (loops_[index].capturing) finishCapture(loops_[index]);
                else {
                    loops_[index].armed = false;
                    loops_[index].preRollFrames = 0u;
                }
            }
        }
        gateWasHigh_ = high;
    }

    static double wrapPhase(double phase, double period)
    {
        if (period <= 0.0) return 0.0;
        phase -= std::floor(phase / period) * period;
        return phase < 0.0 ? phase + period : phase;
    }

    static float readLinear(const float* data, uint32_t frames, double position)
    {
        if (!data || frames < 2u) return 0.0f;
        const double wrapped = wrapPhase(position, static_cast<double>(frames));
        const uint32_t first = static_cast<uint32_t>(wrapped);
        const uint32_t second = first + 1u < frames ? first + 1u : 0u;
        const float fraction = static_cast<float>(wrapped - std::floor(wrapped));
        return lerp(data[first], data[second], fraction);
    }

    StereoFrame readLoop(uint32_t index, float rate)
    {
        if (index >= loops_.size()) return {};
        auto& loop = loops_[index];
        const auto selected = loopWindow(index);
        if (selected.frames < 4u || (loop.capturing
            && latchedRecordMode_ == CrcltrRecordMode::Replace)) return {};
        const auto join = index == 0u ? params_.loop1Join
            : params_.loop2Join;
        bool wrapped = false;
        StereoFrame result = join == CrcltrLoopJoin::Seam
            ? readSeamLoop(loop, selected, rate, wrapped)
            : readDuckLoop(loop, selected, rate, wrapped);
        if (loop.windowTransitionRemaining > 0u
            && loop.windowTransitionTotal > 0u) {
            const float amount = static_cast<float>(
                loop.windowTransitionTotal - loop.windowTransitionRemaining + 1u)
                / static_cast<float>(loop.windowTransitionTotal);
            result.left = lerp(loop.windowTransitionFrom.left,
                result.left, amount);
            result.right = lerp(loop.windowTransitionFrom.right,
                result.right, amount);
            --loop.windowTransitionRemaining;
        }
        if (wrapped && loop.windowPending)
            commitPendingLoopWindow(index, true, result);
        return result;
    }

    StereoFrame readSeamLoop(LoopState& loop, const LoopWindow& window,
                             float rate, bool& wrapped)
    {
        const uint32_t sourceFrames = window.frames;

        const uint32_t requestedFade = static_cast<uint32_t>(
            std::round(sampleRate_ * 0.010));
        const uint32_t fadeFrames = std::min<uint32_t>(
            std::max<uint32_t>(1u, requestedFade), sourceFrames / 4u);
        const uint32_t periodFrames = sourceFrames - fadeFrames;
        if (periodFrames < 2u) return {};

        const double phase = wrapPhase(loop.phase,
            static_cast<double>(periodFrames));
        const double crossfadeStart = static_cast<double>(
            sourceFrames - 2u * fadeFrames);
        StereoFrame result {};
        if (phase < crossfadeStart || fadeFrames == 0u) {
            const double sourcePosition = phase + static_cast<double>(fadeFrames);
            result.left = readLinearWindow(loop.left, window.start,
                sourceFrames, sourcePosition);
            result.right = readLinearWindow(loop.right, window.start,
                sourceFrames, sourcePosition);
        } else {
            const float amount = clamp(static_cast<float>(
                (phase - crossfadeStart) / static_cast<double>(fadeFrames)),
                0.0f, 1.0f);
            const double tailPosition = phase + static_cast<double>(fadeFrames);
            const double headPosition = phase - crossfadeStart;
            const StereoFrame tail {
                readLinearWindow(loop.left, window.start, sourceFrames,
                    tailPosition),
                readLinearWindow(loop.right, window.start, sourceFrames,
                    tailPosition),
            };
            const StereoFrame head {
                readLinearWindow(loop.left, window.start, sourceFrames,
                    headPosition),
                readLinearWindow(loop.right, window.start, sourceFrames,
                    headPosition),
            };
            result = equalPower(tail, head, amount);
        }

        const double nextPhase = phase + static_cast<double>(rate);
        wrapped = nextPhase < 0.0
            || nextPhase >= static_cast<double>(periodFrames);
        loop.phase = wrapPhase(nextPhase,
            static_cast<double>(periodFrames));
        return result;
    }

    StereoFrame readDuckLoop(LoopState& loop, const LoopWindow& selected,
                             float rate, bool& wrapped)
    {
        const uint32_t frames = selected.frames;
        const double phase = wrapPhase(loop.phase, static_cast<double>(frames));
        const double duckFrames = std::min<double>(
            sampleRate_ * 0.250, static_cast<double>(frames) * 0.5);
        const double edgeDistance = std::min(phase,
            std::max(0.0, static_cast<double>(frames) - phase));
        const float duckUnit = duckFrames > 0.0
            ? clamp(static_cast<float>(edgeDistance / duckFrames), 0.0f, 1.0f)
            : 1.0f;
        const float window = std::sin(duckUnit * kHalfPi);
        const float gain = window * window;
        const StereoFrame result {
            readLinearWindow(loop.left, selected.start, frames, phase) * gain,
            readLinearWindow(loop.right, selected.start, frames, phase) * gain,
        };
        const double nextPhase = phase + static_cast<double>(rate);
        wrapped = nextPhase < 0.0
            || nextPhase >= static_cast<double>(frames);
        loop.phase = wrapPhase(nextPhase,
            static_cast<double>(frames));
        return result;
    }

    static float readLinearWindow(const float* data, uint32_t start,
                                  uint32_t frames, double position)
    {
        if (!data || frames < 2u) return 0.0f;
        const double wrapped = wrapPhase(position,
            static_cast<double>(frames));
        const uint32_t first = static_cast<uint32_t>(wrapped);
        const uint32_t second = first + 1u < frames ? first + 1u : 0u;
        const float amount = static_cast<float>(wrapped - std::floor(wrapped));
        return lerp(data[start + first], data[start + second], amount);
    }

    StereoFrame monitorFrame(uint32_t loopIndex, float left, float right) const
    {
        const bool enabled = (loopIndex == 0u
            && params_.monitorMode == CrcltrMonitorMode::Loop1)
            || (loopIndex == 1u
                && params_.monitorMode == CrcltrMonitorMode::Loop2);
        return enabled ? StereoFrame { left, right } : StereoFrame {};
    }

    float nextRandomCrossfadeValue()
    {
        randomState_ ^= randomState_ << 13u;
        randomState_ ^= randomState_ >> 17u;
        randomState_ ^= randomState_ << 5u;
        return static_cast<float>(randomState_ & 0xffffu) / 65535.0f;
    }

    float nextCrossfade(float loop1Rate, float loop2Rate)
    {
        float target = params_.crossfade;
        if (params_.crossfadeMode != CrcltrCrossfadeMode::Manual) {
            uint32_t referenceFrames = playbackFrames(0u);
            float referenceRate = loop1Rate;
            if (referenceFrames < 4u) {
                referenceFrames = playbackFrames(1u);
                referenceRate = loop2Rate;
            }
            if (referenceFrames < 4u) {
                referenceFrames = std::max<uint32_t>(4u,
                    static_cast<uint32_t>(std::round(sampleRate_)));
                referenceRate = 1.0f;
            }
            const float rateMultiplier = 0.25f
                * std::pow(16.0f, params_.crossfade);
            const double motionCycleFrames = static_cast<double>(
                referenceFrames) / std::max(0.0001,
                    static_cast<double>(referenceRate * rateMultiplier));
            const double nextPhase = xfadeLfoPhase_
                + static_cast<double>(referenceRate * rateMultiplier)
                    / static_cast<double>(referenceFrames);
            const bool cycleWrapped = nextPhase >= 1.0;
            xfadeLfoPhase_ = wrapPhase(nextPhase, 1.0);
            const float phase = static_cast<float>(xfadeLfoPhase_);
            if (params_.crossfadeMode == CrcltrCrossfadeMode::Sine) {
                target = 0.5f - 0.5f * std::cos(2.0f * kPi * phase);
            } else if (params_.crossfadeMode
                       == CrcltrCrossfadeMode::RandomWalk) {
                if (randomWalkCounter_ == 0u) {
                    randomWalkTarget_ = nextRandomCrossfadeValue();
                    randomWalkCounter_ = std::max<uint32_t>(1u,
                        static_cast<uint32_t>(std::round(motionCycleFrames)));
                } else {
                    --randomWalkCounter_;
                }
                randomWalkValue_ += (randomWalkTarget_ - randomWalkValue_)
                    * static_cast<float>(1.0 / std::max(1.0,
                        motionCycleFrames * 0.35));
                target = randomWalkValue_;
            } else if (params_.crossfadeMode
                       == CrcltrCrossfadeMode::Triangle) {
                target = phase < 0.5f ? phase * 2.0f
                    : 2.0f - phase * 2.0f;
            } else if (params_.crossfadeMode
                       == CrcltrCrossfadeMode::RampAToB) {
                target = phase;
            } else if (params_.crossfadeMode
                       == CrcltrCrossfadeMode::RampBToA) {
                target = 1.0f - phase;
            } else if (params_.crossfadeMode
                       == CrcltrCrossfadeMode::SampleHold) {
                if (!sampleHoldInitialized_ || cycleWrapped) {
                    sampleHoldValue_ = nextRandomCrossfadeValue();
                    sampleHoldInitialized_ = true;
                }
                target = sampleHoldValue_;
            } else if (params_.crossfadeMode
                       == CrcltrCrossfadeMode::Square) {
                target = phase < 0.5f ? 0.0f : 1.0f;
            } else if (phase < 0.25f) {
                target = phase * 4.0f;
            } else if (phase < 0.5f) {
                target = 1.0f;
            } else if (phase < 0.75f) {
                target = 3.0f - phase * 4.0f;
            } else {
                target = 0.0f;
            }
        }
        crossfade_.setTarget(clamp(target, 0.0f, 1.0f));
        previousCrossfade_ = currentCrossfade_;
        currentCrossfade_ = crossfade_.next();
        const float movement = currentCrossfade_ - previousCrossfade_;
        crossfadeDirection_ = movement > 1.0e-7f ? 1.0f
            : movement < -1.0e-7f ? -1.0f : 0.0f;
        return currentCrossfade_;
    }

    double sampleRate_ = 48000.0;
    uint32_t loopCapacityFrames_ = 0u;
    uint32_t preRollCapacityFrames_ = 0u;
    bool prepared_ = false;
    bool usingExternalMemory_ = false;
    bool gateWasHigh_ = false;
    CrcltrRecordTarget latchedTarget_ = CrcltrRecordTarget::Both;
    CrcltrRecordMode latchedRecordMode_ = CrcltrRecordMode::Replace;
    double xfadeLfoPhase_ = 0.0;
    float randomWalkValue_ = 0.5f;
    float randomWalkTarget_ = 0.5f;
    float sampleHoldValue_ = 0.5f;
    float currentCrossfade_ = 0.5f;
    float previousCrossfade_ = 0.5f;
    float crossfadeDirection_ = 0.0f;
    uint32_t randomWalkCounter_ = 0u;
    uint32_t randomState_ = 0x6d2b79f5u;
    bool sampleHoldInitialized_ = false;
    CrcltrParams params_ {};
    std::array<LoopState, 2u> loops_ {};
    std::array<DcBlocker, 2u> inputDc_ {};
    std::array<SmoothedValue, 2u> recordMix_ {};
    SmoothedValue loop1Rate_ {};
    SmoothedValue loop2Rate_ {};
    SmoothedValue crossfade_ {};
    SmoothedValue blend_ {};
    SmoothedValue inputGain_ {};
    SmoothedValue outputGain_ {};
#if !defined(S3G_CRCLTR_EXTERNAL_MEMORY_ONLY)
    std::array<std::vector<float>, 4u> ownedLoopMemory_ {};
    std::array<std::vector<float>, 4u> ownedPreRollMemory_ {};
#endif
};

} // namespace s3g
