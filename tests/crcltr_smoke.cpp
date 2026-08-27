#include "s3g_crcltr.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

constexpr double kSampleRate = 1000.0;
constexpr uint32_t kBlockFrames = 25u;

bool finiteBuffer(const std::vector<float>& buffer)
{
    return std::all_of(buffer.begin(), buffer.end(),
        [](float value) { return std::isfinite(value); });
}

float energy(const std::vector<float>& buffer)
{
    float sum = 0.0f;
    for (float value : buffer) sum += value * value;
    return sum;
}

float maximumStep(const std::vector<float>& buffer)
{
    float maximum = 0.0f;
    for (std::size_t index = 1u; index < buffer.size(); ++index)
        maximum = std::max(maximum,
            std::abs(buffer[index] - buffer[index - 1u]));
    return maximum;
}

void render(s3g::Crcltr& dsp, const std::vector<float>& left,
            const std::vector<float>& right, std::vector<float>& outputLeft,
            std::vector<float>& outputRight)
{
    outputLeft.resize(left.size());
    outputRight.resize(left.size());
    dsp.process(left.data(), right.data(), outputLeft.data(), outputRight.data(),
        static_cast<uint32_t>(left.size()));
}

} // namespace

int main()
{
    bool ok = true;
    const auto equalPowerCenter = s3g::crcltrCrossfadeGains(
        s3g::CrcltrCrossfadeShape::EqualPower, 0.5f);
    const auto linearCenter = s3g::crcltrCrossfadeGains(
        s3g::CrcltrCrossfadeShape::Linear, 0.5f);
    const auto wideCenter = s3g::crcltrCrossfadeGains(
        s3g::CrcltrCrossfadeShape::Wide, 0.5f);
    const auto tightCenter = s3g::crcltrCrossfadeGains(
        s3g::CrcltrCrossfadeShape::Tight, 0.5f);
    const auto wideAtB = s3g::crcltrCrossfadeGains(
        s3g::CrcltrCrossfadeShape::Wide, 1.0f);
    const auto smoothQuarter = s3g::crcltrCrossfadeGains(
        s3g::CrcltrCrossfadeShape::Smooth, 0.25f);
    const auto overlapCenter = s3g::crcltrCrossfadeGains(
        s3g::CrcltrCrossfadeShape::FullOverlap, 0.5f);
    const auto deepDipCenter = s3g::crcltrCrossfadeGains(
        s3g::CrcltrCrossfadeShape::DeepDip, 0.5f);
    const auto plateauCenter = s3g::crcltrCrossfadeGains(
        s3g::CrcltrCrossfadeShape::Plateau, 0.5f);
    const auto cutBefore = s3g::crcltrCrossfadeGains(
        s3g::CrcltrCrossfadeShape::Cut, 0.25f);
    const auto cutAfter = s3g::crcltrCrossfadeGains(
        s3g::CrcltrCrossfadeShape::Cut, 0.75f);
    ok = ok && std::abs(equalPowerCenter.a - equalPowerCenter.b) < 0.0001f
        && std::abs(linearCenter.a - 0.5f) < 0.0001f
        && std::abs(linearCenter.b - 0.5f) < 0.0001f
        && wideCenter.a > equalPowerCenter.a
        && wideCenter.b > equalPowerCenter.b
        && tightCenter.a < equalPowerCenter.a
        && tightCenter.b < equalPowerCenter.b
        && std::isfinite(wideAtB.a) && std::isfinite(wideAtB.b)
        && wideAtB.a == 0.0f && std::abs(wideAtB.b - 1.0f) < 0.0001f
        && smoothQuarter.a > 0.8f && smoothQuarter.b < 0.2f
        && overlapCenter.a == 1.0f && overlapCenter.b == 1.0f
        && deepDipCenter.a < tightCenter.a
        && deepDipCenter.b < tightCenter.b
        && std::abs(plateauCenter.a - 0.70710678f) < 0.0001f
        && std::abs(plateauCenter.b - 0.70710678f) < 0.0001f
        && cutBefore.a == 1.0f && std::abs(cutBefore.b) < 0.0001f
        && std::abs(cutAfter.a) < 0.0001f && cutAfter.b == 1.0f;
    for (uint32_t shape = 0u; shape <= 8u; ++shape) {
        const auto atA = s3g::crcltrCrossfadeGains(
            static_cast<s3g::CrcltrCrossfadeShape>(shape), 0.0f);
        const auto atB = s3g::crcltrCrossfadeGains(
            static_cast<s3g::CrcltrCrossfadeShape>(shape), 1.0f);
        ok = ok && std::abs(atA.a - 1.0f) < 0.0001f
            && std::abs(atA.b) < 0.0001f
            && std::abs(atB.a) < 0.0001f
            && std::abs(atB.b - 1.0f) < 0.0001f;
    }
    s3g::Crcltr dsp;
    ok = ok && dsp.prepare(kSampleRate, kBlockFrames);
    ok = ok && !dsp.usingExternalMemory();
    ok = ok && dsp.loopCapacityFrames() == 32000u;

    s3g::CrcltrParams params;
    params.blend = 0.0f;
    params.outputGain = 1.0f;
    dsp.setParams(params);

    std::vector<float> inputLeft(50u, 0.1f);
    std::vector<float> inputRight(50u, -0.1f);
    std::vector<float> outputLeft;
    std::vector<float> outputRight;
    render(dsp, inputLeft, inputRight, outputLeft, outputRight);
    ok = ok && finiteBuffer(outputLeft) && finiteBuffer(outputRight);
    ok = ok && energy(outputLeft) > 0.01f && energy(outputRight) > 0.01f;

    // A tap shorter than 100 ms must leave the current loop untouched.
    params.recordTarget = s3g::CrcltrRecordTarget::Loop1;
    params.record = true;
    dsp.setParams(params);
    render(dsp, inputLeft, inputRight, outputLeft, outputRight);
    params.record = false;
    dsp.setParams(params);
    std::vector<float> oneFrame(1u, 0.0f);
    render(dsp, oneFrame, oneFrame, outputLeft, outputRight);
    ok = ok && dsp.recordedFrames(0u) == 0u;

    // Loop 1 records twice the intended loop duration, matching CRCLTR v1.0.1.
    std::vector<float> recordLeft(400u);
    std::vector<float> recordRight(400u);
    for (uint32_t i = 0u; i < recordLeft.size(); ++i) {
        recordLeft[i] = 0.22f * std::sin(0.031f * static_cast<float>(i));
        recordRight[i] = 0.18f * std::cos(0.027f * static_cast<float>(i));
    }
    params.record = true;
    dsp.setParams(params);
    render(dsp, recordLeft, recordRight, outputLeft, outputRight);
    params.record = false;
    params.blend = 1.0f;
    params.crossfadeMode = s3g::CrcltrCrossfadeMode::Manual;
    params.crossfade = 0.0f;
    dsp.setParams(params);
    render(dsp, oneFrame, oneFrame, outputLeft, outputRight);
    ok = ok && dsp.recordedFrames(0u) == 400u;
    ok = ok && dsp.playbackFrames(0u) == 200u;

    std::vector<float> silence(300u, 0.0f);
    render(dsp, silence, silence, outputLeft, outputRight);
    ok = ok && finiteBuffer(outputLeft) && finiteBuffer(outputRight);
    ok = ok && energy(outputLeft) > 0.001f && energy(outputRight) > 0.001f;

    // Loop 2 uses the full recorded duration and the same seamless selected-
    // window reader as Loop 1 by default.
    params.recordTarget = s3g::CrcltrRecordTarget::Loop2;
    params.record = true;
    dsp.setParams(params);
    render(dsp, recordLeft, recordRight, outputLeft, outputRight);
    params.record = false;
    params.crossfade = 1.0f;
    dsp.setParams(params);
    render(dsp, oneFrame, oneFrame, outputLeft, outputRight);
    ok = ok && dsp.recordedFrames(1u) == 400u;
    ok = ok && dsp.playbackFrames(1u) == 400u;
    render(dsp, silence, silence, outputLeft, outputRight);
    ok = ok && finiteBuffer(outputLeft) && finiteBuffer(outputRight);
    ok = ok && energy(outputLeft) > 0.0001f && energy(outputRight) > 0.0001f;

    // The instrument model treats both captures as complete editable slots.
    params.playbackModel = s3g::CrcltrPlaybackModel::Dual;
    params.loop1Reverse = true;
    params.loop1Start = 0.1f;
    params.loop1End = 0.9f;
    params.loop1Join = s3g::CrcltrLoopJoin::Duck;
    params.crossfadeMode = s3g::CrcltrCrossfadeMode::RandomWalk;
    dsp.setParams(params);
    const bool queuedInstrumentEdit = dsp.loopWindowPending(0u)
        && dsp.playbackFrames(0u) == 400u
        && std::abs(dsp.activeLoopStart(0u)) < 0.0001f
        && std::abs(dsp.activeLoopEnd(0u) - 1.0f) < 0.0001f;
    ok = ok && queuedInstrumentEdit;
    render(dsp, silence, silence, outputLeft, outputRight);
    const bool committedInstrumentEdit = !dsp.loopWindowPending(0u)
        && dsp.playbackFrames(0u) == 320u
        && finiteBuffer(outputLeft) && finiteBuffer(outputRight)
        && energy(outputLeft) > 0.0001f;
    ok = ok && committedInstrumentEdit;
    if (!queuedInstrumentEdit || !committedInstrumentEdit) {
        std::cerr << "CRCLTR instrument edit state: queued="
                  << queuedInstrumentEdit << " committed="
                  << committedInstrumentEdit << " pending="
                  << dsp.loopWindowPending(0u) << " frames="
                  << dsp.playbackFrames(0u) << " energy="
                  << energy(outputLeft) << "\n";
    }

    // Overdub keeps the established loop length and remains audible while
    // recording instead of replacing playback with the record monitor.
    params.recordTarget = s3g::CrcltrRecordTarget::Loop1;
    params.recordMode = s3g::CrcltrRecordMode::Overdub;
    params.overdubFeedback = 1.0f;
    params.crossfadeMode = s3g::CrcltrCrossfadeMode::Manual;
    params.crossfade = 0.0f;
    params.record = true;
    dsp.setParams(params);
    render(dsp, silence, silence, outputLeft, outputRight);
    ok = ok && dsp.recordedFrames(0u) == 400u
        && energy(outputLeft) > 0.0001f;
    params.record = false;
    dsp.setParams(params);
    render(dsp, oneFrame, oneFrame, outputLeft, outputRight);

    // Punch writes directly into an existing loop without changing its size.
    std::vector<float> beforePunchLeft(dsp.recordedFrames(0u));
    std::vector<float> beforePunchRight(dsp.recordedFrames(0u));
    ok = ok && dsp.copyLoop(0u, beforePunchLeft.data(),
        beforePunchRight.data(), static_cast<uint32_t>(beforePunchLeft.size()));
    params.recordMode = s3g::CrcltrRecordMode::Punch;
    params.record = true;
    dsp.setParams(params);
    std::vector<float> punchLeft(80u, 0.65f);
    std::vector<float> punchRight(80u, -0.55f);
    render(dsp, punchLeft, punchRight, outputLeft, outputRight);
    params.record = false;
    dsp.setParams(params);
    render(dsp, oneFrame, oneFrame, outputLeft, outputRight);
    std::vector<float> afterPunchLeft(dsp.recordedFrames(0u));
    std::vector<float> afterPunchRight(dsp.recordedFrames(0u));
    ok = ok && dsp.copyLoop(0u, afterPunchLeft.data(), afterPunchRight.data(),
        static_cast<uint32_t>(afterPunchLeft.size()))
        && dsp.recordedFrames(0u) == 400u
        && (beforePunchLeft != afterPunchLeft
            || beforePunchRight != afterPunchRight);

    // Captures can be copied into CLAP state and restored at a new sample rate.
    std::vector<float> savedLeft(dsp.recordedFrames(0u));
    std::vector<float> savedRight(dsp.recordedFrames(0u));
    ok = ok && dsp.copyLoop(0u, savedLeft.data(), savedRight.data(),
        static_cast<uint32_t>(savedLeft.size()));
    s3g::Crcltr restored;
    ok = ok && restored.prepare(2000.0, kBlockFrames)
        && restored.restoreLoop(0u, savedLeft.data(), savedRight.data(),
            static_cast<uint32_t>(savedLeft.size()), kSampleRate)
        && restored.recordedFrames(0u) == savedLeft.size() * 2u;

    // Start and End constrain the samples actually read in Classic mode as
    // well as in Dual Slots. Classic A's editable source remains its first
    // half; Classic B's editable source remains the complete capture.
    s3g::Crcltr windowDsp;
    ok = ok && windowDsp.prepare(kSampleRate, kBlockFrames);
    std::vector<float> windowA(400u, 0.0f);
    std::fill_n(windowA.begin(), 100u, 0.65f);
    std::vector<float> windowB(400u, 0.0f);
    std::fill_n(windowB.begin(), 200u, 0.55f);
    ok = ok && windowDsp.restoreLoop(0u, windowA.data(), windowA.data(),
        static_cast<uint32_t>(windowA.size()), kSampleRate);
    ok = ok && windowDsp.restoreLoop(1u, windowB.data(), windowB.data(),
        static_cast<uint32_t>(windowB.size()), kSampleRate);
    s3g::CrcltrParams windowParams;
    windowParams.blend = 1.0f;
    windowParams.crossfadeMode = s3g::CrcltrCrossfadeMode::Manual;
    windowParams.crossfade = 0.0f;
    windowParams.playbackModel = s3g::CrcltrPlaybackModel::Classic;
    windowParams.loop1Start = 0.0f;
    windowParams.loop1End = 0.5f;
    windowDsp.setParams(windowParams);
    windowDsp.reset();
    std::vector<float> windowSilence(80u, 0.0f);
    render(windowDsp, windowSilence, windowSilence,
        outputLeft, outputRight);
    ok = ok && windowDsp.playbackFrames(0u) == 100u
        && energy(outputLeft) > 0.01f
        && windowDsp.playbackPosition(0u) <= 0.25f;

    windowParams.loop1Start = 0.5f;
    windowParams.loop1End = 1.0f;
    windowDsp.setParams(windowParams);
    windowDsp.reset();
    render(windowDsp, windowSilence, windowSilence,
        outputLeft, outputRight);
    ok = ok && windowDsp.playbackFrames(0u) == 100u
        && energy(outputLeft) < 0.000001f
        && windowDsp.playbackPosition(0u) >= 0.25f
        && windowDsp.playbackPosition(0u) <= 0.5f;

    windowParams.crossfade = 1.0f;
    windowParams.loop2Start = 0.0f;
    windowParams.loop2End = 0.5f;
    windowDsp.setParams(windowParams);
    windowDsp.reset();
    render(windowDsp, windowSilence, windowSilence,
        outputLeft, outputRight);
    ok = ok && windowDsp.playbackFrames(1u) == 200u
        && energy(outputLeft) > 0.01f;

    windowParams.loop2Start = 0.5f;
    windowParams.loop2End = 1.0f;
    windowDsp.setParams(windowParams);
    windowDsp.reset();
    render(windowDsp, windowSilence, windowSilence,
        outputLeft, outputRight);
    ok = ok && windowDsp.playbackFrames(1u) == 200u
        && energy(outputLeft) < 0.000001f;

    // Moving Start/End creates a new loop window, and Seam applies the same
    // short End-to-Start wrap crossfade to that window as to a full capture.
    // A ramp makes an uncrossfaded wrap produce an obvious output step.
    s3g::Crcltr seamDsp;
    ok = ok && seamDsp.prepare(kSampleRate, kBlockFrames);
    std::vector<float> seamSource(400u);
    for (uint32_t frame = 0u; frame < seamSource.size(); ++frame) {
        seamSource[frame] = -0.9f + 1.8f
            * static_cast<float>(frame)
            / static_cast<float>(seamSource.size() - 1u);
    }
    ok = ok && seamDsp.restoreLoop(0u, seamSource.data(), seamSource.data(),
        static_cast<uint32_t>(seamSource.size()), kSampleRate);
    s3g::CrcltrParams seamParams;
    seamParams.playbackModel = s3g::CrcltrPlaybackModel::Dual;
    seamParams.blend = 1.0f;
    seamParams.crossfadeMode = s3g::CrcltrCrossfadeMode::Manual;
    seamParams.crossfade = 0.0f;
    seamParams.loop1Join = s3g::CrcltrLoopJoin::Seam;
    seamParams.loop1Start = 0.25f;
    seamParams.loop1End = 0.75f;
    seamDsp.setParams(seamParams);
    seamDsp.reset();
    std::vector<float> seamSilence(600u, 0.0f);
    render(seamDsp, seamSilence, seamSilence, outputLeft, outputRight);
    const float firstWindowLeftStep = maximumStep(outputLeft);
    const float firstWindowRightStep = maximumStep(outputRight);
    ok = ok && seamDsp.playbackFrames(0u) == 200u
        && firstWindowLeftStep < 0.25f
        && firstWindowRightStep < 0.25f;

    seamParams.loop1Start = 0.375f;
    seamParams.loop1End = 0.625f;
    seamDsp.setParams(seamParams);
    seamDsp.reset();
    render(seamDsp, seamSilence, seamSilence, outputLeft, outputRight);
    const float secondWindowLeftStep = maximumStep(outputLeft);
    const float secondWindowRightStep = maximumStep(outputRight);
    ok = ok && seamDsp.playbackFrames(0u) == 100u
        && secondWindowLeftStep < 0.25f
        && secondWindowRightStep < 0.25f;
    if (firstWindowLeftStep >= 0.25f || firstWindowRightStep >= 0.25f
        || secondWindowLeftStep >= 0.25f || secondWindowRightStep >= 0.25f) {
        std::cerr << "CRCLTR selected-window seam steps: "
                  << firstWindowLeftStep << ", " << firstWindowRightStep
                  << " / " << secondWindowLeftStep << ", "
                  << secondWindowRightStep << "\n";
    }

    // Live marker edits remain queued while the current pass completes. At
    // wrap the most recent window is adopted, with a short handoff from the
    // old boundary so neither the cursor nor audio jumps during marker drag.
    s3g::Crcltr liveEditDsp;
    ok = ok && liveEditDsp.prepare(kSampleRate, kBlockFrames)
        && liveEditDsp.restoreLoop(0u, seamSource.data(), seamSource.data(),
            static_cast<uint32_t>(seamSource.size()), kSampleRate);
    s3g::CrcltrParams liveEditParams;
    liveEditParams.playbackModel = s3g::CrcltrPlaybackModel::Dual;
    liveEditParams.blend = 1.0f;
    liveEditParams.crossfadeMode = s3g::CrcltrCrossfadeMode::Manual;
    liveEditParams.crossfade = 0.0f;
    liveEditParams.loop1Join = s3g::CrcltrLoopJoin::Seam;
    liveEditDsp.setParams(liveEditParams);
    liveEditDsp.reset();
    std::vector<float> leadIn(73u, 0.0f);
    render(liveEditDsp, leadIn, leadIn, outputLeft, outputRight);
    const float positionBeforeEdit = liveEditDsp.playbackPosition(0u);
    liveEditParams.loop1Start = 0.20f;
    liveEditParams.loop1End = 0.80f;
    liveEditDsp.setParams(liveEditParams);
    liveEditParams.loop1Start = 0.25f;
    liveEditParams.loop1End = 0.75f;
    liveEditDsp.setParams(liveEditParams);
    const bool liveEditQueued = liveEditDsp.loopWindowPending(0u)
        && liveEditDsp.playbackFrames(0u) == 400u
        && std::abs(liveEditDsp.activeLoopStart(0u)) < 0.0001f
        && std::abs(liveEditDsp.activeLoopEnd(0u) - 1.0f) < 0.0001f
        && std::abs(liveEditDsp.playbackPosition(0u)
            - positionBeforeEdit) < 0.0001f;
    ok = ok && liveEditQueued;
    std::vector<float> liveEditSilence(420u, 0.0f);
    render(liveEditDsp, liveEditSilence, liveEditSilence,
        outputLeft, outputRight);
    const float liveEditLeftStep = maximumStep(outputLeft);
    const float liveEditRightStep = maximumStep(outputRight);
    const bool liveEditCommitted = !liveEditDsp.loopWindowPending(0u)
        && liveEditDsp.playbackFrames(0u) == 200u
        && std::abs(liveEditDsp.activeLoopStart(0u) - 0.25f) < 0.0001f
        && std::abs(liveEditDsp.activeLoopEnd(0u) - 0.75f) < 0.0001f
        && liveEditLeftStep < 0.25f && liveEditRightStep < 0.25f;
    ok = ok && liveEditCommitted;
    if (!liveEditQueued || !liveEditCommitted) {
        std::cerr << "CRCLTR live-window edit: queued=" << liveEditQueued
                  << " committed=" << liveEditCommitted << " pending="
                  << liveEditDsp.loopWindowPending(0u) << " frames="
                  << liveEditDsp.playbackFrames(0u) << " active="
                  << liveEditDsp.activeLoopStart(0u) << ".."
                  << liveEditDsp.activeLoopEnd(0u) << " steps="
                  << liveEditLeftStep << ", " << liveEditRightStep << "\n";
    }

    // A host can commit a queued edit before processing resumes rather than
    // waiting for a wrap that cannot happen while its audio callback is idle.
    liveEditParams.loop1Start = 0.10f;
    liveEditParams.loop1End = 0.90f;
    liveEditDsp.setParams(liveEditParams);
    ok = ok && liveEditDsp.loopWindowPending(0u);
    liveEditDsp.applyPendingLoopWindows();
    ok = ok && !liveEditDsp.loopWindowPending(0u)
        && std::abs(liveEditDsp.activeLoopStart(0u) - 0.10f) < 0.0001f
        && std::abs(liveEditDsp.activeLoopEnd(0u) - 0.90f) < 0.0001f
        && liveEditDsp.playbackFrames(0u) == 320u;

    // Trapezoid remains a motion law rather than a gain shape: it travels in
    // both directions and holds briefly at A and B. Direction is published
    // for the vertical GUI read head.
    s3g::Crcltr motionDsp;
    ok = ok && motionDsp.prepare(kSampleRate, kBlockFrames)
        && motionDsp.restoreLoop(0u, seamSource.data(), seamSource.data(),
            static_cast<uint32_t>(seamSource.size()), kSampleRate);
    s3g::CrcltrParams motionParams;
    motionParams.playbackModel = s3g::CrcltrPlaybackModel::Dual;
    motionParams.crossfadeMode = s3g::CrcltrCrossfadeMode::Trapezoid;
    motionParams.crossfadeShape = s3g::CrcltrCrossfadeShape::Wide;
    motionParams.crossfade = 0.5f;
    motionDsp.setParams(motionParams);
    motionDsp.reset();
    bool movedTowardA = false;
    bool movedTowardB = false;
    bool heldAtA = false;
    bool heldAtB = false;
    for (uint32_t frame = 0u; frame < 450u; ++frame) {
        render(motionDsp, oneFrame, oneFrame, outputLeft, outputRight);
        const float direction = motionDsp.crossfadeDirection();
        const float position = motionDsp.currentCrossfade();
        movedTowardA = movedTowardA || direction < -0.5f;
        movedTowardB = movedTowardB || direction > 0.5f;
        heldAtA = heldAtA || (position < 0.01f
            && std::abs(direction) < 0.5f);
        heldAtB = heldAtB || (position > 0.99f
            && std::abs(direction) < 0.5f);
    }
    ok = ok && movedTowardA && movedTowardB && heldAtA && heldAtB;
    if (!(movedTowardA && movedTowardB && heldAtA && heldAtB)) {
        std::cerr << "CRCLTR trapezoid motion state: towardA="
                  << movedTowardA << " towardB=" << movedTowardB
                  << " holdA=" << heldAtA << " holdB=" << heldAtB << "\n";
    }

    // Motion falls back to Loop B's period and rate when A is empty, so
    // clearing A never freezes the crossfader LFO or its read-head display.
    s3g::Crcltr bClockDsp;
    ok = ok && bClockDsp.prepare(kSampleRate, kBlockFrames)
        && bClockDsp.restoreLoop(1u, seamSource.data(), seamSource.data(),
            static_cast<uint32_t>(seamSource.size()), kSampleRate);
    s3g::CrcltrParams bClockParams;
    bClockParams.playbackModel = s3g::CrcltrPlaybackModel::Dual;
    bClockParams.crossfadeMode = s3g::CrcltrCrossfadeMode::Sine;
    bClockParams.crossfade = 0.5f;
    bClockDsp.setParams(bClockParams);
    bClockDsp.reset();
    std::vector<float> bClockSilence(64u, 0.0f);
    render(bClockDsp, bClockSilence, bClockSilence,
        outputLeft, outputRight);
    ok = ok && bClockDsp.recordedFrames(0u) == 0u
        && bClockDsp.recordedFrames(1u) == seamSource.size()
        && std::abs(bClockDsp.currentCrossfade() - 0.5f) > 0.05f
        && std::abs(bClockDsp.crossfadeDirection()) > 0.5f;

    // The added periodic and stochastic laws stay bounded and exercise both
    // travel directions or held states as appropriate.
    for (uint32_t mode = 4u; mode <= 8u; ++mode) {
        bClockParams.crossfadeMode =
            static_cast<s3g::CrcltrCrossfadeMode>(mode);
        bClockDsp.setParams(bClockParams);
        bClockDsp.reset();
        float minimumPosition = 1.0f;
        float maximumPosition = 0.0f;
        for (uint32_t frame = 0u; frame < 900u; ++frame) {
            render(bClockDsp, oneFrame, oneFrame, outputLeft, outputRight);
            const float position = bClockDsp.currentCrossfade();
            minimumPosition = std::min(minimumPosition, position);
            maximumPosition = std::max(maximumPosition, position);
            ok = ok && std::isfinite(position)
                && position >= 0.0f && position <= 1.0f;
        }
        ok = ok && maximumPosition - minimumPosition > 0.1f;
    }

    // Either capture can be erased independently without disturbing the
    // other slot or requiring a replacement recording.
    s3g::Crcltr clearDsp;
    ok = ok && clearDsp.prepare(kSampleRate, kBlockFrames)
        && clearDsp.restoreLoop(0u, seamSource.data(), seamSource.data(),
            static_cast<uint32_t>(seamSource.size()), kSampleRate)
        && clearDsp.restoreLoop(1u, seamSource.data(), seamSource.data(),
            static_cast<uint32_t>(seamSource.size()), kSampleRate);
    clearDsp.clearLoop(0u);
    ok = ok && clearDsp.recordedFrames(0u) == 0u
        && clearDsp.playbackFrames(0u) == 0u
        && clearDsp.recordedFrames(1u) == seamSource.size();
    clearDsp.clearLoop(1u);
    ok = ok && clearDsp.recordedFrames(1u) == 0u
        && clearDsp.playbackFrames(1u) == 0u;

    // File clients install long decoded sources in bounded chunks. The loop
    // remains unavailable until the final commit, so playback cannot expose
    // a partially copied file.
    s3g::Crcltr importDsp;
    std::array<float, 8u> importLeft {{
        -0.8f, -0.6f, -0.4f, -0.2f, 0.2f, 0.4f, 0.6f, 0.8f,
    }};
    std::array<float, 8u> importRight {{
        0.8f, 0.6f, 0.4f, 0.2f, -0.2f, -0.4f, -0.6f, -0.8f,
    }};
    ok = ok && importDsp.prepare(kSampleRate, kBlockFrames)
        && importDsp.beginLoopImport(0u)
        && importDsp.writeLoopImport(0u, 0u, importLeft.data(),
            importRight.data(), 3u)
        && importDsp.writeLoopImport(0u, 3u, importLeft.data() + 3u,
            importRight.data() + 3u, 5u)
        && importDsp.recordedFrames(0u) == 0u
        && importDsp.finishLoopImport(0u,
            static_cast<uint32_t>(importLeft.size()))
        && importDsp.recordedFrames(0u) == importLeft.size()
        && std::abs(importDsp.loopSample(0u, 0u, 6u) - 0.6f) < 1.0e-6f
        && std::abs(importDsp.loopSample(0u, 1u, 6u) + 0.6f) < 1.0e-6f;

    // The exact same core can run against caller-owned Daisy SDRAM buffers.
    const uint32_t loopCapacity = s3g::Crcltr::requiredLoopCapacity(kSampleRate);
    const uint32_t preRollCapacity = s3g::Crcltr::requiredPreRollCapacity(kSampleRate);
    std::vector<float> loop1Left(loopCapacity);
    std::vector<float> loop1Right(loopCapacity);
    std::vector<float> loop2Left(loopCapacity);
    std::vector<float> loop2Right(loopCapacity);
    std::vector<float> pre1Left(preRollCapacity);
    std::vector<float> pre1Right(preRollCapacity);
    std::vector<float> pre2Left(preRollCapacity);
    std::vector<float> pre2Right(preRollCapacity);
    s3g::CrcltrMemory memory {
        loop1Left.data(), loop1Right.data(), loop2Left.data(), loop2Right.data(),
        loopCapacity,
        pre1Left.data(), pre1Right.data(), pre2Left.data(), pre2Right.data(),
        preRollCapacity,
    };
    s3g::Crcltr externalDsp;
    ok = ok && externalDsp.prepare(kSampleRate, kBlockFrames, &memory);
    ok = ok && externalDsp.usingExternalMemory();

    if (!ok) {
        std::cerr << "CRCLTR smoke test failed\n";
        return 1;
    }
    std::cout << "CRCLTR smoke test passed\n";
    return 0;
}
