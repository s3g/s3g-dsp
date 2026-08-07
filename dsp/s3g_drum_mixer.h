#pragma once

#include "s3g_drum_overload.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kDrumMixerLaneCount = 8u;
constexpr uint32_t kDrumMixerChannelCount = kDrumMixerLaneCount * 2u;

enum class DrumMixerOutputMode : uint32_t {
    Sum = 0u,
    Direct = 1u,
};

inline const char* drumMixerOutputModeName(DrumMixerOutputMode mode)
{
    return mode == DrumMixerOutputMode::Direct ? "DIRECT" : "SUM";
}

struct DrumMixerLaneParams {
    float levelDb = 0.0f;
    float pan = 0.0f;
    float lowEqDb = 0.0f;
    float midEqDb = 0.0f;
    float highEqDb = 0.0f;
    float auxSend = 0.0f;
    bool mute = false;
    bool solo = false;
    float midFrequencyHz = 900.0f;
};

struct DrumMixerParams {
    std::array<DrumMixerLaneParams, kDrumMixerLaneCount> lanes {};
    DrumMixerOutputMode outputMode = DrumMixerOutputMode::Sum;
    float masterLevelDb = -6.0f;
    bool busEnabled = false;
    float busDrive = 0.38f;
    float busGlue = 0.34f;
    float busRoom = 0.0f;
    float busWeight = 0.68f;
    float busTone = 0.0f;
    float busReturnDb = -9.0f;
};

// Eight stereo strips feeding either a stereo sum or eight post-strip direct
// pairs. The parallel AUX return is deliberately host-independent so the same
// routing and drum-bus behavior can later live directly inside s3g-tracker.
class DrumMixer {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? std::clamp(sampleRate, 8000.0, 768000.0) : 48000.0;
        const float sr = static_cast<float>(sampleRate_);
        smoothCoefficient_ = onePoleCoefficient(6.0f, sr);
        switchCoefficient_ = onePoleCoefficient(5.0f, sr);
        lowCoefficient_ = frequencyCoefficient(170.0f, sr);
        highCoefficient_ = frequencyCoefficient(4200.0f, sr);
        fastAttackCoefficient_ = onePoleCoefficient(0.18f, sr);
        fastReleaseCoefficient_ = onePoleCoefficient(24.0f, sr);
        slowAttackCoefficient_ = onePoleCoefficient(14.0f, sr);
        slowReleaseCoefficient_ = onePoleCoefficient(260.0f, sr);
        updateMidFrequencyTargets();
        busProcessor_.prepare(sampleRate_);
        reset();
    }

    void reset()
    {
        laneStates_.fill({});
        laneOutput_.fill({});
        busFastEnvelope_ = 0.0f;
        busSlowEnvelope_ = 0.0f;
        busRoomGain_ = 1.0f;
        busActivity_ = 0.0f;
        busProcessor_.reset();
        initializeSmoothedState();
    }

    void setParams(DrumMixerParams params)
    {
        target_ = sanitize(params);
        updateMidFrequencyTargets();
        updateBusProcessorParams();
    }

    DrumMixerParams params() const { return target_; }

    void processFrame(
        const std::array<float, kDrumMixerChannelCount>& input,
        std::array<float, kDrumMixerChannelCount>& output)
    {
        output.fill(0.0f);
        laneOutput_.fill({});

        bool anySolo = false;
        for (const auto& lane : target_.lanes) anySolo |= lane.solo;
        smoothParameters(anySolo);

        std::array<float, 2u> main {};
        std::array<float, 2u> aux {};
        for (uint32_t lane = 0u; lane < kDrumMixerLaneCount; ++lane) {
            const auto& strip = smoothedLanes_[lane];
            constexpr float midQInverse = 1.25f;
            const float g = strip.midFrequencyG;
            const float a1 = 1.0f
                / (1.0f + g * (g + midQInverse));
            const float a2 = g * a1;
            const float a3 = g * a2;
            for (uint32_t side = 0u; side < 2u; ++side) {
                const uint32_t channel = lane * 2u + side;
                const float clean = finiteSample(input[channel]);
                auto& filter = laneStates_[lane].channels[side];
                filter.low = flushDenormal(filter.low
                    + (clean - filter.low) * lowCoefficient_);
                filter.lowMid = flushDenormal(filter.lowMid
                    + (clean - filter.lowMid) * highCoefficient_);
                const float low = filter.low;
                const float high = clean - filter.lowMid;
                const float v3 = clean - filter.midIc2;
                const float v1 = a1 * filter.midIc1 + a2 * v3;
                const float v2 = filter.midIc2
                    + a2 * filter.midIc1 + a3 * v3;
                filter.midIc1 = flushDenormal(2.0f * v1 - filter.midIc1);
                filter.midIc2 = flushDenormal(2.0f * v2 - filter.midIc2);
                const float mid = midQInverse * v1;
                float mixed = clean
                    + (strip.lowGain - 1.0f) * low
                    + (strip.midGain - 1.0f) * mid
                    + (strip.highGain - 1.0f) * high;
                const float panGain = side == 0u
                    ? std::sqrt(std::max(0.0f, 1.0f - std::max(0.0f,
                        strip.pan)))
                    : std::sqrt(std::max(0.0f, 1.0f + std::min(0.0f,
                        strip.pan)));
                mixed *= strip.levelGain * panGain * strip.audibleGain;
                mixed = finiteSample(mixed);
                laneOutput_[lane][side] = mixed;
                main[side] += mixed;
                aux[side] += mixed * strip.auxSend * strip.auxSend;
                if (target_.outputMode == DrumMixerOutputMode::Direct) {
                    output[channel] = mixed;
                }
            }
        }

        if (target_.outputMode == DrumMixerOutputMode::Sum) {
            float busLeft = aux[0u];
            float busRight = aux[1u];
            processRoomStage(busLeft, busRight);
            busProcessor_.processFrame(busLeft, busRight);
            const float busGain = smoothedBusEnable_ * smoothedBusReturnGain_;
            main[0u] += busLeft * busGain;
            main[1u] += busRight * busGain;
            output[0u] = finiteSample(main[0u] * smoothedMasterGain_);
            output[1u] = finiteSample(main[1u] * smoothedMasterGain_);
            busActivity_ = std::max(std::abs(busLeft), std::abs(busRight))
                * smoothedBusEnable_;
        } else {
            busActivity_ *= 0.96f;
        }
    }

    std::array<float, 2u> laneOutput(uint32_t lane) const
    {
        return lane < laneOutput_.size() ? laneOutput_[lane]
                                         : std::array<float, 2u> {};
    }

    float busActivity() const { return busActivity_; }
    float busGainReductionDb() const
    {
        return busProcessor_.gainReductionDb();
    }

private:
    struct FilterState {
        float low = 0.0f;
        float lowMid = 0.0f;
        float midIc1 = 0.0f;
        float midIc2 = 0.0f;
    };

    struct LaneState {
        std::array<FilterState, 2u> channels {};
    };

    struct SmoothedLane {
        float levelGain = 1.0f;
        float pan = 0.0f;
        float lowGain = 1.0f;
        float midGain = 1.0f;
        float highGain = 1.0f;
        float auxSend = 0.0f;
        float midFrequencyG = 0.06f;
        float audibleGain = 1.0f;
    };

    static float finiteOr(float value, float fallback = 0.0f)
    {
        return std::isfinite(value) ? value : fallback;
    }

    static float finiteSample(float value)
    {
        return std::isfinite(value) ? flushDenormal(value) : 0.0f;
    }

    static float onePoleCoefficient(float timeMs, float sampleRate)
    {
        const float seconds = std::max(0.000001f, timeMs * 0.001f);
        return 1.0f - std::exp(-1.0f / (seconds * sampleRate));
    }

    static float frequencyCoefficient(float frequencyHz, float sampleRate)
    {
        const float frequency = clamp(
            frequencyHz, 1.0f, sampleRate * 0.45f);
        return 1.0f - std::exp(-2.0f * kPi * frequency / sampleRate);
    }

    static float followEnvelope(float current, float input,
        float attackCoefficient, float releaseCoefficient)
    {
        const float coefficient = input > current
            ? attackCoefficient : releaseCoefficient;
        return flushDenormal(current + (input - current) * coefficient);
    }

    static DrumMixerParams sanitize(DrumMixerParams params)
    {
        params.outputMode = params.outputMode == DrumMixerOutputMode::Direct
            ? DrumMixerOutputMode::Direct : DrumMixerOutputMode::Sum;
        params.masterLevelDb = clamp(
            finiteOr(params.masterLevelDb, -6.0f), -60.0f, 12.0f);
        params.busDrive = clamp(finiteOr(params.busDrive, 0.38f), 0.0f, 1.0f);
        params.busGlue = clamp(finiteOr(params.busGlue, 0.34f), 0.0f, 1.0f);
        params.busRoom = clamp(finiteOr(params.busRoom), -1.0f, 1.0f);
        params.busWeight = clamp(finiteOr(params.busWeight, 0.68f), 0.0f, 1.0f);
        params.busTone = clamp(finiteOr(params.busTone), -1.0f, 1.0f);
        params.busReturnDb = clamp(
            finiteOr(params.busReturnDb, -9.0f), -60.0f, 12.0f);
        for (auto& lane : params.lanes) {
            lane.levelDb = clamp(finiteOr(lane.levelDb), -60.0f, 12.0f);
            lane.pan = clamp(finiteOr(lane.pan), -1.0f, 1.0f);
            lane.lowEqDb = clamp(finiteOr(lane.lowEqDb), -12.0f, 12.0f);
            lane.midEqDb = clamp(finiteOr(lane.midEqDb), -12.0f, 12.0f);
            lane.highEqDb = clamp(finiteOr(lane.highEqDb), -12.0f, 12.0f);
            lane.auxSend = clamp(finiteOr(lane.auxSend), 0.0f, 1.0f);
            lane.midFrequencyHz = clamp(
                finiteOr(lane.midFrequencyHz, 900.0f), 120.0f, 8000.0f);
        }
        return params;
    }

    float midFrequencyG(float frequencyHz) const
    {
        const float normalized = clamp(frequencyHz, 120.0f, 8000.0f)
            / static_cast<float>(sampleRate_);
        return std::tan(kPi * std::min(normalized, 0.45f));
    }

    void updateMidFrequencyTargets()
    {
        for (uint32_t lane = 0u; lane < kDrumMixerLaneCount; ++lane) {
            targetMidFrequencyG_[lane] = midFrequencyG(
                target_.lanes[lane].midFrequencyHz);
        }
    }

    void initializeSmoothedState()
    {
        const bool anySolo = std::any_of(target_.lanes.begin(),
            target_.lanes.end(), [](const auto& candidate) {
                return candidate.solo;
            });
        for (uint32_t lane = 0u; lane < kDrumMixerLaneCount; ++lane) {
            const auto& target = target_.lanes[lane];
            const bool audible = !target.mute && (!anySolo || target.solo);
            smoothedLanes_[lane] = {
                dbToGain(target.levelDb), target.pan,
                dbToGain(target.lowEqDb), dbToGain(target.midEqDb),
                dbToGain(target.highEqDb), target.auxSend,
                targetMidFrequencyG_[lane], audible ? 1.0f : 0.0f,
            };
        }
        smoothedMasterGain_ = dbToGain(target_.masterLevelDb);
        smoothedBusEnable_ = target_.busEnabled ? 1.0f : 0.0f;
        smoothedBusReturnGain_ = dbToGain(target_.busReturnDb);
        smoothedRoom_ = target_.busRoom;
    }

    void smoothParameters(bool anySolo)
    {
        const auto smooth = [this](float& current, float target) {
            current = flushDenormal(current
                + (target - current) * smoothCoefficient_);
        };
        for (uint32_t lane = 0u; lane < kDrumMixerLaneCount; ++lane) {
            const auto& target = target_.lanes[lane];
            auto& current = smoothedLanes_[lane];
            smooth(current.levelGain, dbToGain(target.levelDb));
            smooth(current.pan, target.pan);
            smooth(current.lowGain, dbToGain(target.lowEqDb));
            smooth(current.midGain, dbToGain(target.midEqDb));
            smooth(current.highGain, dbToGain(target.highEqDb));
            smooth(current.auxSend, target.auxSend);
            smooth(current.midFrequencyG, targetMidFrequencyG_[lane]);
            const bool audible = !target.mute && (!anySolo || target.solo);
            current.audibleGain = flushDenormal(current.audibleGain
                + ((audible ? 1.0f : 0.0f) - current.audibleGain)
                    * switchCoefficient_);
        }
        smooth(smoothedMasterGain_, dbToGain(target_.masterLevelDb));
        smooth(smoothedBusEnable_, target_.busEnabled ? 1.0f : 0.0f);
        smooth(smoothedBusReturnGain_, dbToGain(target_.busReturnDb));
        smooth(smoothedRoom_, target_.busRoom);
    }

    void updateBusProcessorParams()
    {
        DrumOverloadParams params;
        params.circuit = DrumOverloadCircuit::Console;
        params.inputGainDb = target_.busDrive * 12.0f;
        params.overload = 0.18f + target_.busDrive * 0.72f;
        params.density = target_.busGlue;
        params.punch = 0.08f + target_.busGlue * 0.20f;
        params.bias = 0.08f;
        params.breakup = target_.busGlue * 0.18f;
        params.weight = target_.busWeight;
        params.tone = target_.busTone;
        params.stereoLink = 0.92f;
        params.mix = 1.0f;
        params.outputGainDb = -3.0f;
        params.bypass = false;
        busProcessor_.setParams(params);
    }

    void processRoomStage(float& left, float& right)
    {
        const float magnitude = std::max(std::abs(left), std::abs(right));
        busFastEnvelope_ = followEnvelope(busFastEnvelope_, magnitude,
            fastAttackCoefficient_, fastReleaseCoefficient_);
        busSlowEnvelope_ = followEnvelope(busSlowEnvelope_, magnitude,
            slowAttackCoefficient_, slowReleaseCoefficient_);
        const float transient = clamp(
            (busFastEnvelope_ - busSlowEnvelope_)
                / (busSlowEnvelope_ + 0.025f), 0.0f, 1.0f);
        const float body = 1.0f - transient;
        const float targetGain = smoothedRoom_ < 0.0f
            ? 1.0f - (-smoothedRoom_) * body * 0.94f
            : 1.0f + smoothedRoom_ * body * 1.45f;
        busRoomGain_ = flushDenormal(busRoomGain_
            + (targetGain - busRoomGain_) * fastReleaseCoefficient_);
        left = finiteSample(left * busRoomGain_);
        right = finiteSample(right * busRoomGain_);
    }

    double sampleRate_ = 48000.0;
    DrumMixerParams target_ {};
    std::array<LaneState, kDrumMixerLaneCount> laneStates_ {};
    std::array<SmoothedLane, kDrumMixerLaneCount> smoothedLanes_ {};
    std::array<float, kDrumMixerLaneCount> targetMidFrequencyG_ {};
    std::array<std::array<float, 2u>, kDrumMixerLaneCount> laneOutput_ {};
    DrumOverload busProcessor_ {};
    float smoothCoefficient_ = 0.0035f;
    float switchCoefficient_ = 0.0042f;
    float lowCoefficient_ = 0.02f;
    float highCoefficient_ = 0.35f;
    float fastAttackCoefficient_ = 0.10f;
    float fastReleaseCoefficient_ = 0.001f;
    float slowAttackCoefficient_ = 0.001f;
    float slowReleaseCoefficient_ = 0.0001f;
    float smoothedMasterGain_ = dbToGain(-6.0f);
    float smoothedBusEnable_ = 0.0f;
    float smoothedBusReturnGain_ = dbToGain(-9.0f);
    float smoothedRoom_ = 0.0f;
    float busFastEnvelope_ = 0.0f;
    float busSlowEnvelope_ = 0.0f;
    float busRoomGain_ = 1.0f;
    float busActivity_ = 0.0f;
};

} // namespace s3g
