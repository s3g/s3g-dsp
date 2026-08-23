#include "s3g_sample_motion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {

using namespace s3g::sample;

void require(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "sample motion smoke failed: " << message << '\n';
    std::exit(1);
}

std::shared_ptr<SampleAsset> makeAsset()
{
    constexpr uint32_t frames = 48000u;
    constexpr double rate = 48000.0;
    constexpr double pi = 3.14159265358979323846;
    auto asset = std::make_shared<SampleAsset>();
    asset->sampleRate = rate;
    asset->channelCount = 2u;
    asset->channels[0u].resize(frames);
    asset->channels[1u].resize(frames);
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        const double time = static_cast<double>(frame) / rate;
        const double contour = 0.3 + 0.7 * static_cast<double>(frame)
            / static_cast<double>(frames - 1u);
        asset->channels[0u][frame] = static_cast<float>(contour
            * std::sin(2.0 * pi * 137.0 * time));
        asset->channels[1u][frame] = static_cast<float>(0.8 * contour
            * std::sin(2.0 * pi * 211.0 * time + 0.27));
    }
    require(asset->valid(), "fixture validity");
    return asset;
}

std::shared_ptr<SampleAsset> makeConstantAsset(float value = 0.75f)
{
    auto asset = std::make_shared<SampleAsset>();
    asset->sampleRate = 48000.0;
    asset->channelCount = 2u;
    asset->channels[0u].assign(48000u, value);
    asset->channels[1u].assign(48000u, value * 0.8f);
    require(asset->valid(), "constant fixture validity");
    return asset;
}

float peak(const std::vector<float>& samples)
{
    float result = 0.0f;
    for (float sample : samples) result = std::max(result, std::abs(sample));
    return result;
}

double difference(const std::vector<float>& left,
    const std::vector<float>& right)
{
    double result = 0.0;
    for (std::size_t index = 0u; index < left.size(); ++index)
        result += std::abs(left[index] - right[index]);
    return result;
}

std::array<std::vector<float>, 2u> renderMode(MotionMode motion,
    MotionArticulation articulation, uint32_t seed = 17u)
{
    auto asset = makeAsset();
    SampleMotionEngine engine;
    require(engine.prepare(48000.0), "prepare");
    engine.setAsset(asset.get());
    MotionSettings settings;
    settings.motion = motion;
    settings.articulation = articulation;
    settings.start = 0.1;
    settings.end = 0.9;
    settings.locus = 0.52;
    settings.field = 0.42;
    settings.motionRate = 3.25f;
    settings.rateBasis = MotionRateBasis::Hertz;
    settings.innerRateHz = 37.0f;
    settings.outerRateHz = 3.0f;
    settings.packetDuty = 0.55f;
    settings.attackSeconds = 0.0f;
    settings.releaseSeconds = 0.001f;
    settings.seed = seed;
    MotionRenderEvent note;
    note.kind = MotionEventKind::NoteOn;
    note.noteId = 9u;
    note.key = 60u;
    std::array<std::vector<float>, 2u> result;
    result[0u].resize(24000u);
    result[1u].resize(24000u);
    engine.render(settings, &note, 1u, result[0u].data(),
        result[1u].data(), static_cast<uint32_t>(result[0u].size()));
    require(engine.voiceCursorCount() == 1u, "cursor publication");
    const auto& cursor = engine.voiceCursors()[0u];
    require(cursor.sourcePositionNormalized >= cursor.fieldLowNormalized
            && cursor.sourcePositionNormalized <= cursor.fieldHighNormalized,
        "cursor inside motion field");
    return result;
}

} // namespace

int main()
{
    MotionSettings invalid;
    invalid.field = 0.0;
    require(!invalid.valid(), "invalid settings rejected");

    const auto hover = renderMode(MotionMode::Hover,
        MotionArticulation::Continuous);
    const auto mirror = renderMode(MotionMode::Mirror,
        MotionArticulation::Continuous);
    const auto motor = renderMode(MotionMode::Hover,
        MotionArticulation::Motor);
    const auto drunkA = renderMode(MotionMode::Drunk,
        MotionArticulation::Continuous, 451u);
    const auto drunkB = renderMode(MotionMode::Drunk,
        MotionArticulation::Continuous, 451u);
    const auto drunkC = renderMode(MotionMode::Drunk,
        MotionArticulation::Continuous, 452u);

    require(peak(hover[0u]) > 0.05f && peak(hover[1u]) > 0.05f,
        "hover stereo output");
    require(difference(hover[0u], hover[1u]) > 10.0,
        "true stereo source relationship");
    require(difference(hover[0u], mirror[0u]) > 10.0,
        "mirror differs from hover");
    require(difference(hover[0u], motor[0u]) > 10.0,
        "motor articulation differs from continuous");
    require(difference(drunkA[0u], drunkB[0u]) < 1.0e-8,
        "seeded drunk motion is reproducible");
    require(difference(drunkA[0u], drunkC[0u]) > 1.0,
        "different seeds change drunk motion");

    auto asset = makeAsset();
    SampleMotionEngine engine;
    require(engine.prepare(48000.0), "event engine prepare");
    engine.setAsset(asset.get());
    MotionSettings settings;
    settings.attackSeconds = 0.0f;
    settings.releaseSeconds = 0.001f;
    std::array<float, 256u> left {};
    std::array<float, 256u> right {};
    std::array<MotionRenderEvent, 2u> events {{
        { 32u, MotionEventKind::NoteOn, 1u, 60u, 1.0f, 0u },
        { 128u, MotionEventKind::NoteOff, 1u, 60u, 0.0f, 0u },
    }};
    engine.render(settings, events.data(), events.size(), left.data(),
        right.data(), static_cast<uint32_t>(left.size()));
    require(std::all_of(left.begin(), left.begin() + 32,
            [](float value) { return value == 0.0f; }),
        "sample-accurate note onset");
    require(engine.activeVoiceCount() == 0u,
        "gate note releases");

    settings.voiceMode = VoiceMode::Legato;
    settings.releaseSeconds = 0.05f;
    MotionRenderEvent first { 0u, MotionEventKind::NoteOn,
        2u, 60u, 1.0f, 0u };
    engine.render(settings, &first, 1u, left.data(), right.data(), 128u);
    const auto before = engine.voiceCursors()[0u];
    MotionRenderEvent second { 0u, MotionEventKind::NoteOn,
        3u, 67u, 1.0f, 0u };
    engine.render(settings, &second, 1u, left.data(), right.data(), 1u);
    const auto after = engine.voiceCursors()[0u];
    require(engine.activeVoiceCount() == 1u && after.key == 67u,
        "legato retarget");
    require(std::abs(after.motionPhase - before.motionPhase) < 0.01f,
        "legato preserves motion phase");

    {
        auto constant = makeConstantAsset();
        SampleMotionEngine motorEngine;
        require(motorEngine.prepare(48000.0), "motor onset prepare");
        motorEngine.setAsset(constant.get());
        MotionSettings motorSettings;
        motorSettings.articulation = MotionArticulation::Motor;
        motorSettings.outputGainDecibels = 0.0f;
        motorSettings.attackSeconds = 0.0f;
        MotionRenderEvent motorNote { 0u, MotionEventKind::NoteOn,
            11u, 60u, 1.0f, 0u };
        std::array<float, 1u> motorLeft {};
        std::array<float, 1u> motorRight {};
        motorEngine.render(motorSettings, &motorNote, 1u,
            motorLeft.data(), motorRight.data(), 1u);
        require(motorLeft[0u] > 0.5f && motorRight[0u] > 0.4f,
            "Motor begins inside an audible packet at the outer peak");
    }

    {
        SampleMotionEngine heldDrunk;
        require(heldDrunk.prepare(48000.0), "held drunk prepare");
        heldDrunk.setAsset(asset.get());
        MotionSettings heldSettings;
        heldSettings.motion = MotionMode::Drunk;
        heldSettings.travel = 0.0f;
        heldSettings.motionRate = 19.0f;
        heldSettings.rateBasis = MotionRateBasis::Hertz;
        heldSettings.attackSeconds = 0.0f;
        MotionRenderEvent note { 0u, MotionEventKind::NoteOn,
            12u, 60u, 1.0f, 0u };
        heldDrunk.render(heldSettings, &note, 1u, left.data(), right.data(),
            static_cast<uint32_t>(left.size()));
        for (uint32_t block = 0u; block < 32u; ++block)
            heldDrunk.render(heldSettings, nullptr, 0u, left.data(),
                right.data(), static_cast<uint32_t>(left.size()));
        require(std::abs(heldDrunk.primaryPositionNormalized()
                - heldSettings.locus) < 1.0e-6,
            "zero Travel holds the Drunk trajectory at Locus");
    }

    {
        SampleMotionEngine repeated;
        require(repeated.prepare(48000.0), "repeated-note prepare");
        repeated.setAsset(asset.get());
        MotionSettings repeatedSettings;
        repeatedSettings.attackSeconds = 0.0f;
        repeatedSettings.releaseSeconds = 0.0f;
        std::array<MotionRenderEvent, 2u> repeatedNotes {{
            { 0u, MotionEventKind::NoteOn, 0u, 64u, 1.0f, 2u },
            { 1u, MotionEventKind::NoteOn, 0u, 64u, 1.0f, 2u },
        }};
        repeated.render(repeatedSettings, repeatedNotes.data(),
            repeatedNotes.size(), left.data(), right.data(), 8u);
        require(repeated.activeVoiceCount() == 2u,
            "poly repeated notes layer");
        MotionRenderEvent releaseAll { 0u, MotionEventKind::NoteOff,
            0u, 64u, 0.0f, 2u };
        repeated.render(repeatedSettings, &releaseAll, 1u, left.data(),
            right.data(), 1u);
        require(repeated.activeVoiceCount() == 0u,
            "MIDI note-off releases every matching unlabelled voice");
    }

    {
        SampleMotionEngine switching;
        require(switching.prepare(48000.0), "mode switch prepare");
        switching.setAsset(asset.get());
        MotionSettings switchSettings;
        switchSettings.attackSeconds = 0.0f;
        MotionRenderEvent note { 0u, MotionEventKind::NoteOn,
            13u, 60u, 1.0f, 0u };
        switching.render(switchSettings, &note, 1u, left.data(),
            right.data(), 128u);
        const float hoverPosition = switching.primaryPositionNormalized();
        switchSettings.motion = MotionMode::Mirror;
        switching.render(switchSettings, nullptr, 0u, left.data(),
            right.data(), 1u);
        require(std::abs(switching.primaryPositionNormalized()
                - hoverPosition) < 0.001f,
            "live motion-mode changes preserve source position");
    }

    {
        auto constant = makeConstantAsset();
        SampleMotionEngine normal;
        require(normal.prepare(48000.0), "normal-rate prepare");
        normal.setAsset(constant.get());
        MotionSettings native;
        native.motion = MotionMode::Forward;
        native.rateBasis = MotionRateBasis::Normal;
        native.motionRate = 1.0f;
        native.start = 0.0;
        native.end = 1.0;
        native.locus = 0.25;
        native.field = 0.50;
        native.attackSeconds = 0.0f;
        MotionRenderEvent note { 0u, MotionEventKind::NoteOn,
            20u, 60u, 1.0f, 0u };
        std::vector<float> normalLeft(4800u);
        std::vector<float> normalRight(4800u);
        normal.render(native, &note, 1u, normalLeft.data(),
            normalRight.data(), static_cast<uint32_t>(normalLeft.size()));
        require(std::abs(normal.primaryPositionNormalized() - 0.35f)
                < 0.002f,
            "Normal 1x follows the source's native playback speed");
    }

    {
        auto constant = makeConstantAsset();
        MotionSettings directional;
        directional.rateBasis = MotionRateBasis::Hertz;
        directional.motionRate = 0.5f;
        directional.field = 0.8;
        directional.attackSeconds = 0.0f;
        std::array<float, 512u> directionalLeft {};
        std::array<float, 512u> directionalRight {};
        MotionRenderEvent note { 0u, MotionEventKind::NoteOn,
            21u, 60u, 1.0f, 0u };

        SampleMotionEngine forward;
        require(forward.prepare(48000.0), "forward prepare");
        forward.setAsset(constant.get());
        directional.motion = MotionMode::Forward;
        forward.render(directional, &note, 1u, directionalLeft.data(),
            directionalRight.data(), directionalLeft.size());
        require(forward.voiceCursors()[0u].directionForward,
            "Forward is a one-way trajectory");

        SampleMotionEngine reverse;
        require(reverse.prepare(48000.0), "reverse prepare");
        reverse.setAsset(constant.get());
        directional.motion = MotionMode::Reverse;
        reverse.render(directional, &note, 1u, directionalLeft.data(),
            directionalRight.data(), directionalLeft.size());
        require(!reverse.voiceCursors()[0u].directionForward,
            "Reverse is a one-way trajectory");
    }

    {
        SampleMotionEngine zigzag;
        require(zigzag.prepare(48000.0), "zigzag prepare");
        zigzag.setAsset(asset.get());
        MotionSettings zig;
        zig.motion = MotionMode::Zigzag;
        zig.rateBasis = MotionRateBasis::Hertz;
        zig.motionRate = 5.0f;
        zig.field = 0.8;
        zig.travel = 0.55f;
        zig.jitter = 0.35f;
        zig.attackSeconds = 0.0f;
        zig.seed = 8181u;
        MotionRenderEvent note { 0u, MotionEventKind::NoteOn,
            22u, 60u, 1.0f, 0u };
        std::array<float, 1024u> zigLeft {};
        std::array<float, 1024u> zigRight {};
        bool previousDirection = true;
        uint32_t changes = 0u;
        for (uint32_t block = 0u; block < 96u; ++block) {
            zigzag.render(zig, block == 0u ? &note : nullptr,
                block == 0u ? 1u : 0u, zigLeft.data(), zigRight.data(),
                zigLeft.size());
            const bool direction = zigzag.voiceCursors()[0u].directionForward;
            if (block != 0u && direction != previousDirection) ++changes;
            previousDirection = direction;
        }
        require(changes >= 4u,
            "Zigzag alternates forward and reverse source segments");
    }

    {
        auto constant = makeConstantAsset();
        SampleMotionEngine packetEngine;
        require(packetEngine.prepare(48000.0), "packets prepare");
        packetEngine.setAsset(constant.get());
        MotionSettings packetSettings;
        packetSettings.articulation = MotionArticulation::Packets;
        packetSettings.innerRateHz = 20.0f;
        packetSettings.packetDuty = 0.25f;
        packetSettings.joinAmount = 1.0f;
        packetSettings.attackSeconds = 0.0f;
        packetSettings.outputGainDecibels = 0.0f;
        MotionRenderEvent note { 0u, MotionEventKind::NoteOn,
            23u, 60u, 1.0f, 0u };
        std::vector<float> packetLeft(4800u);
        std::vector<float> packetRight(4800u);
        packetEngine.render(packetSettings, &note, 1u, packetLeft.data(),
            packetRight.data(), static_cast<uint32_t>(packetLeft.size()));
        const auto silent = static_cast<std::size_t>(std::count_if(
            packetLeft.begin(), packetLeft.end(),
            [](float value) { return std::abs(value) < 1.0e-7f; }));
        require(peak(packetLeft) > 0.5f
                && silent > packetLeft.size() / 2u,
            "ordinary Packets applies an audible inner packet train");
    }

    {
        constexpr float phase = 0.125f;
        constexpr float symmetry = 0.5f;
        const float linear = motorEnvelopeLevel(phase, symmetry,
            MotorEnvelopeShape::Linear);
        const float rounded = motorEnvelopeLevel(phase, symmetry,
            MotorEnvelopeShape::Rounded);
        const float exponential = motorEnvelopeLevel(phase, symmetry,
            MotorEnvelopeShape::Exponential);
        const float plateau = motorEnvelopeLevel(phase, symmetry,
            MotorEnvelopeShape::Plateau);
        require(exponential < rounded && rounded < linear
                && linear < plateau,
            "Motor envelope shapes produce distinct contours");
        for (const auto shape : { MotorEnvelopeShape::Linear,
                 MotorEnvelopeShape::Rounded,
                 MotorEnvelopeShape::Exponential,
                 MotorEnvelopeShape::Plateau }) {
            require(motorEnvelopeLevel(0.0f, symmetry, shape) == 0.0f
                    && motorEnvelopeLevel(symmetry, symmetry, shape) == 1.0f
                    && motorEnvelopeLevel(1.0f, symmetry, shape) == 0.0f,
                "Motor envelope endpoints and peak");
        }
    }

    {
        auto constant = makeConstantAsset();
        SampleMotionEngine multichannel;
        require(multichannel.prepare(48000.0, 32u),
            "32-channel allocator prepare");
        multichannel.setAsset(constant.get());
        MotionSettings routed;
        routed.attackSeconds = 0.0f;
        routed.outputGainDecibels = 0.0f;
        routed.activeOutputChannelCount = 8u;
        routed.outputRouting.width = s3g::routing::OutputVoiceWidth::Mono;
        routed.outputRouting.traversal =
            s3g::routing::OutputTraversal::Sequential;
        std::array<MotionRenderEvent, 2u> notes {{
            { 0u, MotionEventKind::NoteOn, 31u, 60u, 1.0f, 0u },
            { 1u, MotionEventKind::NoteOn, 32u, 64u, 1.0f, 0u },
        }};
        std::array<std::array<float, 128u>, 32u> channelStorage {};
        std::array<float*, 32u> channelPointers {};
        for (std::size_t channel = 0u; channel < channelStorage.size();
             ++channel)
            channelPointers[channel] = channelStorage[channel].data();
        multichannel.render(routed, notes.data(), notes.size(),
            channelPointers.data(), channelPointers.size(),
            channelStorage[0u].size());
        const auto channelPeak = [&](std::size_t channel) {
            float result = 0.0f;
            for (float value : channelStorage[channel])
                result = std::max(result, std::abs(value));
            return result;
        };
        require(channelPeak(0u) > 0.1f && channelPeak(1u) > 0.1f,
            "sequential notes receive distinct 32-channel outputs");
        bool unusedSilent = true;
        for (std::size_t channel = 2u; channel < channelStorage.size();
             ++channel)
            unusedSilent = unusedSilent && channelPeak(channel) == 0.0f;
        require(unusedSilent,
            "unassigned multichannel outputs remain silent");
        require(multichannel.voiceCursorCount() == 2u
                && multichannel.voiceCursors()[0u].outputFirstChannel == 0u
                && multichannel.voiceCursors()[1u].outputFirstChannel == 1u,
            "cursor reports retained output assignments");
    }

    {
        auto constant = makeConstantAsset();
        SampleMotionEngine movingLoop;
        require(movingLoop.prepare(48000.0), "moving loop prepare");
        movingLoop.setAsset(constant.get());
        MotionSettings loop;
        loop.motion = MotionMode::MovingLoop;
        loop.rateBasis = MotionRateBasis::Hertz;
        loop.motionRate = 20.0f;
        loop.locus = 0.30;
        loop.field = 0.10;
        loop.eventStep = 0.08f;
        loop.attackSeconds = 0.0f;
        MotionRenderEvent note { 0u, MotionEventKind::NoteOn,
            40u, 60u, 1.0f, 0u };
        std::vector<float> loopLeft(3000u);
        std::vector<float> loopRight(3000u);
        movingLoop.render(loop, &note, 1u, loopLeft.data(), loopRight.data(),
            static_cast<uint32_t>(loopLeft.size()));
        require(movingLoop.voiceCursors()[0u].fieldLowNormalized > 0.30f,
            "Moving Loop advances its bounded window after a wrap");
    }

    {
        auto constant = makeConstantAsset();
        SampleMotionEngine bakToBak;
        require(bakToBak.prepare(48000.0), "BaktoBak prepare");
        bakToBak.setAsset(constant.get());
        MotionSettings back;
        back.motion = MotionMode::BakToBak;
        back.rateBasis = MotionRateBasis::Hertz;
        back.motionRate = 4.0f;
        back.attackSeconds = 0.0f;
        MotionRenderEvent note { 0u, MotionEventKind::NoteOn,
            41u, 60u, 1.0f, 0u };
        std::vector<float> backLeft(7000u);
        std::vector<float> backRight(7000u);
        bakToBak.render(back, &note, 1u, backLeft.data(), backRight.data(),
            static_cast<uint32_t>(backLeft.size()));
        require(!bakToBak.voiceCursors()[0u].directionForward,
            "BaktoBak returns without Mirror's polarity inversion");
    }

    {
        auto constant = makeConstantAsset();
        SampleMotionEngine mchZig;
        require(mchZig.prepare(48000.0, 32u), "MCHZIG prepare");
        mchZig.setAsset(constant.get());
        MotionSettings zig;
        zig.motion = MotionMode::Zigzag;
        zig.rateBasis = MotionRateBasis::Hertz;
        zig.motionRate = 20.0f;
        zig.travel = 0.45f;
        zig.attackSeconds = 0.0f;
        zig.outputGainDecibels = 0.0f;
        zig.activeOutputChannelCount = 8u;
        zig.outputRouting.width = s3g::routing::OutputVoiceWidth::Mono;
        zig.outputRouting.traversal =
            s3g::routing::OutputTraversal::RandomCycle;
        zig.outputRouting.avoidAdjacent = true;
        zig.outputAssignmentEvent = OutputAssignmentEvent::Turn;
        MotionRenderEvent note { 0u, MotionEventKind::NoteOn,
            42u, 60u, 1.0f, 0u };
        std::array<std::vector<float>, 32u> channels;
        std::array<float*, 32u> pointers {};
        for (std::size_t channel = 0u; channel < channels.size(); ++channel) {
            channels[channel].resize(12000u);
            pointers[channel] = channels[channel].data();
        }
        mchZig.render(zig, &note, 1u, pointers.data(), pointers.size(),
            static_cast<uint32_t>(channels[0u].size()));
        uint32_t used = 0u;
        for (const auto& channel : channels)
            if (peak(channel) > 0.05f) ++used;
        require(used >= 4u,
            "MCHZIG reallocates a held voice at successive turns");
    }

    {
        auto constant = makeConstantAsset();
        for (const auto model : { SegmentModel::Freeze,
                 SegmentModel::Iterate, SegmentModel::Pulser,
                 SegmentModel::Doublets, SegmentModel::Bounce }) {
            SampleMotionEngine eventEngine;
            require(eventEngine.prepare(48000.0), "segment model prepare");
            eventEngine.setAsset(constant.get());
            MotionSettings eventSettings;
            eventSettings.segmentModel = model;
            eventSettings.eventRateHz = 20.0f;
            eventSettings.eventRepeats = 4u;
            eventSettings.field = 0.02;
            eventSettings.eventStep = 0.04f;
            eventSettings.eventPitchScatterSemitones = 0.5f;
            eventSettings.eventLevelVariation = 0.2f;
            eventSettings.eventIntervalCurve = model == SegmentModel::Bounce
                ? 0.12f : 0.0f;
            eventSettings.segmentOverlap = SegmentOverlap::Layer;
            eventSettings.attackSeconds = 0.0f;
            MotionRenderEvent note { 0u, MotionEventKind::NoteOn,
                50u + static_cast<uint64_t>(model), 60u, 1.0f, 0u };
            std::vector<float> eventLeft(4800u);
            std::vector<float> eventRight(4800u);
            eventEngine.render(eventSettings, &note, 1u, eventLeft.data(),
                eventRight.data(), static_cast<uint32_t>(eventLeft.size()));
            require(eventEngine.segmentEventCount() >= 2u
                    && peak(eventLeft) > 0.05f,
                "segment models retrigger audible source events");
        }
    }

    {
        auto constant = makeConstantAsset();
        SampleMotionEngine grouped;
        require(grouped.prepare(48000.0), "Doublets grouped prepare");
        grouped.setAsset(constant.get());
        MotionSettings groupedSettings;
        groupedSettings.segmentModel = SegmentModel::Doublets;
        groupedSettings.segmentTrigger = SegmentTrigger::Clock;
        groupedSettings.eventRateHz = 0.25f;
        groupedSettings.eventRepeats = 3u;
        groupedSettings.field = 0.01;
        groupedSettings.eventStep = 0.01f;
        groupedSettings.attackSeconds = 0.0f;
        MotionRenderEvent note { 0u, MotionEventKind::NoteOn,
            58u, 60u, 1.0f, 0u };
        std::vector<float> groupedLeft(4800u);
        std::vector<float> groupedRight(4800u);
        grouped.render(groupedSettings, &note, 1u, groupedLeft.data(),
            groupedRight.data(), static_cast<uint32_t>(groupedLeft.size()));
        require(grouped.segmentEventCount() >= 8u,
            "Doublets retriggers contiguous segments as grouped slices");
    }

    {
        auto constant = makeConstantAsset();
        SampleMotionEngine bouncing;
        require(bouncing.prepare(48000.0), "Bounce acceleration prepare");
        bouncing.setAsset(constant.get());
        MotionSettings bounceSettings;
        bounceSettings.segmentModel = SegmentModel::Bounce;
        bounceSettings.eventRateHz = 2.0f;
        bounceSettings.eventRepeats = 8u;
        bounceSettings.eventIntervalCurve = 0.35f;
        bounceSettings.eventLevelVariation = 0.18f;
        bounceSettings.packetDuty = 0.25f;
        bounceSettings.field = 0.02;
        bounceSettings.segmentOverlap = SegmentOverlap::Layer;
        bounceSettings.attackSeconds = 0.0f;
        MotionRenderEvent note { 0u, MotionEventKind::NoteOn,
            59u, 60u, 1.0f, 0u };
        std::vector<float> bounceLeft(96000u);
        std::vector<float> bounceRight(96000u);
        bouncing.render(bounceSettings, &note, 1u, bounceLeft.data(),
            bounceRight.data(), static_cast<uint32_t>(bounceLeft.size()));
        require(bouncing.segmentEventCount() >= 8u,
            "Bounce accelerates successive repeats above its start rate");
    }

    {
        auto constant = makeConstantAsset();
        SampleMotionEngine mchIter;
        require(mchIter.prepare(48000.0, 32u), "MCH Iterate prepare");
        mchIter.setAsset(constant.get());
        MotionSettings iter;
        iter.segmentModel = SegmentModel::MchIter;
        iter.eventRateHz = 20.0f;
        iter.field = 0.01;
        iter.attackSeconds = 0.0f;
        iter.outputGainDecibels = 0.0f;
        iter.activeOutputChannelCount = 8u;
        iter.outputRouting.width = s3g::routing::OutputVoiceWidth::Mono;
        iter.outputRouting.traversal =
            s3g::routing::OutputTraversal::Sequential;
        MotionRenderEvent note { 0u, MotionEventKind::NoteOn,
            60u, 60u, 1.0f, 0u };
        std::array<std::vector<float>, 32u> channels;
        std::array<float*, 32u> pointers {};
        for (std::size_t channel = 0u; channel < channels.size(); ++channel) {
            channels[channel].resize(9600u);
            pointers[channel] = channels[channel].data();
        }
        mchIter.render(iter, &note, 1u, pointers.data(), pointers.size(),
            static_cast<uint32_t>(channels[0u].size()));
        uint32_t used = 0u;
        for (const auto& channel : channels)
            if (peak(channel) > 0.05f) ++used;
        require(used >= 3u,
            "MCH Iterate allocates successive events across outputs");
    }

    std::cout << "sample motion smoke passed\n";
    return 0;
}
