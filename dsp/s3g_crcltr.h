#pragma once

#include "s3g_smoothing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr float kCrcltrMaximumRecordSeconds = 32.0f;
constexpr float kCrcltrMinimumRecordSeconds = 0.1f;

enum class CrcltrCrossfadeMode : uint32_t {
    Manual = 0u,
    Sine = 1u,
    Trapezoid = 2u,
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
    float crossfade = 0.5f;
    float blend = 0.5f;
    float inputGain = 1.0f;
    float outputGain = 1.0f;
    bool record = false;
    CrcltrRecordTarget recordTarget = CrcltrRecordTarget::Both;
    CrcltrMonitorMode monitorMode = CrcltrMonitorMode::Mute;
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
            if (!allocateOwnedMemory()) return false;
            assignOwnedMemory();
            usingExternalMemory_ = false;
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
        xfadeLfoPhase_ = 0.0;
        inputDc_[0].reset();
        inputDc_[1].reset();
        for (auto& loop : loops_) {
            loop.phase = 0.0;
            loop.writePosition = 0u;
            loop.preRollFrames = 0u;
            loop.armed = false;
            loop.capturing = false;
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
            std::min<uint32_t>(static_cast<uint32_t>(params_.crossfadeMode), 2u));
        params_.crossfade = clamp(params_.crossfade, 0.0f, 1.0f);
        params_.blend = clamp(params_.blend, 0.0f, 1.0f);
        params_.inputGain = clamp(params_.inputGain, 0.0f, 1.0f);
        params_.outputGain = clamp(params_.outputGain, 0.0f, 1.0f);
        params_.recordTarget = static_cast<CrcltrRecordTarget>(
            std::min<uint32_t>(static_cast<uint32_t>(params_.recordTarget), 2u));
        params_.monitorMode = static_cast<CrcltrMonitorMode>(
            std::min<uint32_t>(static_cast<uint32_t>(params_.monitorMode), 2u));

        loop1Rate_.setTarget(params_.loop1Rate);
        loop2Rate_.setTarget(params_.loop2Rate);
        blend_.setTarget(params_.blend);
        inputGain_.setTarget(params_.inputGain);
        outputGain_.setTarget(params_.outputGain);
    }

    const CrcltrParams& params() const { return params_; }

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
            const StereoFrame loop1 = readLoop1(rate1);
            const StereoFrame loop2 = readLoop2(rate2);

            recordMix_[0].setTarget(loops_[0].capturing ? 1.0f : 0.0f);
            recordMix_[1].setTarget(loops_[1].capturing ? 1.0f : 0.0f);
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

            const float xfade = nextCrossfade(rate1);
            const StereoFrame wet = equalPower(slot1, slot2, xfade);
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
        if (index >= loops_.size()) return 0u;
        return index == 0u
            ? loops_[0].recordedFrames / 2u
            : loops_[1].recordedFrames;
    }

    bool isRecording(uint32_t index) const
    {
        return index < loops_.size() && loops_[index].capturing;
    }

    bool usingExternalMemory() const { return usingExternalMemory_; }
    uint32_t loopCapacityFrames() const { return loopCapacityFrames_; }

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
        bool armed = false;
        bool capturing = false;
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
        for (uint32_t index = 0u; index < loops_.size(); ++index) {
            if (!targetIncludes(index)) continue;
            auto& loop = loops_[index];
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
        if (loop.capturing && loop.writePosition >= preRollCapacityFrames_)
            loop.recordedFrames = loop.writePosition;
        loop.phase = 0.0;
        loop.armed = false;
        loop.capturing = false;
        loop.preRollFrames = 0u;
    }

    void captureSample(LoopState& loop, float left, float right)
    {
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

    StereoFrame readLoop1(float rate)
    {
        auto& loop = loops_[0];
        const uint32_t sourceFrames = loop.recordedFrames / 2u;
        if (sourceFrames < 4u || loop.capturing) return {};

        const uint32_t requestedFade = static_cast<uint32_t>(
            std::round(sampleRate_ * 0.010));
        const uint32_t fadeFrames = std::min<uint32_t>(
            std::max<uint32_t>(1u, requestedFade), sourceFrames / 4u);
        const uint32_t periodFrames = sourceFrames - fadeFrames;
        if (periodFrames < 2u) return {};

        const double phase = wrapPhase(loop.phase, static_cast<double>(periodFrames));
        const double crossfadeStart = static_cast<double>(sourceFrames - 2u * fadeFrames);
        StereoFrame result {};
        if (phase < crossfadeStart || fadeFrames == 0u) {
            const double sourcePosition = phase + static_cast<double>(fadeFrames);
            result.left = readLinear(loop.left, sourceFrames, sourcePosition);
            result.right = readLinear(loop.right, sourceFrames, sourcePosition);
        } else {
            const float amount = clamp(static_cast<float>(
                (phase - crossfadeStart) / static_cast<double>(fadeFrames)),
                0.0f, 1.0f);
            const double tailPosition = phase + static_cast<double>(fadeFrames);
            const double headPosition = phase - crossfadeStart;
            const StereoFrame tail {
                readLinear(loop.left, sourceFrames, tailPosition),
                readLinear(loop.right, sourceFrames, tailPosition),
            };
            const StereoFrame head {
                readLinear(loop.left, sourceFrames, headPosition),
                readLinear(loop.right, sourceFrames, headPosition),
            };
            result = equalPower(tail, head, amount);
        }

        loop.phase = wrapPhase(phase + static_cast<double>(rate),
            static_cast<double>(periodFrames));
        return result;
    }

    StereoFrame readLoop2(float rate)
    {
        auto& loop = loops_[1];
        const uint32_t frames = loop.recordedFrames;
        if (frames < 4u || loop.capturing) return {};

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
            readLinear(loop.left, frames, phase) * gain,
            readLinear(loop.right, frames, phase) * gain,
        };
        loop.phase = wrapPhase(phase + static_cast<double>(rate),
            static_cast<double>(frames));
        return result;
    }

    StereoFrame monitorFrame(uint32_t loopIndex, float left, float right) const
    {
        const bool enabled = (loopIndex == 0u
            && params_.monitorMode == CrcltrMonitorMode::Loop1)
            || (loopIndex == 1u
                && params_.monitorMode == CrcltrMonitorMode::Loop2);
        return enabled ? StereoFrame { left, right } : StereoFrame {};
    }

    float nextCrossfade(float loop1Rate)
    {
        float target = params_.crossfade;
        if (params_.crossfadeMode != CrcltrCrossfadeMode::Manual) {
            const uint32_t loopFrames = playbackFrames(0u);
            if (loopFrames >= 4u) {
                const float rateMultiplier = 0.25f * std::pow(16.0f, params_.crossfade);
                xfadeLfoPhase_ = wrapPhase(xfadeLfoPhase_
                    + static_cast<double>(loop1Rate * rateMultiplier)
                        / static_cast<double>(loopFrames), 1.0);
            }
            const float phase = static_cast<float>(xfadeLfoPhase_);
            if (params_.crossfadeMode == CrcltrCrossfadeMode::Sine) {
                target = 0.5f - 0.5f * std::cos(2.0f * kPi * phase);
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
        return crossfade_.next();
    }

    double sampleRate_ = 48000.0;
    uint32_t loopCapacityFrames_ = 0u;
    uint32_t preRollCapacityFrames_ = 0u;
    bool prepared_ = false;
    bool usingExternalMemory_ = false;
    bool gateWasHigh_ = false;
    CrcltrRecordTarget latchedTarget_ = CrcltrRecordTarget::Both;
    double xfadeLfoPhase_ = 0.0;
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
    std::array<std::vector<float>, 4u> ownedLoopMemory_ {};
    std::array<std::vector<float>, 4u> ownedPreRollMemory_ {};
};

} // namespace s3g
