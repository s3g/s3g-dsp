#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace s3g {

enum class ErrantMode : uint32_t {
    Cell = 0u,
    Phrase = 1u,
    Field = 2u,
};

enum class ErrantTopology : uint32_t {
    Spine = 0u,
    Wings = 1u,
    Exchange = 2u,
    Side = 3u,
};

enum class ErrantKeyRole : uint32_t {
    Pitch = 0u,
    Clock = 1u,
    Both = 2u,
};

struct ProcessorErrantParams {
    ErrantMode mode = ErrantMode::Phrase;
    float material = 0.52f;
    float span = 0.46f;
    float density = 0.58f;
    float ancestry = 0.68f;
    float mutation = 0.42f;
    float repeat = 0.38f;
    float coherence = 0.62f;
    float registerSemitones = 0.0f;
    float noteTracking = 1.0f;
    float tone = 0.18f;
    float drive = 0.20f;
    ErrantTopology topology = ErrantTopology::Wings;
    float width = 0.56f;
    uint32_t seed = 1979u;
    float velocitySensitivity = 0.78f;
    float outputGainDb = -8.0f;
    ErrantKeyRole keyRole = ErrantKeyRole::Both;
    float sub = 0.62f;
    float resonance = 0.38f;
    float filterContour = 0.46f;
    float crosswire = 0.34f;
};

inline ProcessorErrantParams sanitizeProcessorErrantParams(
    ProcessorErrantParams p)
{
    p.mode = static_cast<ErrantMode>(std::min<uint32_t>(
        static_cast<uint32_t>(p.mode), 2u));
    p.material = std::clamp(p.material, 0.0f, 1.0f);
    p.span = std::clamp(p.span, 0.0f, 1.0f);
    p.density = std::clamp(p.density, 0.0f, 1.0f);
    p.ancestry = std::clamp(p.ancestry, 0.0f, 1.0f);
    p.mutation = std::clamp(p.mutation, 0.0f, 1.0f);
    p.repeat = std::clamp(p.repeat, 0.0f, 1.0f);
    p.coherence = std::clamp(p.coherence, 0.0f, 1.0f);
    p.registerSemitones = std::clamp(p.registerSemitones, -36.0f, 36.0f);
    p.noteTracking = std::clamp(p.noteTracking, 0.0f, 1.0f);
    p.tone = std::clamp(p.tone, -1.0f, 1.0f);
    p.drive = std::clamp(p.drive, 0.0f, 1.0f);
    p.topology = static_cast<ErrantTopology>(std::min<uint32_t>(
        static_cast<uint32_t>(p.topology), 3u));
    p.width = std::clamp(p.width, 0.0f, 1.0f);
    p.seed = std::clamp<uint32_t>(p.seed, 1u, 65535u);
    p.velocitySensitivity = std::clamp(
        p.velocitySensitivity, 0.0f, 1.0f);
    p.outputGainDb = std::clamp(p.outputGainDb, -36.0f, 6.0f);
    p.keyRole = static_cast<ErrantKeyRole>(std::min<uint32_t>(
        static_cast<uint32_t>(p.keyRole), 2u));
    p.sub = std::clamp(p.sub, 0.0f, 1.0f);
    p.resonance = std::clamp(p.resonance, 0.0f, 1.0f);
    p.filterContour = std::clamp(p.filterContour, -1.0f, 1.0f);
    p.crosswire = std::clamp(p.crosswire, 0.0f, 1.0f);
    return p;
}

class ProcessorErrant {
public:
    static constexpr uint32_t kVoiceCount = 8u;

    void prepare(double sampleRate)
    {
        sampleRate_ = std::clamp(
            std::isfinite(sampleRate) ? sampleRate : 48000.0,
            8000.0, 768000.0);
        // Four seconds at rates through 192 kHz. At unusually high rates the
        // fixed ceiling remains a useful causal memory without unbounded RAM.
        const uint32_t memoryFrames = static_cast<uint32_t>(std::max(
            4096.0, std::min(sampleRate_, 192000.0) * 4.0));
        for (auto& voice : voices_) voice.prepare(sampleRate_, memoryFrames);
        lineageL_.assign(memoryFrames, 0.0f);
        lineageR_.assign(memoryFrames, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& voice : voices_) voice.reset();
        std::fill(lineageL_.begin(), lineageL_.end(), 0.0f);
        std::fill(lineageR_.begin(), lineageR_.end(), 0.0f);
        lineageWriteIndex_ = lineageValid_ = 0u;
        triggerSerial_ = 0u;
        lastNote_ = -1;
        lastInterval_ = 0;
        lineageGeneration_ = 0u;
        smoothedOutputGain_ = std::pow(
            10.0f, params_.outputGainDb / 20.0f);
        smoothedVoiceNormalization_ = 1.0f;
        dcInputL_ = dcOutputL_ = 0.0f;
        dcInputR_ = dcOutputR_ = 0.0f;
    }

    void setParams(const ProcessorErrantParams& params)
    {
        params_ = sanitizeProcessorErrantParams(params);
    }

    const ProcessorErrantParams& params() const { return params_; }

    void noteOn(int note, float velocity, int32_t noteId = -1,
        int16_t channel = -1)
    {
        note = std::clamp(note, 0, 127);
        velocity = std::clamp(velocity, 0.0f, 1.0f);
        Voice* selected = nullptr;
        for (auto& voice : voices_) {
            if (!voice.active) {
                selected = &voice;
                break;
            }
        }
        if (!selected) {
            selected = &voices_[0];
            for (auto& voice : voices_) {
                if (voice.serial < selected->serial) selected = &voice;
            }
        }
        const uint64_t serial = ++triggerSerial_;
        const int interval = lastNote_ >= 0 ? note - lastNote_ : 0;
        lastNote_ = note;
        lastInterval_ = interval;
        ++lineageGeneration_;
        const uint32_t seed = mixSeed(params_.seed
            ^ static_cast<uint32_t>(note * 0x9e37u)
            ^ static_cast<uint32_t>(serial)
            ^ static_cast<uint32_t>(serial >> 32u));
        selected->start(params_, note, velocity, noteId, channel, serial, seed,
            interval, lineageWriteIndex_, lineageValid_);
    }

    void noteOff(int note, int32_t noteId = -1, int16_t channel = -1)
    {
        for (auto& voice : voices_) {
            if (!voice.active) continue;
            const bool idMatches = noteId >= 0
                ? voice.noteId == noteId : voice.note == note;
            const bool channelMatches = channel < 0
                || voice.channel < 0 || voice.channel == channel;
            if (idMatches && channelMatches) voice.release(params_);
        }
    }

    void allNotesOff()
    {
        for (auto& voice : voices_) {
            if (voice.active) voice.release(params_);
        }
    }

    void processFrame(float& left, float& right)
    {
        double sumL = 0.0;
        double sumR = 0.0;
        uint32_t sounding = 0u;
        uint32_t processed = 0u;
        for (auto& voice : voices_) {
            if (!voice.active) continue;
            ++processed;
            float voiceL = 0.0f;
            float voiceR = 0.0f;
            voice.process(params_, lineageL_, lineageR_, voiceL, voiceR);
            sumL += voiceL;
            sumR += voiceR;
            if (voice.active) ++sounding;
        }

        const float targetNormalization = 1.0f / std::sqrt(
            static_cast<float>(std::max(1u, sounding)));
        const float normalizationCoefficient = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.012));
        smoothedVoiceNormalization_ += (targetNormalization
            - smoothedVoiceNormalization_) * normalizationCoefficient;
        sumL *= smoothedVoiceNormalization_;
        sumR *= smoothedVoiceNormalization_;
        if (processed > 0u && !lineageL_.empty()) {
            const float archiveScale = 1.0f / std::sqrt(
                static_cast<float>(std::max(1u, processed)));
            lineageL_[lineageWriteIndex_] = finite(
                static_cast<float>(sumL) * archiveScale);
            lineageR_[lineageWriteIndex_] = finite(
                static_cast<float>(sumR) * archiveScale);
            lineageWriteIndex_ = (lineageWriteIndex_ + 1u) % lineageL_.size();
            lineageValid_ = std::min<uint32_t>(
                lineageValid_ + 1u, lineageL_.size());
        }
        const float targetGain = std::pow(
            10.0f, params_.outputGainDb / 20.0f);
        const float gainCoefficient = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.012));
        smoothedOutputGain_ += (targetGain - smoothedOutputGain_)
            * gainCoefficient;
        float outL = static_cast<float>(sumL) * smoothedOutputGain_;
        float outR = static_cast<float>(sumR) * smoothedOutputGain_;

        // A causal DC blocker and gentle final knee contain polarity edits,
        // resonator buildup, and repeated descendants without adding latency.
        const float dcL = outL - dcInputL_ + 0.9975f * dcOutputL_;
        const float dcR = outR - dcInputR_ + 0.9975f * dcOutputR_;
        dcInputL_ = outL;
        dcInputR_ = outR;
        dcOutputL_ = finite(dcL);
        dcOutputR_ = finite(dcR);
        left = std::tanh(dcOutputL_ * 1.08f);
        right = std::tanh(dcOutputR_ * 1.08f);
    }

    bool active() const
    {
        for (const auto& voice : voices_) {
            if (voice.active) return true;
        }
        return false;
    }

    int lastNoteInterval() const { return lastInterval_; }
    uint32_t lineageGeneration() const { return lineageGeneration_; }
    uint32_t lineageFrames() const { return lineageValid_; }

private:
    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr float kTwoPi = 2.0f * kPi;

    static float finite(float value)
    {
        return std::isfinite(value) ? std::clamp(value, -8.0f, 8.0f) : 0.0f;
    }

    static uint32_t mixSeed(uint32_t x)
    {
        x ^= x >> 16u;
        x *= 0x7feb352du;
        x ^= x >> 15u;
        x *= 0x846ca68bu;
        x ^= x >> 16u;
        return x == 0u ? 0x6d2b79f5u : x;
    }

    struct Random {
        uint32_t state = 0x6d2b79f5u;

        void seed(uint32_t value) { state = mixSeed(value); }

        uint32_t nextU32()
        {
            uint32_t x = state;
            x ^= x << 13u;
            x ^= x >> 17u;
            x ^= x << 5u;
            state = x == 0u ? 0x6d2b79f5u : x;
            return state;
        }

        float uniform()
        {
            return static_cast<float>(nextU32() >> 8u)
                * (1.0f / 16777216.0f);
        }

        float bipolar() { return uniform() * 2.0f - 1.0f; }
    };

    struct Resonator {
        float a1 = 0.0f;
        float a2 = 0.0f;
        float gain = 0.0f;
        float y1 = 0.0f;
        float y2 = 0.0f;

        void configure(float frequency, float bandwidth, float sampleRate)
        {
            frequency = std::clamp(frequency, 20.0f, sampleRate * 0.43f);
            bandwidth = std::clamp(bandwidth, 20.0f, sampleRate * 0.2f);
            const float radius = std::exp(-kPi * bandwidth / sampleRate);
            a1 = 2.0f * radius * std::cos(kTwoPi * frequency / sampleRate);
            a2 = -radius * radius;
            gain = std::max(0.0001f, 1.0f - radius);
        }

        float process(float input)
        {
            const float output = finite(input * gain + a1 * y1 + a2 * y2);
            y2 = y1;
            y1 = output;
            return output;
        }

        void reset() { y1 = y2 = 0.0f; }
    };

    struct SaturatedLadder {
        float stage1 = 0.0f;
        float stage2 = 0.0f;
        float stage3 = 0.0f;
        float stage4 = 0.0f;

        float process(float input, float cutoff, float resonance,
            float drive, float sampleRate)
        {
            cutoff = std::clamp(cutoff, 24.0f, sampleRate * 0.42f);
            resonance = std::clamp(resonance, 0.0f, 0.96f);
            const float coefficient = std::clamp(1.0f - std::exp(
                -kTwoPi * cutoff * 1.52f / sampleRate), 0.0f, 0.86f);
            const float feedback = stage4 * resonance * 3.72f;
            const float driven = std::tanh(
                (input * (1.0f + drive * 7.0f) - feedback)
                    * (0.82f + drive * 0.48f));
            stage1 += coefficient * (driven - stage1);
            stage2 += coefficient * (std::tanh(stage1 * 1.12f) - stage2);
            stage3 += coefficient * (std::tanh(stage2 * 1.10f) - stage3);
            stage4 += coefficient * (std::tanh(stage3 * 1.08f) - stage4);
            stage1 = finite(stage1);
            stage2 = finite(stage2);
            stage3 = finite(stage3);
            stage4 = finite(stage4);
            return stage4 * (1.0f + resonance * 0.44f);
        }

        void reset() { stage1 = stage2 = stage3 = stage4 = 0.0f; }
    };

    enum class SegmentSource : uint8_t {
        Direct,
        Forward,
        Reverse,
        Loop,
    };

    enum class SegmentDamage : uint8_t {
        None,
        Prediction,
        Drop,
        Replace,
        Fold,
    };

    struct Voice {
        bool active = false;
        bool held = false;
        bool releasing = false;
        int note = 60;
        int32_t noteId = -1;
        int16_t channel = -1;
        uint64_t serial = 0u;
        uint64_t age = 0u;
        uint64_t phraseFrames = 0u;
        uint64_t releaseAge = 0u;
        uint64_t releaseFrames = 1u;
        float releaseStart = 1.0f;
        float velocityGain = 1.0f;
        float noteVelocity = 1.0f;
        float rootHz = 130.8128f;
        float clockRatio = 1.0f;
        float intervalRatio = 1.0f;
        float expressiveMutation = 0.0f;
        float relationFidelity = 1.0f;
        int intervalSemitones = 0;
        float sampleRate = 48000.0f;
        Random random {};

        std::vector<float> memoryL;
        std::vector<float> memoryR;
        uint32_t writeIndex = 0u;
        uint32_t memoryValid = 0u;
        uint32_t lineageSnapshotWrite = 0u;
        uint32_t lineageSnapshotValid = 0u;

        uint64_t segmentAge = 0u;
        uint64_t segmentFrames = 1u;
        uint64_t gapFrames = 0u;
        uint64_t attackFrames = 1u;
        uint64_t edgeFrames = 1u;
        SegmentSource segmentSource = SegmentSource::Direct;
        SegmentDamage segmentDamage = SegmentDamage::None;
        bool readSharedLineage = false;
        uint32_t readBase = 0u;
        float readPhase = 0.0f;
        float readRate = 1.0f;
        uint32_t readLength = 1u;
        float segmentGain = 1.0f;
        float segmentPan = 0.0f;
        float segmentPolarity = 1.0f;
        uint32_t ratchets = 1u;
        uint32_t rateHoldFrames = 1u;
        uint32_t rateHoldCounter = 0u;
        float heldL = 0.0f;
        float heldR = 0.0f;
        float slewedHeldL = 0.0f;
        float slewedHeldR = 0.0f;
        uint32_t quantizeBits = 16u;
        uint32_t damageBlockFrames = 1u;
        float lineageBlend = 0.0f;
        float predictionL = 0.0f;
        float predictionR = 0.0f;
        float damageGate = 1.0f;
        float damageReplace = 0.0f;

        float phaseBody = 0.0f;
        float phaseBody2 = 0.0f;
        float phasePulse = 0.0f;
        float phaseSub = 0.0f;
        float phaseWireL = 0.0f;
        float phaseWireR = 0.0f;
        float curveValue = 0.0f;
        float curveTarget = 0.0f;
        float curveStep = 0.0f;
        float curveDuration = 0.0f;
        float curveDurationStep = 0.0f;
        float pitchWalk = 0.0f;
        float pitchWalkStep = 0.0f;
        float crosswireState = 0.0f;
        float crosswireTarget = 0.0f;
        float cutoffState = 1200.0f;
        float resonanceState = 0.0f;
        float driveState = 0.0f;
        float antiClickL = 0.0f;
        float antiClickR = 0.0f;
        float lastOutputL = 0.0f;
        float lastOutputR = 0.0f;
        float stolenTailL = 0.0f;
        float stolenTailR = 0.0f;
        uint32_t stolenTailAge = 0u;
        uint32_t stolenTailFrames = 0u;
        uint32_t curveSegmentAge = 0u;
        uint32_t curveSegmentFrames = 1u;
        float dustLowL = 0.0f;
        float dustLowR = 0.0f;
        float bassState = 0.0f;
        std::array<Resonator, 3> throatL {};
        std::array<Resonator, 3> throatR {};
        SaturatedLadder ladderL {};
        SaturatedLadder ladderR {};

        void prepare(double sr, uint32_t memoryFrames)
        {
            sampleRate = static_cast<float>(sr);
            memoryL.assign(memoryFrames, 0.0f);
            memoryR.assign(memoryFrames, 0.0f);
            reset();
        }

        void reset()
        {
            active = held = releasing = false;
            note = 60;
            noteId = -1;
            channel = -1;
            serial = age = phraseFrames = releaseAge = 0u;
            releaseFrames = 1u;
            releaseStart = velocityGain = noteVelocity = 1.0f;
            rootHz = 130.8128f;
            clockRatio = intervalRatio = relationFidelity = 1.0f;
            expressiveMutation = 0.0f;
            intervalSemitones = 0;
            writeIndex = memoryValid = 0u;
            lineageSnapshotWrite = lineageSnapshotValid = 0u;
            segmentAge = 0u;
            segmentFrames = attackFrames = edgeFrames = 1u;
            gapFrames = 0u;
            segmentSource = SegmentSource::Direct;
            segmentDamage = SegmentDamage::None;
            readSharedLineage = false;
            readBase = 0u;
            readPhase = 0.0f;
            readRate = 1.0f;
            readLength = 1u;
            segmentGain = 1.0f;
            segmentPan = 0.0f;
            segmentPolarity = 1.0f;
            ratchets = rateHoldFrames = 1u;
            rateHoldCounter = 0u;
            heldL = heldR = slewedHeldL = slewedHeldR = 0.0f;
            quantizeBits = 16u;
            damageBlockFrames = 1u;
            lineageBlend = predictionL = predictionR = 0.0f;
            damageGate = 1.0f;
            damageReplace = 0.0f;
            phaseBody = phaseBody2 = phasePulse = 0.0f;
            phaseSub = 0.0f;
            phaseWireL = phaseWireR = 0.0f;
            curveValue = curveTarget = curveStep = 0.0f;
            curveDuration = curveDurationStep = 0.0f;
            pitchWalk = pitchWalkStep = 0.0f;
            crosswireState = crosswireTarget = 0.0f;
            cutoffState = 1200.0f;
            resonanceState = driveState = 0.0f;
            antiClickL = antiClickR = 0.0f;
            lastOutputL = lastOutputR = 0.0f;
            stolenTailL = stolenTailR = 0.0f;
            stolenTailAge = stolenTailFrames = 0u;
            curveSegmentAge = 0u;
            curveSegmentFrames = 1u;
            dustLowL = dustLowR = bassState = 0.0f;
            for (auto& resonator : throatL) resonator.reset();
            for (auto& resonator : throatR) resonator.reset();
            ladderL.reset();
            ladderR.reset();
        }

        void start(const ProcessorErrantParams& p, int midiNote,
            float midiVelocity, int32_t id, int16_t midiChannel,
            uint64_t voiceSerial, uint32_t seed, int noteInterval,
            uint32_t sharedWriteIndex, uint32_t sharedValid)
        {
            const bool stealing = active;
            const float carriedL = lastOutputL;
            const float carriedR = lastOutputR;
            active = true;
            held = true;
            releasing = false;
            note = midiNote;
            noteId = id;
            channel = midiChannel;
            serial = voiceSerial;
            age = releaseAge = 0u;
            releaseFrames = 1u;
            releaseStart = 1.0f;
            random.seed(seed);
            noteVelocity = midiVelocity;
            velocityGain = (1.0f - p.velocitySensitivity)
                + p.velocitySensitivity * std::sqrt(std::max(0.0f, midiVelocity));
            intervalSemitones = std::clamp(noteInterval, -48, 48);
            const int intervalClass = std::abs(intervalSemitones) % 12;
            relationFidelity = intervalSemitones == 0 ? 1.0f
                : ((intervalClass == 0 || intervalClass == 5
                        || intervalClass == 7) ? 0.86f
                    : (std::abs(intervalSemitones) <= 4 ? 0.72f : 0.46f));
            const float leap = std::clamp(
                (static_cast<float>(std::abs(intervalSemitones)) - 5.0f)
                    / 19.0f,
                0.0f, 1.0f);
            expressiveMutation = std::clamp(
                p.mutation * (0.18f + 0.82f * midiVelocity)
                    + leap * midiVelocity * 0.24f,
                0.0f, 1.0f);
            const bool pitchRole = p.keyRole == ErrantKeyRole::Pitch
                || p.keyRole == ErrantKeyRole::Both;
            const bool clockRole = p.keyRole == ErrantKeyRole::Clock
                || p.keyRole == ErrantKeyRole::Both;
            const float trackedSemitones = static_cast<float>(midiNote - 60)
                * p.noteTracking;
            const float semitones = p.registerSemitones
                + (pitchRole ? trackedSemitones : 0.0f);
            rootHz = 130.8128f * std::pow(2.0f, semitones / 12.0f);
            rootHz = std::clamp(rootHz, 18.0f, sampleRate * 0.18f);
            clockRatio = clockRole
                ? std::clamp(std::pow(2.0f, trackedSemitones / 12.0f),
                    0.125f, 8.0f)
                : 1.0f;
            intervalRatio = std::clamp(std::pow(2.0f,
                static_cast<float>(intervalSemitones) * p.noteTracking
                    / 12.0f),
                0.125f, 8.0f);
            const float seconds = p.mode == ErrantMode::Cell
                ? 0.05f * std::pow(30.0f, p.span)
                : (p.mode == ErrantMode::Phrase
                    ? 0.5f * std::pow(24.0f, p.span)
                    : std::numeric_limits<float>::infinity());
            phraseFrames = std::isfinite(seconds)
                ? static_cast<uint64_t>(std::max(1.0f,
                    seconds * sampleRate / clockRatio))
                : std::numeric_limits<uint64_t>::max();
            writeIndex = memoryValid = 0u;
            lineageSnapshotWrite = sharedWriteIndex;
            lineageSnapshotValid = sharedValid;
            segmentAge = segmentFrames = gapFrames = 0u;
            rateHoldCounter = 0u;
            phaseBody = phaseBody2 = phasePulse = phaseSub = random.uniform();
            phaseWireL = random.uniform();
            phaseWireR = phaseWireL + 0.017f * (1.0f - p.coherence);
            phaseWireR -= std::floor(phaseWireR);
            curveValue = random.bipolar() * 0.35f;
            curveTarget = random.bipolar() * 0.55f;
            curveStep = random.bipolar() * 0.035f;
            curveDuration = random.bipolar() * 0.18f;
            curveDurationStep = random.bipolar() * 0.018f;
            pitchWalk = pitchWalkStep = 0.0f;
            crosswireState = 0.0f;
            crosswireTarget = random.bipolar();
            curveSegmentAge = curveSegmentFrames = 0u;
            const float cutoffNormalized = std::clamp(
                (p.tone + 1.0f) * 0.5f, 0.0f, 1.0f);
            cutoffState = 45.0f * std::pow(220.0f, cutoffNormalized);
            resonanceState = p.resonance;
            driveState = p.drive;
            antiClickL = antiClickR = 0.0f;
            lastOutputL = lastOutputR = 0.0f;
            if (stealing) {
                stolenTailL = carriedL;
                stolenTailR = carriedR;
                stolenTailAge = 0u;
                stolenTailFrames = static_cast<uint32_t>(std::max(
                    1.0f, sampleRate * 0.008f));
            } else {
                stolenTailL = stolenTailR = 0.0f;
                stolenTailAge = stolenTailFrames = 0u;
            }
            damageGate = 1.0f;
            damageReplace = 0.0f;
            heldL = heldR = slewedHeldL = slewedHeldR = 0.0f;
            dustLowL = dustLowR = bassState = 0.0f;
            ladderL.reset();
            ladderR.reset();
            const float formantShift = std::pow(2.0f,
                semitones * 0.20f / 12.0f);
            const std::array<float, 3> formants {
                (520.0f + random.uniform() * 360.0f) * formantShift,
                (1180.0f + random.uniform() * 1120.0f) * formantShift,
                (2450.0f + random.uniform() * 1050.0f) * formantShift,
            };
            for (uint32_t i = 0u; i < formants.size(); ++i) {
                throatL[i].reset();
                throatR[i].reset();
                throatL[i].configure(formants[i], 90.0f + 150.0f * i,
                    sampleRate);
                throatR[i].configure(formants[i]
                    * (1.0f + (1.0f - p.coherence) * (0.006f + 0.004f * i)),
                    100.0f + 165.0f * i, sampleRate);
            }
            startSegment(p);
        }

        void release(const ProcessorErrantParams& p)
        {
            if (!active || releasing) return;
            releaseStart = globalEnvelope();
            held = false;
            releasing = true;
            releaseAge = 0u;
            const float seconds = p.mode == ErrantMode::Cell
                ? 0.055f : (p.mode == ErrantMode::Phrase
                    ? 0.18f + p.span * 0.42f
                    : 0.20f + p.span * 1.8f);
            releaseFrames = static_cast<uint64_t>(std::max(
                1.0f, seconds * sampleRate));
        }

        float globalEnvelope() const
        {
            if (!active) return 0.0f;
            if (releasing) {
                const float position = static_cast<float>(releaseAge)
                    / static_cast<float>(std::max<uint64_t>(1u, releaseFrames));
                return releaseStart * std::max(0.0f, 1.0f - position);
            }
            const uint64_t attack = static_cast<uint64_t>(sampleRate * 0.004f);
            return attack > 0u && age < attack
                ? static_cast<float>(age) / static_cast<float>(attack) : 1.0f;
        }

        static float materialWeight(float material, float center)
        {
            return std::max(0.0f, 1.0f - std::fabs(material - center) / 0.34f);
        }

        static void reflect(float& value, float& step, float limit)
        {
            if (value > limit) {
                value = limit - (value - limit);
                step = -std::fabs(step);
            } else if (value < -limit) {
                value = -limit + (-limit - value);
                step = std::fabs(step);
            }
            value = std::clamp(value, -limit, limit);
        }

        void advanceStochasticCurve(const ProcessorErrantParams& p,
            float frequency)
        {
            if (curveSegmentFrames > 0u) curveValue = curveTarget;
            const float walkEnergy = 0.008f + expressiveMutation * 0.052f;
            curveStep = std::clamp(curveStep
                    + random.bipolar() * walkEnergy,
                -0.24f, 0.24f);
            curveStep *= 0.982f;
            curveTarget += curveStep;
            reflect(curveTarget, curveStep, 0.96f);

            curveDurationStep = std::clamp(curveDurationStep
                    + random.bipolar() * (0.004f
                        + expressiveMutation * 0.026f),
                -0.095f, 0.095f);
            curveDurationStep *= 0.975f;
            curveDuration += curveDurationStep;
            reflect(curveDuration, curveDurationStep, 1.0f);

            pitchWalkStep = std::clamp(pitchWalkStep
                    + random.bipolar() * (0.025f
                        + expressiveMutation * 0.19f),
                -0.55f, 0.55f);
            pitchWalkStep *= 0.94f;
            pitchWalk += pitchWalkStep;
            pitchWalk -= pitchWalk * (0.035f + p.noteTracking * 0.24f);
            reflect(pitchWalk, pitchWalkStep, 12.0f);

            const float segmentSeconds = std::pow(2.0f,
                    curveDuration * (0.22f + expressiveMutation * 0.92f))
                / (std::max(12.0f, frequency) * 8.0f);
            curveSegmentFrames = static_cast<uint32_t>(std::clamp(
                segmentSeconds * sampleRate, 2.0f, sampleRate * 0.12f));
            curveSegmentAge = 0u;
        }

        float stochasticCurve(const ProcessorErrantParams& p,
            float frequency)
        {
            if (curveSegmentAge >= curveSegmentFrames) {
                advanceStochasticCurve(p, frequency);
            }
            const float x = static_cast<float>(curveSegmentAge)
                / static_cast<float>(std::max(1u, curveSegmentFrames));
            const float shaped = 0.5f - 0.5f * std::cos(kPi * x);
            ++curveSegmentAge;
            return curveValue + (curveTarget - curveValue) * shaped;
        }

        static float polyBlep(float phase, float increment)
        {
            increment = std::clamp(increment, 1.0e-6f, 0.49f);
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

        static float bandlimitedSaw(float phase, float increment)
        {
            return phase * 2.0f - 1.0f - polyBlep(phase, increment);
        }

        static float bandlimitedPulse(float phase, float increment,
            float width)
        {
            width = std::clamp(width, 0.08f, 0.92f);
            float value = phase < width ? 1.0f : -1.0f;
            value += polyBlep(phase, increment);
            float shifted = phase - width;
            if (shifted < 0.0f) shifted += 1.0f;
            value -= polyBlep(shifted, increment);
            return value;
        }

        void generateRoot(const ProcessorErrantParams& p,
            float& bassCore, float& lowAnchor, float& left, float& right)
        {
            const float seconds = static_cast<float>(age) / sampleRate;
            const float crosswireCoefficient = 1.0f - std::exp(
                -1.0f / (sampleRate * 0.032f));
            crosswireState += (crosswireTarget - crosswireState)
                * crosswireCoefficient;
            // Keep the oscillator pitch recognizably attached to the MIDI
            // note. The short knock gives the voice a techno-bass transient;
            // genealogy and Crosswire are allowed to disturb the upper body,
            // not replace the fundamental with a pitch error.
            const float sweep = 0.025f + p.drive * 0.12f
                + p.material * 0.055f;
            const float walkSemitones = pitchWalk * (1.0f - p.noteTracking);
            const float walkedRoot = rootHz * std::pow(
                2.0f, walkSemitones / 12.0f);
            const float pitch = std::min(sampleRate * 0.42f,
                walkedRoot * std::pow(2.0f, sweep * std::exp(
                    -seconds / (0.025f + p.span * 0.28f))));
            const float curve = stochasticCurve(p, pitch);
            const float increment1 = std::min(0.42f, pitch / sampleRate);
            const float oscillator2Ratio = 1.004f + p.crosswire
                * crosswireState * 0.019f;
            const float increment2 = std::min(0.42f,
                pitch * oscillator2Ratio / sampleRate);
            const float subIncrement = std::min(0.24f,
                pitch * 0.5f / sampleRate);
            phaseBody += increment1;
            phaseBody -= std::floor(phaseBody);
            phaseBody2 += increment2;
            phaseBody2 -= std::floor(phaseBody2);
            phaseSub += subIncrement;
            phaseSub -= std::floor(phaseSub);
            const float pulseWidth = 0.50f + p.crosswire
                * crosswireState * 0.21f;
            const float oscillator1 = bandlimitedSaw(phaseBody, increment1);
            const float hardPulse = bandlimitedPulse(
                phaseBody2, increment2, pulseWidth);
            const float pulseOffset = (0.5f - pulseWidth) * 1.35f;
            const float pulsePressure = 1.4f + p.material * 1.8f;
            const float roundedPulse = (std::tanh((
                    std::sin(kTwoPi * phaseBody2) + pulseOffset)
                    * pulsePressure)
                - std::tanh(pulseOffset * pulsePressure))
                / std::max(0.001f, std::tanh(pulsePressure));
            const float oscillator2 = hardPulse * 0.18f
                + roundedPulse * 0.82f;
            const float subOscillator = 0.12f * bandlimitedPulse(
                    phaseSub, subIncrement, 0.5f)
                + 0.88f * std::sin(kTwoPi * phaseSub);
            const float triangle = 1.0f
                - 4.0f * std::fabs(phaseBody - 0.5f);
            bassCore = oscillator1 * 0.50f
                + oscillator2 * 0.25f
                + triangle * 0.13f
                + subOscillator * p.sub * 0.42f;
            lowAnchor = subOscillator;

            // MATERIAL now behaves as GROWL: a continuous upper body built
            // from the same oscillators, rather than a selector that could
            // crossfade the bass completely into dust or wire noise.
            const float pressure = 1.35f + p.material * 6.2f
                + p.drive * 2.8f;
            const float drivenBody = std::tanh(
                (oscillator1 * 0.72f + oscillator2 * 0.42f
                    + triangle * 0.18f) * pressure);
            const float crossProduct = oscillator1 * oscillator2;
            const float analogGrowl = drivenBody * 0.72f
                + crossProduct * (0.18f + p.material * 0.28f)
                + curve * (0.035f + p.crosswire * 0.075f);

            const float dustCutoff = 1100.0f * std::pow(
                12.0f, 0.18f + p.material * 0.48f);
            const float dustCoef = 1.0f - std::exp(
                -kTwoPi * std::min(dustCutoff, sampleRate * 0.42f)
                / sampleRate);
            const float sharedNoise = random.bipolar();
            const float noiseL = sharedNoise * p.coherence
                + random.bipolar() * (1.0f - p.coherence);
            const float noiseR = sharedNoise * p.coherence
                + random.bipolar() * (1.0f - p.coherence);
            dustLowL += dustCoef * (noiseL - dustLowL);
            dustLowR += dustCoef * (noiseR - dustLowR);
            const float dustL = noiseL - dustLowL * 0.72f;
            const float dustR = noiseR - dustLowR * 0.72f;

            phasePulse += std::min(sampleRate * 0.40f,
                pitch * (0.96f + 0.08f * p.coherence)) / sampleRate;
            phasePulse -= std::floor(phasePulse);
            const float throatCarrier = std::sin(kTwoPi * phasePulse);
            const float throatExciterL = analogGrowl * 0.42f
                + throatCarrier * 0.24f + dustLowL * 0.018f;
            const float throatExciterR = analogGrowl * 0.42f
                + throatCarrier * 0.24f + dustLowR * 0.018f;
            float throatOutL = 0.0f;
            float throatOutR = 0.0f;
            for (uint32_t i = 0u; i < throatL.size(); ++i) {
                const float weight = i == 0u ? 1.0f : (i == 1u ? 0.72f : 0.48f);
                throatOutL += throatL[i].process(throatExciterL) * weight;
                throatOutR += throatR[i].process(throatExciterR) * weight;
            }

            const float wireHz = std::min(sampleRate * 0.38f,
                walkedRoot * (2.0f + 2.8f * p.material));
            phaseWireL += wireHz / sampleRate;
            phaseWireR += wireHz * (1.0f
                + (1.0f - p.coherence) * 0.0085f) / sampleRate;
            phaseWireL -= std::floor(phaseWireL);
            phaseWireR -= std::floor(phaseWireR);
            const float wireMod = 0.65f + expressiveMutation * 2.4f
                + p.crosswire * 2.2f;
            const float wireL = std::sin(kTwoPi * phaseWireL
                + wireMod * std::sin(kTwoPi * phaseBody * 1.71f));
            const float wireR = std::sin(kTwoPi * phaseWireR
                + wireMod * std::sin(kTwoPi * phaseBody * 1.73f));

            const float growl = 0.12f + p.material * 0.88f;
            const float throatMix = growl * (0.08f + p.material * 0.22f);
            const float faultMix = p.crosswire * (0.035f
                + p.material * 0.16f);
            const float dustMix = faultMix * (0.14f
                + expressiveMutation * 0.24f);
            left = analogGrowl * (0.22f + growl * 0.42f)
                + throatOutL * throatMix * 1.35f
                + wireL * faultMix
                + dustL * dustMix;
            right = analogGrowl * (0.22f + growl * 0.42f)
                + throatOutR * throatMix * 1.35f
                + wireR * faultMix
                + dustR * dustMix;
        }

        float readMemory(const std::vector<float>& memory, float offset,
            uint32_t available) const
        {
            if (memory.empty() || available == 0u) return 0.0f;
            const float size = static_cast<float>(memory.size());
            float position = static_cast<float>(readBase) + offset;
            position -= std::floor(position / size) * size;
            const uint32_t a = static_cast<uint32_t>(position) % memory.size();
            const uint32_t b = (a + 1u) % memory.size();
            const float fraction = position - std::floor(position);
            return memory[a] + (memory[b] - memory[a]) * fraction;
        }

        void startSegment(const ProcessorErrantParams& p)
        {
            segmentAge = 0u;
            const float randomScale = 0.55f + random.uniform() * 1.25f;
            float seconds = 0.04f;
            if (p.mode == ErrantMode::Cell) {
                seconds = (0.006f + 0.16f * p.span)
                    * (1.2f - 0.72f * p.density) * randomScale;
            } else if (p.mode == ErrantMode::Phrase) {
                seconds = (0.028f + 0.82f * p.span)
                    * (1.25f - 0.82f * p.density) * randomScale;
            } else {
                seconds = (0.07f + 3.2f * p.span)
                    * (1.30f - 0.78f * p.density) * randomScale;
            }
            seconds /= clockRatio;
            segmentFrames = static_cast<uint64_t>(std::max(
                8.0f, seconds * sampleRate));
            const float fadeCeiling = std::max(4.0f,
                static_cast<float>(segmentFrames) * 0.46f);
            const float minimumFade = std::min(
                sampleRate * 0.0040f, fadeCeiling);
            attackFrames = static_cast<uint64_t>(std::clamp(
                static_cast<float>(segmentFrames)
                    * (0.025f + random.uniform() * 0.12f),
                minimumFade, std::min(sampleRate * 0.060f, fadeCeiling)));
            edgeFrames = static_cast<uint64_t>(std::clamp(
                static_cast<float>(segmentFrames)
                    * (0.055f + random.uniform() * 0.18f),
                minimumFade, std::min(sampleRate * 0.110f, fadeCeiling)));
            segmentGain = 0.58f + random.uniform() * 0.52f;
            segmentPan = random.bipolar();
            crosswireTarget = random.bipolar()
                * (0.55f + expressiveMutation * 0.45f);
            segmentPolarity = random.uniform() < expressiveMutation * 0.22f
                ? -1.0f : 1.0f;
            ratchets = 1u + static_cast<uint32_t>(
                std::floor(p.repeat * p.repeat
                    * (5.0f + expressiveMutation * 6.0f) * random.uniform()));
            quantizeBits = 16u - static_cast<uint32_t>(std::floor(
                expressiveMutation * expressiveMutation
                    * random.uniform() * 12.0f));
            quantizeBits = std::clamp<uint32_t>(quantizeBits, 4u, 16u);
            rateHoldFrames = 1u + static_cast<uint32_t>(std::floor(
                expressiveMutation * expressiveMutation
                    * random.uniform() * 72.0f));
            rateHoldCounter = 0u;
            damageBlockFrames = std::max<uint32_t>(4u,
                static_cast<uint32_t>(sampleRate
                    * (0.0015f + random.uniform() * 0.026f)));
            segmentDamage = SegmentDamage::None;
            if (random.uniform() < expressiveMutation * 0.78f) {
                const uint32_t selection = std::min<uint32_t>(3u,
                    static_cast<uint32_t>(random.uniform() * 4.0f));
                segmentDamage = static_cast<SegmentDamage>(selection + 1u);
            }
            predictionL = predictionR = 0.0f;
            lineageBlend = 0.0f;
            readSharedLineage = false;

            const bool mayReadLocal = memoryValid > static_cast<uint32_t>(
                sampleRate * 0.012f);
            const bool mayReadShared = lineageSnapshotValid
                    > static_cast<uint32_t>(sampleRate * 0.012f)
                && age < memoryL.size();
            const float velocityInheritance = 1.0f - noteVelocity * 0.48f;
            const float inheritanceChance = std::clamp(p.ancestry
                    * (0.58f + velocityInheritance * 0.42f)
                    * (0.58f + relationFidelity * 0.42f)
                    + p.repeat * (intervalSemitones == 0 ? 0.22f : 0.0f),
                0.0f, 1.0f);
            const bool descendant = (mayReadLocal || mayReadShared)
                && random.uniform() < inheritanceChance;
            if (!descendant) {
                segmentSource = SegmentSource::Direct;
                readLength = 1u;
                readRate = 1.0f;
                return;
            }

            readSharedLineage = mayReadShared && (!mayReadLocal
                || random.uniform() < (0.42f + relationFidelity * 0.46f));
            const uint32_t available = readSharedLineage
                ? lineageSnapshotValid : memoryValid;
            lineageBlend = readSharedLineage ? std::clamp(
                p.ancestry * (0.52f + 0.38f * relationFidelity)
                    * (1.12f - noteVelocity * 0.34f),
                0.18f, 0.96f) : 1.0f;

            const float selector = random.uniform();
            segmentSource = selector < 0.40f ? SegmentSource::Forward
                : (selector < 0.67f ? SegmentSource::Reverse
                : SegmentSource::Loop);
            const uint32_t minimum = std::max<uint32_t>(8u,
                static_cast<uint32_t>(sampleRate * 0.004f));
            const uint32_t maximum = std::max(minimum,
                std::min<uint32_t>(available - 1u,
                    static_cast<uint32_t>(sampleRate
                        * (0.08f + 1.4f * p.span))));
            const float lengthShape = std::pow(random.uniform(),
                1.8f - 1.25f * p.repeat);
            readLength = minimum + static_cast<uint32_t>(
                lengthShape * static_cast<float>(maximum - minimum));
            const uint32_t availableLag = std::max<uint32_t>(
                readLength + 2u, std::min<uint32_t>(available - 1u,
                    static_cast<uint32_t>(sampleRate
                        * (0.04f + 2.8f * p.span))));
            const uint32_t lag = std::min<uint32_t>(available - 1u,
                readLength + 1u + static_cast<uint32_t>(random.uniform()
                    * static_cast<float>(std::max<uint32_t>(
                        1u, availableLag - readLength - 1u))));
            const uint32_t sourceWrite = readSharedLineage
                ? lineageSnapshotWrite : writeIndex;
            readBase = (sourceWrite + memoryL.size() - lag) % memoryL.size();
            readPhase = segmentSource == SegmentSource::Reverse
                ? static_cast<float>(readLength - 1u) : 0.0f;
            const float octave = expressiveMutation * random.bipolar() * 1.6f;
            readRate = std::pow(2.0f, octave);
            if (readSharedLineage) {
                if (p.keyRole == ErrantKeyRole::Pitch
                    || p.keyRole == ErrantKeyRole::Both) {
                    readRate *= intervalRatio;
                } else if (p.keyRole == ErrantKeyRole::Clock) {
                    readRate *= clockRatio;
                }
                readRate = std::clamp(readRate, 0.0625f, 16.0f);
            }
            if (segmentSource == SegmentSource::Reverse) readRate = -readRate;
        }

        float segmentEnvelope() const
        {
            if (segmentFrames == 0u) return 0.0f;
            float envelope = 1.0f;
            if (segmentAge < attackFrames) {
                const float x = static_cast<float>(segmentAge)
                    / static_cast<float>(std::max<uint64_t>(1u, attackFrames));
                envelope *= 0.5f - 0.5f * std::cos(kPi * x);
            }
            const uint64_t remaining = segmentFrames > segmentAge
                ? segmentFrames - segmentAge : 0u;
            if (remaining < edgeFrames) {
                const float x = static_cast<float>(remaining)
                    / static_cast<float>(std::max<uint64_t>(1u, edgeFrames));
                envelope *= 0.5f - 0.5f * std::cos(kPi * x);
            }
            if (ratchets > 1u) {
                const double phase = std::fmod(
                    static_cast<double>(segmentAge) * ratchets
                        / static_cast<double>(std::max<uint64_t>(1u, segmentFrames)),
                    1.0);
                const float pulse = std::sin(kPi * static_cast<float>(phase));
                envelope *= std::pow(std::max(0.0f, pulse), 1.35f);
            }
            return envelope;
        }

        static float quantize(float sample, uint32_t bits)
        {
            if (bits >= 16u) return sample;
            const float steps = static_cast<float>((1u << (bits - 1u)) - 1u);
            return std::round(std::clamp(sample, -1.5f, 1.5f) * steps) / steps;
        }

        void applySegmentDamage(float rootL, float rootR,
            float& eventL, float& eventR)
        {
            const float amount = expressiveMutation;
            const uint64_t block = segmentAge
                / std::max<uint32_t>(1u, damageBlockFrames);
            float gateTarget = 1.0f;
            float replaceTarget = 0.0f;
            switch (segmentDamage) {
            case SegmentDamage::Prediction: {
                const float errorL = eventL - predictionL;
                const float errorR = eventR - predictionR;
                predictionL += (eventL - predictionL) * 0.34f;
                predictionR += (eventR - predictionR) * 0.34f;
                eventL += (errorL * 1.7f - eventL) * amount;
                eventR += (errorR * 1.7f - eventR) * amount;
                break;
            }
            case SegmentDamage::Drop:
                gateTarget = (block + serial) % 4u == 3u ? 0.0f : 1.0f;
                break;
            case SegmentDamage::Replace:
                replaceTarget = (block + serial) % 3u == 2u ? amount : 0.0f;
                break;
            case SegmentDamage::Fold: {
                const float folds = 1.0f + amount * 7.0f;
                eventL = std::sin(eventL * folds);
                eventR = std::sin(eventR * folds);
                break;
            }
            case SegmentDamage::None:
            default:
                break;
            }
            const float transition = 1.0f - std::exp(
                -1.0f / (sampleRate * 0.0060f));
            damageGate += (gateTarget - damageGate) * transition;
            damageReplace += (replaceTarget - damageReplace) * transition;
            const float replacementL = rootR * segmentPolarity;
            const float replacementR = rootL;
            eventL += (replacementL - eventL) * damageReplace;
            eventR += (replacementR - eventR) * damageReplace;
            eventL *= damageGate;
            eventR *= damageGate;
        }

        void process(const ProcessorErrantParams& p,
            const std::vector<float>& sharedL,
            const std::vector<float>& sharedR,
            float& left, float& right)
        {
            if (!active || memoryL.empty()) {
                left = right = 0.0f;
                return;
            }
            if (!releasing && phraseFrames != std::numeric_limits<uint64_t>::max()
                && age >= phraseFrames) {
                release(p);
            }
            if (gapFrames > 0u) --gapFrames;
            if (segmentAge >= segmentFrames && gapFrames == 0u) {
                startSegment(p);
            }

            float bassCore = 0.0f;
            float lowAnchor = 0.0f;
            float rootL = 0.0f;
            float rootR = 0.0f;
            generateRoot(p, bassCore, lowAnchor, rootL, rootR);
            float eventL = rootL;
            float eventR = rootR;
            const uint32_t sourceAvailable = readSharedLineage
                ? lineageSnapshotValid : memoryValid;
            if (segmentSource != SegmentSource::Direct
                && sourceAvailable > 1u
                && (!readSharedLineage || age < memoryL.size())) {
                const auto& sourceL = readSharedLineage ? sharedL : memoryL;
                const auto& sourceR = readSharedLineage ? sharedR : memoryR;
                const float length = static_cast<float>(
                    std::max(1u, readLength));
                const float decorrelatedOffset = (1.0f - p.coherence)
                    * std::min<float>(static_cast<float>(readLength) * 0.17f,
                        sampleRate * 0.021f);
                float rightPhase = std::fmod(
                    readPhase + decorrelatedOffset, length);
                if (rightPhase < 0.0f) rightPhase += length;
                float inheritedL = readMemory(
                    sourceL, readPhase, sourceAvailable);
                float inheritedR = readMemory(
                    sourceR, rightPhase, sourceAvailable);
                const float wrapFade = std::max(2.0f, std::min(
                    length * 0.22f, sampleRate * 0.0080f));
                if (readRate >= 0.0f && readPhase > length - wrapFade) {
                    const float blend = std::clamp(
                        (readPhase - (length - wrapFade)) / wrapFade,
                        0.0f, 1.0f);
                    const float alternatePhase = readPhase
                        - (length - wrapFade);
                    float alternateRight = std::fmod(
                        alternatePhase + decorrelatedOffset, length);
                    if (alternateRight < 0.0f) alternateRight += length;
                    inheritedL += (readMemory(sourceL, alternatePhase,
                        sourceAvailable) - inheritedL) * blend;
                    inheritedR += (readMemory(sourceR, alternateRight,
                        sourceAvailable) - inheritedR) * blend;
                } else if (readRate < 0.0f && readPhase < wrapFade) {
                    const float blend = std::clamp(
                        (wrapFade - readPhase) / wrapFade, 0.0f, 1.0f);
                    const float alternatePhase = length - wrapFade + readPhase;
                    float alternateRight = std::fmod(
                        alternatePhase + decorrelatedOffset, length);
                    if (alternateRight < 0.0f) alternateRight += length;
                    inheritedL += (readMemory(sourceL, alternatePhase,
                        sourceAvailable) - inheritedL) * blend;
                    inheritedR += (readMemory(sourceR, alternateRight,
                        sourceAvailable) - inheritedR) * blend;
                }
                eventL += (inheritedL - eventL) * lineageBlend;
                eventR += (inheritedR - eventR) * lineageBlend;
                readPhase += readRate;
                while (readPhase >= length) readPhase -= length;
                while (readPhase < 0.0f) readPhase += length;
            }

            applySegmentDamage(rootL, rootR, eventL, eventR);

            if (rateHoldCounter == 0u) {
                heldL = quantize(eventL, quantizeBits);
                heldR = quantize(eventR, quantizeBits);
                rateHoldCounter = rateHoldFrames;
            }
            --rateHoldCounter;
            const bool steppedMutation = rateHoldFrames > 1u
                || quantizeBits < 16u || segmentDamage != SegmentDamage::None;
            const float heldCoefficient = steppedMutation
                ? 1.0f - std::exp(-1.0f / (sampleRate
                    * (0.0018f + expressiveMutation * 0.0038f)))
                : 1.0f;
            slewedHeldL += (heldL - slewedHeldL) * heldCoefficient;
            slewedHeldR += (heldR - slewedHeldR) * heldCoefficient;
            eventL = slewedHeldL;
            eventR = slewedHeldR;

            const float eventEnvelopeValue = gapFrames == 0u
                ? segmentEnvelope() * segmentGain : 0.0f;
            eventL *= eventEnvelopeValue * segmentPolarity;
            eventR *= eventEnvelopeValue;

            // The family archive is an upper-body mutation layer around a
            // continuous oscillator spine. Even a dropped, reversed, or
            // heavily quantized descendant therefore remains a bass note.
            const float genealogyGain = std::clamp(0.10f
                    + p.material * 0.24f + p.ancestry * 0.18f
                    + expressiveMutation * 0.18f + p.crosswire * 0.12f,
                0.10f, 0.72f);
            const float transientBody = 1.0f + p.drive * 0.16f
                * std::exp(-static_cast<float>(age)
                    / std::max(1.0f, sampleRate * 0.026f));
            const float spine = bassCore * transientBody * 0.78f;
            eventL = spine + eventL * genealogyGain;
            eventR = spine + eventR * genealogyGain;

            const float mono = 0.5f * (eventL + eventR);
            const float bassCoef = 1.0f - std::exp(
                -kTwoPi * 125.0f / sampleRate);
            bassState += bassCoef * (mono - bassState);
            const float highL = eventL - bassState;
            const float highR = eventR - bassState;
            const float highMid = 0.5f * (highL + highR);
            const float highSide = 0.5f * (highL - highR);
            switch (p.topology) {
            case ErrantTopology::Spine:
                left = bassState + highMid + highSide * p.width * 0.22f;
                right = bassState + highMid - highSide * p.width * 0.22f;
                break;
            case ErrantTopology::Wings: {
                const float side = highSide + highMid * segmentPan
                    * (1.0f - p.coherence) * 0.52f;
                left = bassState + highMid + side * p.width * 1.35f;
                right = bassState + highMid - side * p.width * 1.35f;
                break;
            }
            case ErrantTopology::Exchange: {
                const float pan = std::clamp(segmentPan * p.width, -1.0f, 1.0f);
                const float gainL = std::sqrt(0.5f * (1.0f - pan));
                const float gainR = std::sqrt(0.5f * (1.0f + pan));
                left = bassState + (highMid + highSide) * gainL * 1.4142f;
                right = bassState + (highMid - highSide) * gainR * 1.4142f;
                break;
            }
            case ErrantTopology::Side: {
                const float side = highSide * 0.55f
                    + highMid * segmentPolarity * 0.85f;
                left = bassState + highMid * (1.0f - p.width) + side * p.width;
                right = bassState + highMid * (1.0f - p.width) - side * p.width;
                break;
            }
            }

            const float cutoffNormalized = std::clamp(
                (p.tone + 1.0f) * 0.5f, 0.0f, 1.0f);
            const float baseCutoff = 45.0f * std::pow(
                220.0f, cutoffNormalized);
            const float filterEnvelope = std::exp(
                -static_cast<float>(age)
                    / (sampleRate * (0.035f + p.span * 0.82f)));
            const float contourOctaves = p.filterContour * 4.2f
                * filterEnvelope;
            const float crosswireOctaves = crosswireState * p.crosswire * 2.1f;
            const float keyFollow = std::pow(std::max(0.125f,
                rootHz / 130.8128f), 0.28f);
            const float cutoffTarget = std::clamp(baseCutoff * keyFollow
                    * std::pow(2.0f, contourOctaves + crosswireOctaves),
                24.0f, sampleRate * 0.42f);
            const float controlCoefficient = 1.0f - std::exp(
                -1.0f / (sampleRate * 0.014f));
            cutoffState += (cutoffTarget - cutoffState) * controlCoefficient;
            const float resonanceTarget = std::clamp(p.resonance
                    + crosswireState * p.crosswire * 0.17f,
                0.0f, 0.94f);
            resonanceState += (resonanceTarget - resonanceState)
                * controlCoefficient;
            driveState += (p.drive - driveState) * controlCoefficient;
            left = ladderL.process(left, cutoffState, resonanceState,
                driveState, sampleRate);
            const float filterStereoOffset = p.topology == ErrantTopology::Spine
                ? 0.0f : (1.0f - p.coherence) * p.width * 0.006f;
            right = ladderR.process(right, cutoffState
                    * (1.0f + filterStereoOffset),
                resonanceState, driveState, sampleRate);
            const float postDrive = 1.0f + driveState * 1.6f;
            const float postNorm = std::max(0.001f, std::tanh(postDrive));
            left = std::tanh(left * postDrive) / postNorm;
            right = std::tanh(right * postDrive) / postNorm;
            // A restrained post-filter sub path mirrors Fault's protected low
            // anchor: high resonance and Crosswire can chew up the body while
            // the octave below remains clear, centered, and pitch-stable.
            const float protectedSub = lowAnchor * p.sub
                * (0.10f + 0.10f * (1.0f - p.drive));
            left = std::tanh(left + protectedSub);
            right = std::tanh(right + protectedSub);
            const float slewLimit = 0.022f + 0.075f * std::sqrt(std::clamp(
                cutoffState / (sampleRate * 0.42f), 0.0f, 1.0f));
            antiClickL += std::clamp(left - antiClickL,
                -slewLimit, slewLimit);
            antiClickR += std::clamp(right - antiClickR,
                -slewLimit, slewLimit);
            left = antiClickL;
            right = antiClickR;

            const float global = globalEnvelope() * velocityGain * 0.76f;
            left *= global;
            right *= global;
            if (stolenTailAge < stolenTailFrames) {
                const float x = static_cast<float>(stolenTailAge)
                    / static_cast<float>(std::max(1u, stolenTailFrames));
                const float tail = (1.0f - x) * (1.0f - x);
                left += stolenTailL * tail;
                right += stolenTailR * tail;
                ++stolenTailAge;
            }
            left = finite(left);
            right = finite(right);
            lastOutputL = left;
            lastOutputR = right;

            // Store the audible descendant alongside some live source. Later
            // segments therefore inherit earlier edits without forming an
            // instantaneous algebraic feedback path.
            const float inherit = p.ancestry * 0.72f;
            const float freshL = bassCore * 0.72f + rootL;
            const float freshR = bassCore * 0.72f + rootR;
            memoryL[writeIndex] = finite(
                freshL * (1.0f - inherit) + left * inherit);
            memoryR[writeIndex] = finite(
                freshR * (1.0f - inherit) + right * inherit);
            writeIndex = (writeIndex + 1u) % memoryL.size();
            memoryValid = std::min<uint32_t>(memoryValid + 1u, memoryL.size());

            ++segmentAge;
            if (segmentAge >= segmentFrames && gapFrames == 0u) {
                const float gapSeconds = (0.001f + 0.38f * (1.0f - p.density))
                    * (0.35f + random.uniform() * 1.5f)
                    * (p.mode == ErrantMode::Field ? 2.0f : 1.0f)
                    / clockRatio;
                gapFrames = static_cast<uint64_t>(gapSeconds * sampleRate);
            }
            ++age;
            if (releasing) {
                ++releaseAge;
                if (releaseAge >= releaseFrames) active = false;
            }
        }
    };

    double sampleRate_ = 48000.0;
    ProcessorErrantParams params_ {};
    std::array<Voice, kVoiceCount> voices_ {};
    std::vector<float> lineageL_;
    std::vector<float> lineageR_;
    uint32_t lineageWriteIndex_ = 0u;
    uint32_t lineageValid_ = 0u;
    uint64_t triggerSerial_ = 0u;
    int lastNote_ = -1;
    int lastInterval_ = 0;
    uint32_t lineageGeneration_ = 0u;
    float smoothedOutputGain_ = 0.398107f;
    float smoothedVoiceNormalization_ = 1.0f;
    float dcInputL_ = 0.0f;
    float dcOutputL_ = 0.0f;
    float dcInputR_ = 0.0f;
    float dcOutputR_ = 0.0f;
};

} // namespace s3g
