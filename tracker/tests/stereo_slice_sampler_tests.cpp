#include "s3g/tracker/audio/stereo_slice_sampler_node.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string_view>

namespace {

using namespace s3g::tracker::audio;

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

bool near(float actual, float expected)
{
    return std::abs(actual - expected) < 1.0e-6f;
}

void testBackgroundAnalysisAndTransientSlices()
{
    StereoSampleAsset asset;
    asset.sampleRate = 1000.0;
    asset.left.assign(1000u, 0.0f);
    asset.right.assign(1000u, 0.0f);
    asset.left[100u] = 0.20f;
    asset.left[101u] = 0.90f;
    asset.left[102u] = 0.30f;
    asset.right[400u] = -0.20f;
    asset.right[401u] = -0.75f;
    asset.right[402u] = -0.25f;

    SampleAnalysisSettings settings;
    settings.maximumPeakCount = 100u;
    settings.minimumTransientLevel = 0.10f;
    settings.transientSensitivity = 1.5f;
    settings.minimumTransientSpacingSeconds = 0.050;
    settings.transientLookaheadSeconds = 0.004;
    const auto analysis = analyzeStereoSample(asset, settings);
    const auto repeated = analyzeStereoSample(asset, settings);
    check(analysis.validFor(asset) && analysis.peakStrideFrames == 10u
            && analysis.peaks.size() == 100u,
        "background peak analysis did not produce a bounded envelope");
    check(analysis.transients.size() == 2u
            && analysis.transients[0u].frame == 101u
            && analysis.transients[1u].frame == 401u
            && repeated.transients.size() == analysis.transients.size()
            && repeated.transients[0u].frame == analysis.transients[0u].frame
            && repeated.transients[1u].frame == analysis.transients[1u].frame,
        "deterministic transient analysis did not find the synthetic hits");
    check(near(analysis.peaks[10u].maximum, 0.90f)
            && near(analysis.peaks[40u].minimum, -0.75f),
        "stereo peak envelope did not retain both channel extrema");
    check(nearestStereoZeroFrame(asset, 101u, 3u) == 99u,
        "zero-frame snapping was not nearest and deterministic");

    const auto slices = makeTransientSampleSlices(asset, analysis, 8u, 3u);
    check(slices.size() == 3u && slices[0u].startFrame == 0u
            && slices[0u].endFrame == 99u
            && slices[1u].startFrame == 99u
            && slices[1u].endFrame == 399u
            && slices[2u].startFrame == 399u
            && slices[2u].endFrame == 1000u,
        "transient slicing did not publish snapped contiguous regions");
}

void testManualMarkersAndSliceTokens()
{
    std::array<SampleSlice, kMaximumSamplerSlices> slices {};
    std::size_t count = 1u;
    slices[0u] = { 0u, 1000u, 0.7f, true };
    check(addSampleSliceMarker(slices.data(), count, slices.size(), 250u)
            && addSampleSliceMarker(slices.data(), count, slices.size(), 750u)
            && count == 3u && slices[1u].gain == 0.7f
            && slices[1u].reverse,
        "manual marker insertion did not preserve non-destructive slice state");
    check(moveSampleSliceMarker(slices.data(), count, 1u, 200u)
            && slices[0u].endFrame == 200u
            && slices[1u].startFrame == 200u
            && !moveSampleSliceMarker(slices.data(), count, 0u, 100u),
        "manual marker drag did not preserve bounded contiguous slices");
    check(deleteSampleSliceMarker(slices.data(), count, 2u)
            && count == 2u && slices[1u].endFrame == 1000u,
        "manual marker deletion did not merge adjacent slices");

    uint8_t sliceIndex = 0u;
    uint8_t note = 0u;
    const auto token = formatSamplerSliceToken(127u);
    check(parseSamplerSliceToken("s042", sliceIndex) && sliceIndex == 42u
            && parseSamplerSliceToken("S7", sliceIndex) && sliceIndex == 7u
            && std::string_view(token.data()) == "S127"
            && !parseSamplerSliceToken("S128", sliceIndex)
            && !parseSamplerSliceToken("S2A", sliceIndex)
            && samplerNoteForSlice(36u, 20u, note) && note == 56u
            && !samplerNoteForSlice(120u, 8u, note),
        "decimal tracker slice token or note mapping seam was incorrect");
}

} // namespace

int main()
{
    testBackgroundAnalysisAndTransientSlices();
    testManualMarkersAndSliceTokens();

    auto asset = std::make_shared<StereoSampleAsset>();
    asset->sampleRate = 48000.0;
    asset->left.resize(256u);
    asset->right.resize(256u);
    for (std::size_t frame = 0u; frame < asset->left.size(); ++frame) {
        asset->left[frame] = 0.10f + static_cast<float>(frame) * 0.002f;
        asset->right[frame] = -asset->left[frame] * 0.5f;
    }
    check(asset->valid() && asset->frameCount() == 256u,
        "valid stereo asset was rejected");

    StereoSliceSamplerNode sampler;
    check(sampler.setAsset(asset), "sampler rejected an immutable asset");
    const std::array<SampleSlice, 2u> slices {{
        { 0u, 128u, 1.0f, false },
        { 128u, 256u, 0.5f, false },
    }};
    SamplerEnvelope transparent;
    transparent.attackMilliseconds = 0.0;
    transparent.decayMilliseconds = 0.0;
    transparent.sustain = 1.0f;
    transparent.releaseMilliseconds = 4.0;
    check(sampler.setSlices(slices.data(), slices.size())
            && sampler.setEnvelope(transparent)
            && sampler.sliceCount() == 2u && sampler.baseNote() == 36u,
        "sampler rejected a valid two-slice map");
    check(sampler.prepare({ 48000.0, 1u, 16u })
            && sampler.startProcessing(),
        "sampler lifecycle did not start");
    check(!sampler.setAsset(asset) && !sampler.setBaseNote(48u)
            && !sampler.setEnvelope(transparent),
        "sampler allowed control mutation while processing");

    std::array<float, 12u> left {};
    std::array<float, 12u> right {};
    std::array<float*, 2u> channels {{ left.data(), right.data() }};
    PlanarAudioBlock output { channels.data(), 2u,
        static_cast<uint32_t>(left.size()) };
    InstrumentRenderEvent onset;
    onset.frameOffset = 2u;
    onset.kind = InstrumentEventKind::NoteOn;
    onset.noteId = 1u;
    onset.key = 36;
    onset.value = 1.0;
    sampler.render(&onset, 1u, output);
    check(left[0u] == 0.0f && left[1u] == 0.0f
            && near(left[2u], 0.10f) && near(right[2u], -0.05f)
            && near(left[5u], 0.106f)
            && near(right[5u], -0.053f),
        "sample-offset stereo slice playback was incorrect");

    sampler.reset();
    onset.frameOffset = 0u;
    onset.noteId = 2u;
    onset.key = 37;
    sampler.render(&onset, 1u, output);
    check(near(left[0u], asset->left[128u] * 0.5f)
            && near(right[0u], asset->right[128u] * 0.5f)
            && near(left[3u], asset->left[131u] * 0.5f),
        "note-to-slice mapping or per-slice gain was incorrect");

    sampler.stopProcessing();
    auto reverseSlices = slices;
    reverseSlices[1u].reverse = true;
    reverseSlices[1u].gain = 1.0f;
    check(sampler.setSlices(reverseSlices.data(), reverseSlices.size())
            && sampler.startProcessing(),
        "reverse slice configuration failed");
    sampler.reset();
    onset.noteId = 3u;
    sampler.render(&onset, 1u, output);
    check(near(left[0u], asset->left[255u])
            && near(left[3u], asset->left[252u])
            && near(right[0u], asset->right[255u]),
        "reverse stereo slice playback was incorrect");

    sampler.stopProcessing();
    const std::array<SampleSlice, 1u> whole {{
        { 0u, 256u, 1.0f, false },
    }};
    check(sampler.setSlices(whole.data(), whole.size())
            && sampler.startProcessing(),
        "whole-sample release fixture configuration failed");
    sampler.reset();
    std::array<InstrumentRenderEvent, 2u> gated {{ onset, onset }};
    gated[0u].noteId = 4u;
    gated[0u].key = 36;
    gated[1u].frameOffset = 2u;
    gated[1u].kind = InstrumentEventKind::NoteOff;
    gated[1u].noteId = 4u;
    sampler.render(gated.data(), gated.size(), output);
    check(near(left[0u], asset->left[0u])
            && near(left[1u], asset->left[1u])
            && near(left[2u], asset->left[2u])
            && left[3u] > 0.0f && left[3u] < asset->left[3u],
        "note-off did not release continuously from the current level");

    sampler.stopProcessing();
    sampler.unprepare();
    check(!sampler.isProcessing(), "sampler did not stop cleanly");

    // A deliberately non-zero eight-frame glitch slice must still be useful:
    // the default attack makes its first sample exactly zero, while the
    // duration-capped natural tail makes its last sample exactly zero without
    // attenuating the entire slice into silence.
    auto glitch = std::make_shared<StereoSampleAsset>();
    glitch->sampleRate = 48000.0;
    glitch->left.assign(8u, 1.0f);
    StereoSliceSamplerNode glitchSampler;
    check(glitchSampler.setAsset(glitch)
            && glitchSampler.prepare({ 48000.0, 1u, 16u })
            && glitchSampler.startProcessing(),
        "short glitch sampler fixture did not start");
    std::array<float, 10u> glitchLeft {};
    std::array<float, 10u> glitchRight {};
    std::array<float*, 2u> glitchChannels {{
        glitchLeft.data(), glitchRight.data(),
    }};
    InstrumentRenderEvent glitchOn;
    glitchOn.kind = InstrumentEventKind::NoteOn;
    glitchOn.noteId = 20u;
    glitchOn.key = 36;
    glitchOn.value = 1.0;
    glitchSampler.render(&glitchOn, 1u,
        { glitchChannels.data(), 2u,
            static_cast<uint32_t>(glitchLeft.size()) });
    const float glitchPeak = *std::max_element(
        glitchLeft.begin(), glitchLeft.end());
    check(glitchLeft.front() == 0.0f && glitchLeft[7u] == 0.0f
            && glitchLeft[8u] == 0.0f && glitchPeak >= 0.75f,
        "short-slice de-clicking did not preserve a useful center peak");
    glitchSampler.stopProcessing();
    glitchSampler.unprepare();

    // Use a 1 kHz render rate so ADSR segment lengths are exact integers.
    auto constant = std::make_shared<StereoSampleAsset>();
    constant->sampleRate = 1000.0;
    constant->left.assign(100u, 1.0f);
    StereoSliceSamplerNode envelopeSampler;
    SamplerEnvelope shaped;
    shaped.attackMilliseconds = 4.0;
    shaped.decayMilliseconds = 4.0;
    shaped.sustain = 0.5f;
    shaped.releaseMilliseconds = 4.0;
    check(shaped.valid() && envelopeSampler.setAsset(constant)
            && envelopeSampler.setEnvelope(shaped)
            && envelopeSampler.prepare({ 1000.0, 1u, 32u })
            && envelopeSampler.startProcessing(),
        "ADSR sampler fixture did not start");
    std::array<float, 16u> envelopeLeft {};
    std::array<float, 16u> envelopeRight {};
    std::array<float*, 2u> envelopeChannels {{
        envelopeLeft.data(), envelopeRight.data(),
    }};
    InstrumentRenderEvent envelopeOn;
    envelopeOn.kind = InstrumentEventKind::NoteOn;
    envelopeOn.noteId = 30u;
    envelopeOn.key = 36;
    envelopeOn.value = 1.0;
    envelopeSampler.render(&envelopeOn, 1u,
        { envelopeChannels.data(), 2u,
            static_cast<uint32_t>(envelopeLeft.size()) });
    check(near(envelopeLeft[0u], 0.0f)
            && near(envelopeLeft[1u], 0.25f)
            && near(envelopeLeft[3u], 0.75f)
            && near(envelopeLeft[4u], 1.0f)
            && near(envelopeLeft[5u], 0.875f)
            && near(envelopeLeft[8u], 0.5f),
        "attack/decay/sustain stages were not sample deterministic");

    envelopeSampler.reset();
    std::array<InstrumentRenderEvent, 2u> releaseDuringAttack {{
        envelopeOn, envelopeOn,
    }};
    releaseDuringAttack[1u].frameOffset = 2u;
    releaseDuringAttack[1u].kind = InstrumentEventKind::NoteOff;
    releaseDuringAttack[1u].noteId = envelopeOn.noteId;
    envelopeSampler.render(releaseDuringAttack.data(),
        releaseDuringAttack.size(),
        { envelopeChannels.data(), 2u,
            static_cast<uint32_t>(envelopeLeft.size()) });
    check(near(envelopeLeft[2u], 0.5f)
            && near(envelopeLeft[3u], 0.375f)
            && near(envelopeLeft[4u], 0.25f)
            && near(envelopeLeft[5u], 0.125f)
            && envelopeLeft[6u] == 0.0f,
        "note-off did not release from the in-progress attack level");
    envelopeSampler.stopProcessing();
    envelopeSampler.unprepare();

    SamplerEnvelope invalid = shaped;
    invalid.sustain = std::numeric_limits<float>::quiet_NaN();
    StereoSliceSamplerNode validationSampler;
    check(!invalid.valid() && !validationSampler.setEnvelope(invalid),
        "a non-finite sampler envelope did not fail closed");

    if (failures == 0) std::cout << "stereo slice sampler tests passed\n";
    return failures == 0 ? 0 : 1;
}
