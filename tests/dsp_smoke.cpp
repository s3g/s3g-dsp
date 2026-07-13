#include "s3g_24ch_layout.h"
#include "s3g_3oafx.h"
#include "s3g_3oafx_single_effect.h"
#include "s3g_ambi_grain_processor.h"
#include "s3g_ambisonic_point_encoder.h"
#include "s3g_ambisonic_head_decoder.h"
#include "s3g_ambi_group_depth.h"
#include "s3g_ambi_group_matrix.h"
#include "s3g_ambi_group_matrix_128.h"
#include "s3g_ambi_group_rotate.h"
#include "s3g_ambisonic_utilities.h"
#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_ambisonic_stereo_decoder.h"
#include "s3g_buffer_processor.h"
#include "s3g_delay_processor.h"
#include "s3g_gain.h"
#include "s3g_group_matrix.h"
#include "s3g_group_matrix_32.h"
#include "s3g_lane_patch.h"
#include "s3g_loop_processor.h"
#include "s3g_multi_loop_processor.h"
#include "s3g_node_track_mixer.h"
#include "s3g_orbit_delay.h"
#include "s3g_cascade_taps.h"
#include "s3g_spectral_fft.h"
#include "s3g_spectral_spray.h"
#include "s3g_spectral_topology_processor.h"
#include "s3g_wave_geometry_processor.h"
#include "s3g_shard_scatter.h"
#include "s3g_layout_panner.h"
#include "s3g_mc_to_stereo.h"
#include "s3g_mc_to_quad.h"
#include "s3g_macro_delay.h"
#include "s3g_macro_pitch.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {

std::shared_ptr<s3g::LoopProcessorSample> makeIdSample(uint32_t sourceIndex,
                                                       uint32_t channels,
                                                       uint32_t frames = 32u,
                                                       double sampleRate = 48000.0)
{
    auto sample = std::make_shared<s3g::LoopProcessorSample>();
    sample->frames = std::max<uint32_t>(2u, frames);
    sample->channels = std::max<uint32_t>(1u, channels);
    sample->sampleRate = sampleRate;
    sample->path = "test-source-" + std::to_string(sourceIndex);
    sample->audio.assign(static_cast<size_t>(sample->frames) * sample->channels, 0.0f);
    for (uint32_t frame = 0; frame < sample->frames; ++frame) {
        for (uint32_t ch = 0; ch < sample->channels; ++ch) {
            sample->audio[static_cast<size_t>(frame) * sample->channels + ch] =
                static_cast<float>(sourceIndex * 1000u + ch);
        }
    }
    return sample;
}

std::shared_ptr<s3g::LoopProcessorSample> makeSineSample(uint32_t sourceIndex,
                                                         uint32_t channels,
                                                         uint32_t frames = 4096u,
                                                         double sampleRate = 48000.0)
{
    auto sample = std::make_shared<s3g::LoopProcessorSample>();
    sample->frames = std::max<uint32_t>(2u, frames);
    sample->channels = std::max<uint32_t>(1u, channels);
    sample->sampleRate = sampleRate;
    sample->path = "stress-source-" + std::to_string(sourceIndex);
    sample->audio.assign(static_cast<size_t>(sample->frames) * sample->channels, 0.0f);
    for (uint32_t frame = 0; frame < sample->frames; ++frame) {
        const float t = static_cast<float>(frame) / static_cast<float>(sample->sampleRate);
        for (uint32_t ch = 0; ch < sample->channels; ++ch) {
            const float hz = 80.0f + static_cast<float>(sourceIndex) * 47.0f + static_cast<float>(ch) * 9.0f;
            sample->audio[static_cast<size_t>(frame) * sample->channels + ch] =
                std::sin(6.28318530718f * hz * t) * 0.18f;
        }
    }
    return sample;
}

float sampleAt(const s3g::LoopProcessorSample& sample, uint32_t frame, uint32_t lane)
{
    return sample.audio[static_cast<size_t>(frame) * sample.channels + lane];
}

bool near(float a, float b, float tolerance = 0.0001f)
{
    return std::abs(a - b) <= tolerance;
}

} // namespace

int main()
{
    if (s3g::kVirtualDome24.size() != s3g::kVirtualSpeakerCount) {
        std::cerr << "Unexpected 24ch layout size\n";
        return 1;
    }

    float samples[2][4] = {
        {1.0f, 1.0f, 1.0f, 1.0f},
        {0.5f, 0.5f, 0.5f, 0.5f}
    };
    float* ptrs[2] = { samples[0], samples[1] };

    s3g::MultichannelGain gain;
    gain.prepare(48000.0f, 2, 1.0f);
    gain.setGainDb(-6.0f);
    gain.process(ptrs, 4);

    if (!std::isfinite(samples[0][3]) || samples[0][3] >= 1.0f) {
        std::cerr << "Gain smoothing did not reduce signal\n";
        return 1;
    }

    s3g::LanePatch patch;
    patch.setWidth(8);
    patch.setIdentity(2);
    if (!patch.connected(0, 0) || !patch.connected(1, 1) || patch.connected(2, 2)) {
        std::cerr << "LanePatch identity preset failed\n";
        return 1;
    }
    patch.toggle(7, 0);
    if (!patch.connected(7, 0)) {
        std::cerr << "LanePatch toggle failed\n";
        return 1;
    }

    s3g::AmbiGroupMatrix groupMatrix;
    groupMatrix.prepare(48000.0);
    constexpr uint32_t matrixFrames = 4;
    float matrixIn[s3g::kAmbiGroupMatrixChannels][matrixFrames] {};
    float matrixOut[s3g::kAmbiGroupMatrixChannels][matrixFrames] {};
    float* matrixInPtrs[s3g::kAmbiGroupMatrixChannels] {};
    float* matrixOutPtrs[s3g::kAmbiGroupMatrixChannels] {};
    for (uint32_t ch = 0; ch < s3g::kAmbiGroupMatrixChannels; ++ch) {
        matrixInPtrs[ch] = matrixIn[ch];
        matrixOutPtrs[ch] = matrixOut[ch];
        for (uint32_t frame = 0; frame < matrixFrames; ++frame) {
            matrixIn[ch][frame] = static_cast<float>(ch + 1u) * 0.01f + static_cast<float>(frame) * 0.001f;
        }
    }
    groupMatrix.process(matrixInPtrs, s3g::kAmbiGroupMatrixChannels, matrixOutPtrs, s3g::kAmbiGroupMatrixChannels, matrixFrames);
    if (!near(matrixOut[0][0], matrixIn[0][0]) || !near(matrixOut[17][2], matrixIn[17][2]) || !near(matrixOut[63][3], matrixIn[63][3])) {
        std::cerr << "Ambi group matrix identity routing failed\n";
        return 1;
    }
    auto matrixParams = s3g::makeDefaultAmbiGroupMatrixParams();
    matrixParams.crosspointDb[s3g::ambiGroupMatrixIndex(0, 1)] = 0.0f;
    matrixParams.crosspointDb[s3g::ambiGroupMatrixIndex(1, 1)] = -80.0f;
    groupMatrix.setParams(matrixParams);
    groupMatrix.reset();
    for (auto& ch : matrixOut) {
        std::fill(ch, ch + matrixFrames, 0.0f);
    }
    groupMatrix.process(matrixInPtrs, s3g::kAmbiGroupMatrixChannels, matrixOutPtrs, s3g::kAmbiGroupMatrixChannels, matrixFrames);
    if (!near(matrixOut[16][0], matrixIn[0][0]) || !near(matrixOut[31][3], matrixIn[15][3])) {
        std::cerr << "Ambi group matrix 3OA block routing failed\n";
        return 1;
    }
    matrixParams = s3g::makeDefaultAmbiGroupMatrixParams();
    for (float& db : matrixParams.crosspointDb) {
        db = -80.0f;
    }
    matrixParams.crosspointDb[s3g::ambiGroupMatrixIndex(0, 0)] = 0.0f;
    matrixParams.flow = 1.0f;
    matrixParams.spread = 1.0f;
    matrixParams.motion = 1.0f;
    groupMatrix.setParams(matrixParams);
    groupMatrix.setExternalPhase(0.25f);
    groupMatrix.reset();
    for (auto& ch : matrixIn) {
        std::fill(ch, ch + matrixFrames, 0.0f);
    }
    for (auto& ch : matrixOut) {
        std::fill(ch, ch + matrixFrames, 0.0f);
    }
    matrixIn[0][0] = 1.0f;
    groupMatrix.process(matrixInPtrs, s3g::kAmbiGroupMatrixChannels, matrixOutPtrs, s3g::kAmbiGroupMatrixChannels, matrixFrames);
    if (std::abs(matrixOut[16][0]) > 0.0001f) {
        std::cerr << "Ambi group matrix motion ignored a closed manual ceiling\n";
        return 1;
    }
    for (uint32_t shape = 0; shape <= static_cast<uint32_t>(s3g::MatrixFlowShape::Hold); ++shape) {
        matrixParams.shape = s3g::matrixFlowShapeFromIndex(shape);
        matrixParams.flow = 0.8f;
        matrixParams.spread = 0.55f;
        matrixParams.motion = 1.0f;
        groupMatrix.setParams(matrixParams);
        const auto preview = groupMatrix.generatedFlowPreview(0.37f);
        float sum = 0.0f;
        for (float v : preview) {
            if (!std::isfinite(v) || v < 0.0f) {
                std::cerr << "Ambi group matrix generated shape is invalid\n";
                return 1;
            }
            sum += v;
        }
        if (sum <= 0.0f) {
            std::cerr << "Ambi group matrix generated shape is silent\n";
            return 1;
        }
    }

    s3g::AmbiGroupMatrix128 groupMatrix128;
    groupMatrix128.prepare(48000.0);
    float matrix128In[s3g::kAmbiGroupMatrix128Channels][matrixFrames] {};
    float matrix128Out[s3g::kAmbiGroupMatrix128Channels][matrixFrames] {};
    float* matrix128InPtrs[s3g::kAmbiGroupMatrix128Channels] {};
    float* matrix128OutPtrs[s3g::kAmbiGroupMatrix128Channels] {};
    for (uint32_t ch = 0; ch < s3g::kAmbiGroupMatrix128Channels; ++ch) {
        matrix128InPtrs[ch] = matrix128In[ch];
        matrix128OutPtrs[ch] = matrix128Out[ch];
        for (uint32_t frame = 0; frame < matrixFrames; ++frame) {
            matrix128In[ch][frame] = static_cast<float>(ch + 1u) * 0.005f + static_cast<float>(frame) * 0.0005f;
        }
    }
    groupMatrix128.process(matrix128InPtrs, s3g::kAmbiGroupMatrix128Channels, matrix128OutPtrs, s3g::kAmbiGroupMatrix128Channels, matrixFrames);
    if (!near(matrix128Out[0][0], matrix128In[0][0]) || !near(matrix128Out[80][2], matrix128In[80][2]) || !near(matrix128Out[127][3], matrix128In[127][3])) {
        std::cerr << "Ambi group matrix 128 identity routing failed\n";
        return 1;
    }
    auto matrix128Params = s3g::makeDefaultAmbiGroupMatrix128Params();
    matrix128Params.crosspointDb[s3g::AmbiGroupMatrix128Index(0, 7)] = 0.0f;
    matrix128Params.crosspointDb[s3g::AmbiGroupMatrix128Index(7, 7)] = -80.0f;
    groupMatrix128.setParams(matrix128Params);
    groupMatrix128.reset();
    for (auto& ch : matrix128Out) {
        std::fill(ch, ch + matrixFrames, 0.0f);
    }
    groupMatrix128.process(matrix128InPtrs, s3g::kAmbiGroupMatrix128Channels, matrix128OutPtrs, s3g::kAmbiGroupMatrix128Channels, matrixFrames);
    if (!near(matrix128Out[112][0], matrix128In[0][0]) || !near(matrix128Out[127][3], matrix128In[15][3])) {
        std::cerr << "Ambi group matrix 128 3OA block routing failed\n";
        return 1;
    }
    matrix128Params = s3g::makeDefaultAmbiGroupMatrix128Params();
    for (float& db : matrix128Params.crosspointDb) {
        db = -80.0f;
    }
    matrix128Params.crosspointDb[s3g::AmbiGroupMatrix128Index(0, 0)] = 0.0f;
    matrix128Params.flow = 1.0f;
    matrix128Params.spread = 1.0f;
    matrix128Params.motion = 1.0f;
    groupMatrix128.setParams(matrix128Params);
    groupMatrix128.setExternalPhase(0.25f);
    groupMatrix128.reset();
    for (auto& ch : matrix128In) {
        std::fill(ch, ch + matrixFrames, 0.0f);
    }
    for (auto& ch : matrix128Out) {
        std::fill(ch, ch + matrixFrames, 0.0f);
    }
    matrix128In[0][0] = 1.0f;
    groupMatrix128.process(matrix128InPtrs, s3g::kAmbiGroupMatrix128Channels, matrix128OutPtrs, s3g::kAmbiGroupMatrix128Channels, matrixFrames);
    if (std::abs(matrix128Out[112][0]) > 0.0001f) {
        std::cerr << "Ambi group matrix 128 motion ignored a closed manual ceiling\n";
        return 1;
    }
    for (uint32_t shape = 0; shape <= static_cast<uint32_t>(s3g::MatrixFlowShape::Hold); ++shape) {
        matrix128Params.shape = s3g::matrixFlowShapeFromIndex(shape);
        matrix128Params.flow = 0.8f;
        matrix128Params.spread = 0.55f;
        matrix128Params.motion = 1.0f;
        groupMatrix128.setParams(matrix128Params);
        const auto preview = groupMatrix128.generatedFlowPreview(0.37f);
        float sum = 0.0f;
        for (float v : preview) {
            if (!std::isfinite(v) || v < 0.0f) {
                std::cerr << "Ambi group matrix 128 generated shape is invalid\n";
                return 1;
            }
            sum += v;
        }
        if (sum <= 0.0f) {
            std::cerr << "Ambi group matrix 128 generated shape is silent\n";
            return 1;
        }
    }

    s3g::GroupMatrix generalMatrix;
    generalMatrix.prepare(48000.0);
    float generalIn[s3g::kGroupMatrixChannels][matrixFrames] {};
    float generalOut[s3g::kGroupMatrixChannels][matrixFrames] {};
    float* generalInPtrs[s3g::kGroupMatrixChannels] {};
    float* generalOutPtrs[s3g::kGroupMatrixChannels] {};
    for (uint32_t ch = 0; ch < s3g::kGroupMatrixChannels; ++ch) {
        generalInPtrs[ch] = generalIn[ch];
        generalOutPtrs[ch] = generalOut[ch];
        for (uint32_t frame = 0; frame < matrixFrames; ++frame) {
            generalIn[ch][frame] = static_cast<float>(ch + 1u) * 0.02f + static_cast<float>(frame) * 0.001f;
        }
    }
    auto generalParams = s3g::makeDefaultGroupMatrixParams();
    generalParams.groupSize = s3g::GroupMatrixSize::Ch4;
    generalParams.crosspointDb[s3g::groupMatrixIndex(0, 1)] = 0.0f;
    generalParams.crosspointDb[s3g::groupMatrixIndex(1, 1)] = -80.0f;
    generalMatrix.setParams(generalParams);
    generalMatrix.reset();
    generalMatrix.process(generalInPtrs, s3g::kGroupMatrixChannels, generalOutPtrs, s3g::kGroupMatrixChannels, matrixFrames);
    if (!near(generalOut[4][0], generalIn[0][0]) || !near(generalOut[7][3], generalIn[3][3])) {
        std::cerr << "Group matrix 4ch group routing failed\n";
        return 1;
    }
    generalParams = s3g::makeDefaultGroupMatrixParams();
    generalParams.groupSize = s3g::GroupMatrixSize::Ch8;
    generalParams.crosspointDb[s3g::groupMatrixIndex(0, 1)] = 0.0f;
    generalParams.crosspointDb[s3g::groupMatrixIndex(1, 1)] = -80.0f;
    generalMatrix.setParams(generalParams);
    generalMatrix.reset();
    for (auto& ch : generalOut) {
        std::fill(ch, ch + matrixFrames, 0.0f);
    }
    generalMatrix.process(generalInPtrs, s3g::kGroupMatrixChannels, generalOutPtrs, s3g::kGroupMatrixChannels, matrixFrames);
    if (!near(generalOut[8][0], generalIn[0][0]) || !near(generalOut[15][2], generalIn[7][2])) {
        std::cerr << "Group matrix 8ch group routing failed\n";
        return 1;
    }
    generalParams = s3g::makeDefaultGroupMatrixParams();
    generalParams.groupSize = s3g::GroupMatrixSize::Ch16;
    generalParams.crosspointDb[s3g::groupMatrixIndex(0, 1)] = 0.0f;
    generalParams.crosspointDb[s3g::groupMatrixIndex(1, 1)] = -80.0f;
    generalMatrix.setParams(generalParams);
    generalMatrix.reset();
    for (auto& ch : generalOut) {
        std::fill(ch, ch + matrixFrames, 0.0f);
    }
    generalMatrix.process(generalInPtrs, s3g::kGroupMatrixChannels, generalOutPtrs, s3g::kGroupMatrixChannels, matrixFrames);
    if (!near(generalOut[16][0], generalIn[0][0]) || !near(generalOut[31][2], generalIn[15][2])) {
        std::cerr << "Group matrix 16ch group routing failed\n";
        return 1;
    }
    for (uint32_t shape = 0; shape <= static_cast<uint32_t>(s3g::MatrixFlowShape::Hold); ++shape) {
        generalParams.shape = s3g::matrixFlowShapeFromIndex(shape);
        generalParams.flow = 0.8f;
        generalParams.spread = 0.55f;
        generalParams.motion = 1.0f;
        generalMatrix.setParams(generalParams);
        const auto preview = generalMatrix.generatedFlowPreview(0.37f);
        float sum = 0.0f;
        for (float v : preview) {
            if (!std::isfinite(v) || v < 0.0f) {
                std::cerr << "Group matrix generated shape is invalid\n";
                return 1;
            }
            sum += v;
        }
        if (sum <= 0.0f) {
            std::cerr << "Group matrix generated shape is silent\n";
            return 1;
        }
    }

    s3g::GroupMatrix32 generalMatrix32;
    generalMatrix32.prepare(48000.0);
    float general32In[s3g::kGroupMatrix32Channels][matrixFrames] {};
    float general32Out[s3g::kGroupMatrix32Channels][matrixFrames] {};
    float* general32InPtrs[s3g::kGroupMatrix32Channels] {};
    float* general32OutPtrs[s3g::kGroupMatrix32Channels] {};
    for (uint32_t ch = 0; ch < s3g::kGroupMatrix32Channels; ++ch) {
        general32InPtrs[ch] = general32In[ch];
        general32OutPtrs[ch] = general32Out[ch];
        for (uint32_t frame = 0; frame < matrixFrames; ++frame) {
            general32In[ch][frame] = static_cast<float>(ch + 1u) * 0.025f + static_cast<float>(frame) * 0.001f;
        }
    }
    auto general32Params = s3g::makeDefaultGroupMatrix32Params();
    general32Params.groupSize = s3g::GroupMatrix32Size::Ch2;
    general32Params.crosspointDb[s3g::groupMatrix32Index(0, 1)] = 0.0f;
    general32Params.crosspointDb[s3g::groupMatrix32Index(1, 1)] = -80.0f;
    generalMatrix32.setParams(general32Params);
    generalMatrix32.reset();
    generalMatrix32.process(general32InPtrs, s3g::kGroupMatrix32Channels, general32OutPtrs, s3g::kGroupMatrix32Channels, matrixFrames);
    if (!near(general32Out[2][0], general32In[0][0]) || !near(general32Out[3][3], general32In[1][3])) {
        std::cerr << "Group matrix 32 2ch group routing failed\n";
        return 1;
    }
    general32Params = s3g::makeDefaultGroupMatrix32Params();
    general32Params.groupSize = s3g::GroupMatrix32Size::Ch4;
    general32Params.crosspointDb[s3g::groupMatrix32Index(0, 1)] = 0.0f;
    general32Params.crosspointDb[s3g::groupMatrix32Index(1, 1)] = -80.0f;
    generalMatrix32.setParams(general32Params);
    generalMatrix32.reset();
    for (auto& ch : general32Out) {
        std::fill(ch, ch + matrixFrames, 0.0f);
    }
    generalMatrix32.process(general32InPtrs, s3g::kGroupMatrix32Channels, general32OutPtrs, s3g::kGroupMatrix32Channels, matrixFrames);
    if (!near(general32Out[4][0], general32In[0][0]) || !near(general32Out[7][2], general32In[3][2])) {
        std::cerr << "Group matrix 32 4ch group routing failed\n";
        return 1;
    }
    general32Params = s3g::makeDefaultGroupMatrix32Params();
    general32Params.groupSize = s3g::GroupMatrix32Size::Ch8;
    general32Params.crosspointDb[s3g::groupMatrix32Index(0, 1)] = 0.0f;
    general32Params.crosspointDb[s3g::groupMatrix32Index(1, 1)] = -80.0f;
    generalMatrix32.setParams(general32Params);
    generalMatrix32.reset();
    for (auto& ch : general32Out) {
        std::fill(ch, ch + matrixFrames, 0.0f);
    }
    generalMatrix32.process(general32InPtrs, s3g::kGroupMatrix32Channels, general32OutPtrs, s3g::kGroupMatrix32Channels, matrixFrames);
    if (!near(general32Out[8][0], general32In[0][0]) || !near(general32Out[15][2], general32In[7][2])) {
        std::cerr << "Group matrix 32 8ch group routing failed\n";
        return 1;
    }
    for (uint32_t shape = 0; shape <= static_cast<uint32_t>(s3g::MatrixFlowShape::Hold); ++shape) {
        general32Params.shape = s3g::matrixFlowShapeFromIndex(shape);
        general32Params.flow = 0.8f;
        general32Params.spread = 0.55f;
        general32Params.motion = 1.0f;
        generalMatrix32.setParams(general32Params);
        const auto preview = generalMatrix32.generatedFlowPreview(0.37f);
        float sum = 0.0f;
        for (float v : preview) {
            if (!std::isfinite(v) || v < 0.0f) {
                std::cerr << "Group matrix 32 generated shape is invalid\n";
                return 1;
            }
            sum += v;
        }
        if (sum <= 0.0f) {
            std::cerr << "Group matrix 32 generated shape is silent\n";
            return 1;
        }
    }

    s3g::McStereoParams stereoParams;
    stereoParams.inputChannels = 8;
    stereoParams.layout = s3g::McStereoLayout::OddEvenStereo;
    stereoParams.autogain = s3g::McStereoAutogain::Off;
    stereoParams.widthPercent = 100.0f;
    float mcIn[s3g::kMcToStereoMaxInputChannels] {};
    float stereoOut[2] {};
    mcIn[0] = 1.0f;
    mcIn[1] = 1.0f;
    s3g::processMcToStereoFrame(mcIn, s3g::kMcToStereoMaxInputChannels, stereoOut, stereoParams);
    if (std::abs(stereoOut[0] - 1.0f) > 0.0001f || std::abs(stereoOut[1] - 1.0f) > 0.0001f) {
        std::cerr << "MC to Stereo odd/even fold-down failed\n";
        return 1;
    }
    stereoParams.inputChannels = 128;
    stereoParams.layout = s3g::McStereoLayout::SphereProjection;
    stereoParams.autogain = s3g::McStereoAutogain::PowerSqrtN;
    stereoParams.rotationDegrees = 37.0f;
    stereoParams.layoutWeightPercent = 73.0f;
    stereoParams.attenuation3dPercent = 62.0f;
    for (uint32_t ch = 0; ch < s3g::kMcToStereoMaxInputChannels; ++ch) {
        mcIn[ch] = std::sin(static_cast<float>(ch) * 0.37f) * 0.1f;
    }
    s3g::processMcToStereoFrame(mcIn, s3g::kMcToStereoMaxInputChannels, stereoOut, stereoParams);
    if (!std::isfinite(stereoOut[0]) || !std::isfinite(stereoOut[1])) {
        std::cerr << "MC to Stereo 128ch fold-down is not finite\n";
        return 1;
    }

    s3g::McQuadParams quadParams;
    quadParams.inputChannels = 4;
    quadParams.layout = s3g::McStereoLayout::RingProjection;
    quadParams.autogain = s3g::McStereoAutogain::Off;
    quadParams.widthPercent = 100.0f;
    float quadIn[s3g::kMcToStereoMaxInputChannels] {};
    float quadOut[s3g::kMcToQuadOutputChannels] {};
    quadIn[0] = 1.0f;
    quadIn[1] = 2.0f;
    quadIn[2] = 3.0f;
    quadIn[3] = 4.0f;
    s3g::processMcToQuadFrame(quadIn, 4u, quadOut, quadParams);
    const uint32_t quadMaxIndex = static_cast<uint32_t>(std::max_element(quadOut, quadOut + s3g::kMcToQuadOutputChannels) - quadOut);
    if (quadMaxIndex >= s3g::kMcToQuadOutputChannels || !std::isfinite(quadOut[0]) || !std::isfinite(quadOut[1]) || !std::isfinite(quadOut[2]) || !std::isfinite(quadOut[3])) {
        std::cerr << "MC to Quad 4ch fold-down is not finite\n";
        return 1;
    }
    std::array<s3g::McQuadChannelGains, s3g::kMcToStereoMaxInputChannels> quadGains {};
    s3g::makeMcToQuadGains(quadGains.data(), 4u, quadParams);
    if (quadGains[0].left <= quadGains[0].right || quadGains[1].right <= quadGains[1].left
        || quadGains[1].right <= quadGains[1].rightBack || quadGains[2].rightBack <= quadGains[2].right
        || quadGains[3].leftBack <= quadGains[3].left) {
        std::cerr << "MC to Quad output order/gains changed unexpectedly\n";
        return 1;
    }
    quadParams.inputChannels = 128;
    quadParams.layout = s3g::McStereoLayout::SphereProjection;
    quadParams.autogain = s3g::McStereoAutogain::PowerSqrtN;
    quadParams.rotationDegrees = -23.0f;
    quadParams.layoutWeightPercent = 85.0f;
    quadParams.attenuation3dPercent = 55.0f;
    for (uint32_t ch = 0; ch < s3g::kMcToStereoMaxInputChannels; ++ch) {
        quadIn[ch] = std::sin(static_cast<float>(ch) * 0.21f) * 0.08f;
    }
    s3g::processMcToQuadFrame(quadIn, s3g::kMcToStereoMaxInputChannels, quadOut, quadParams);
    for (float value : quadOut) {
        if (!std::isfinite(value)) {
            std::cerr << "MC to Quad 128ch fold-down is not finite\n";
            return 1;
        }
    }

    s3g::DelayProcessor delay;
    delay.prepare(1000.0, 2, 1.0);
    delay.setChannelDelayMs(0, 10.0f);
    delay.setChannelFeedback(0, 0.0f);
    delay.setChannelTone(0, 1.0f);
    float delayIn[2] {};
    float delayOut[2] {};
    delayIn[0] = 1.0f;
    delay.processFrame(delayIn, delayOut);
    delayIn[0] = 0.0f;
    for (int i = 0; i < 10; ++i) {
        delay.processFrame(delayIn, delayOut);
    }
    if (std::abs(delayOut[0] - 1.0f) > 0.0001f) {
        std::cerr << "Delay processor impulse timing failed\n";
        return 1;
    }

    s3g::DelayProcessor delayStress;
    delayStress.prepare(48000.0, 8, 2.25);
    for (int ch = 0; ch < 8; ++ch) {
        delayStress.setChannelDelayMs(ch, 80.0f + static_cast<float>(ch) * 41.0f);
        delayStress.setChannelFeedback(ch, 0.78f);
        delayStress.setChannelTone(ch, 0.45f);
        delayStress.setChannelNetwork(ch, 0.55f);
        delayStress.setChannelCharacter(ch, 0.85f);
        delayStress.setChannelSmearAmount(ch, 0.90f);
        delayStress.setChannelPitchSemitones(ch, ch % 2 == 0 ? 24.0f : -24.0f);
    }
    float delayStressIn[8] {};
    float delayStressOut[8] {};
    float delayStressPeak = 0.0f;
    for (int i = 0; i < 96000; ++i) {
        if (i == 24000 || i == 48000 || i == 72000) {
            for (int ch = 0; ch < 8; ++ch) {
                delayStress.setChannelDelayMs(ch, 140.0f + static_cast<float>((ch * 137 + i / 1000) % 1700));
            }
        }
        for (int ch = 0; ch < 8; ++ch) {
            delayStressIn[ch] = std::sin(static_cast<float>(i) * (0.011f + static_cast<float>(ch) * 0.0027f)) * 0.12f;
        }
        delayStress.processFrame(delayStressIn, delayStressOut);
        for (float value : delayStressOut) {
            if (!std::isfinite(value)) {
                std::cerr << "Delay processor stress output is not finite\n";
                return 1;
            }
            delayStressPeak = std::max(delayStressPeak, std::abs(value));
        }
    }
    if (delayStressPeak > 6.0f) {
        std::cerr << "Delay processor stress output exceeded safety expectation\n";
        return 1;
    }


    auto loopSample = std::make_shared<s3g::LoopProcessorSample>();
    loopSample->frames = 4096;
    loopSample->channels = 2;
    loopSample->sampleRate = 48000.0;
    loopSample->audio.assign(static_cast<size_t>(loopSample->frames) * loopSample->channels, 0.0f);
    for (uint32_t i = 0; i < loopSample->frames; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(loopSample->sampleRate);
        loopSample->audio[static_cast<size_t>(i) * 2u + 0u] = std::sin(6.28318530718f * 110.0f * t) * 0.25f;
        loopSample->audio[static_cast<size_t>(i) * 2u + 1u] = std::sin(6.28318530718f * 165.0f * t) * 0.25f;
    }
    s3g::LoopProcessorEngine loopProcessor;
    loopProcessor.prepare(48000.0);
    s3g::LoopProcessorParams loopParams;
    loopParams.baseRate = 1.0f;
    loopParams.rateSpread = 0.20f;
    loopParams.driftAmount = 0.03f;
    loopParams.xfadePct = 0.08f;
    loopParams.gainDb = -18.0f;
    loopProcessor.setParams(loopParams);
    std::array<std::array<float, 512>, s3g::kLoopProcessorChannels> loopBuffers {};
    float* loopOut[s3g::kLoopProcessorChannels] {};
    for (uint32_t ch = 0; ch < s3g::kLoopProcessorChannels; ++ch) {
        loopOut[ch] = loopBuffers[ch].data();
    }
    float loopPeak = 0.0f;
    for (int block = 0; block < 32; ++block) {
        loopProcessor.process(loopSample, loopOut, static_cast<uint32_t>(loopBuffers[0].size()));
        for (const auto& channel : loopBuffers) {
            for (float value : channel) {
                if (!std::isfinite(value)) {
                    std::cerr << "Loop Processor output is not finite\n";
                    return 1;
                }
                loopPeak = std::max(loopPeak, std::abs(value));
            }
        }
    }
    if (loopPeak <= 0.00001f || loopPeak > 1.0f) {
        std::cerr << "Loop Processor peak outside expected range: " << loopPeak << "\n";
        return 1;
    }
    float loopXfdStressPeak = 0.0f;
    for (int block = 0; block < 96; ++block) {
        if ((block % 8) == 0) {
            loopParams.xfadePct = static_cast<float>((block * 137) % 30) / 100.0f;
            loopProcessor.setParams(loopParams);
        }
        loopProcessor.process(loopSample, loopOut, static_cast<uint32_t>(loopBuffers[0].size()));
        for (const auto& channel : loopBuffers) {
            for (float value : channel) {
                if (!std::isfinite(value)) {
                    std::cerr << "Loop Processor XFD stress output is not finite\n";
                    return 1;
                }
                loopXfdStressPeak = std::max(loopXfdStressPeak, std::abs(value));
            }
        }
    }
    if (loopXfdStressPeak <= 0.00001f || loopXfdStressPeak > 1.0f) {
        std::cerr << "Loop Processor XFD stress peak outside expected range: " << loopXfdStressPeak << "\n";
        return 1;
    }

    auto discontinuousLoop = std::make_shared<s3g::LoopProcessorSample>();
    discontinuousLoop->frames = 2048u;
    discontinuousLoop->channels = s3g::kLoopProcessorChannels;
    discontinuousLoop->sampleRate = 48000.0;
    discontinuousLoop->audio.assign(static_cast<size_t>(discontinuousLoop->frames) * discontinuousLoop->channels, 0.0f);
    for (uint32_t frame = 0; frame < discontinuousLoop->frames; ++frame) {
        const float value = frame < discontinuousLoop->frames / 2u ? 0.8f : -0.8f;
        for (uint32_t ch = 0; ch < discontinuousLoop->channels; ++ch) {
            discontinuousLoop->audio[static_cast<size_t>(frame) * discontinuousLoop->channels + ch] = value;
        }
    }
    s3g::LoopProcessorEngine seamEngine;
    seamEngine.prepare(48000.0);
    s3g::LoopProcessorParams seamParams;
    seamParams.baseRate = 1.0f;
    seamParams.rateSpread = 0.0f;
    seamParams.driftAmount = 0.0f;
    seamParams.loopStart = 0.0f;
    seamParams.loopLength = 1.0f;
    seamParams.xfadePct = 0.12f;
    seamParams.seamDuck = 0.0f;
    seamParams.gainDb = -12.0f;
    seamEngine.setParams(seamParams);
    float seamMaxStep = 0.0f;
    float seamPrev = 0.0f;
    for (int block = 0; block < 12; ++block) {
        seamEngine.process(discontinuousLoop, loopOut, static_cast<uint32_t>(loopBuffers[0].size()));
        for (float value : loopBuffers[0]) {
            seamMaxStep = std::max(seamMaxStep, std::abs(value - seamPrev));
            seamPrev = value;
        }
    }
    if (seamMaxStep > 0.40f) {
        std::cerr << "Loop Processor seam step too large: " << seamMaxStep << "\n";
        return 1;
    }

    float loopRegionStressPeak = 0.0f;
    float loopRegionStressMaxStep = 0.0f;
    float loopRegionStressPrev = 0.0f;
    for (int block = 0; block < 160; ++block) {
        if ((block % 3) == 0) {
            loopParams.loopStart = static_cast<float>((block * 37) % 920) / 1000.0f;
            loopParams.loopLength = 0.18f + static_cast<float>((block * 53) % 760) / 1000.0f;
            loopProcessor.setParams(loopParams);
        }
        loopProcessor.process(loopSample, loopOut, static_cast<uint32_t>(loopBuffers[0].size()));
        for (const auto& channel : loopBuffers) {
            for (float value : channel) {
                if (!std::isfinite(value)) {
                    std::cerr << "Loop Processor region stress output is not finite\n";
                    return 1;
                }
                loopRegionStressPeak = std::max(loopRegionStressPeak, std::abs(value));
                loopRegionStressMaxStep = std::max(loopRegionStressMaxStep, std::abs(value - loopRegionStressPrev));
                loopRegionStressPrev = value;
            }
        }
    }
    if (loopRegionStressPeak <= 0.00001f || loopRegionStressPeak > 1.0f) {
        std::cerr << "Loop Processor region stress peak outside expected range: " << loopRegionStressPeak << "\n";
        return 1;
    }
    if (loopRegionStressMaxStep > 0.35f) {
        std::cerr << "Loop Processor region stress step too large: " << loopRegionStressMaxStep << "\n";
        return 1;
    }
    float loopCtrStressPeak = 0.0f;
    float loopCtrStressMaxStep = 0.0f;
    float loopCtrStressPrev = 0.0f;
    loopParams.loopStart = 0.18f;
    loopParams.loopLength = 0.32f;
    loopParams.relationGlideMs = 220.0f;
    for (int block = 0; block < 180; ++block) {
        loopParams.relationCenter = static_cast<float>((block * 41) % 100) / 100.0f;
        loopProcessor.setParams(loopParams);
        loopProcessor.process(loopSample, loopOut, static_cast<uint32_t>(loopBuffers[0].size()));
        for (const auto& channel : loopBuffers) {
            for (float value : channel) {
                if (!std::isfinite(value)) {
                    std::cerr << "Loop Processor CTR stress output is not finite\n";
                    return 1;
                }
                loopCtrStressPeak = std::max(loopCtrStressPeak, std::abs(value));
                loopCtrStressMaxStep = std::max(loopCtrStressMaxStep, std::abs(value - loopCtrStressPrev));
                loopCtrStressPrev = value;
            }
        }
    }
    if (loopCtrStressPeak <= 0.00001f || loopCtrStressPeak > 1.0f) {
        std::cerr << "Loop Processor CTR stress peak outside expected range: " << loopCtrStressPeak << "\n";
        return 1;
    }
    if (loopCtrStressMaxStep > 0.35f) {
        std::cerr << "Loop Processor CTR stress step too large: " << loopCtrStressMaxStep << "\n";
        return 1;
    }

    const uint32_t channelCountsToCheck[] = { 1u, 2u, 4u, 8u, 24u, 64u };
    for (uint32_t sourceChannels : channelCountsToCheck) {
        for (uint32_t lane = 0; lane < s3g::kLoopProcessorChannels; ++lane) {
            const uint32_t mapped = s3g::multiLoopSourceChannelForLane(lane, sourceChannels);
            if (mapped >= sourceChannels) {
                std::cerr << "Multi Loop source-channel map exceeded source channel count\n";
                return 1;
            }
            if (sourceChannels <= s3g::kLoopProcessorChannels && mapped != lane % sourceChannels) {
                std::cerr << "Multi Loop low-channel repeat map changed unexpectedly\n";
                return 1;
            }
        }
    }
    if (s3g::multiLoopSourceChannelForLane(0, 24u) != 0u
        || s3g::multiLoopSourceChannelForLane(7, 24u) != 23u
        || s3g::multiLoopSourceChannelForLane(4, 64u) != 36u) {
        std::cerr << "Multi Loop wide-channel distribution changed unexpectedly\n";
        return 1;
    }

    std::array<std::shared_ptr<const s3g::LoopProcessorSample>, s3g::kMultiLoopMaxSources> multiSources {};
    multiSources[0] = makeIdSample(0u, 1u);
    multiSources[1] = makeIdSample(1u, 2u);
    multiSources[2] = makeIdSample(2u, 4u);
    multiSources[3] = makeIdSample(3u, 24u);

    s3g::MultiLoopCompositeOptions multiOptions {};
    multiOptions.rule = s3g::MultiLoopSourceRule::Interleave;
    auto multiComposite = s3g::buildMultiLoopComposite(multiSources, 4u, multiOptions);
    if (!multiComposite || multiComposite->channels != s3g::kLoopProcessorChannels) {
        std::cerr << "Multi Loop interleave composite was not built\n";
        return 1;
    }
    const uint32_t assignmentFrame = 16u;
    for (uint32_t lane = 0; lane < s3g::kLoopProcessorChannels; ++lane) {
        const uint32_t source = lane % 4u;
        const uint32_t channel = s3g::multiLoopSourceChannelForLane(lane, multiSources[source]->channels);
        const float expected = static_cast<float>(source * 1000u + channel);
        if (!near(sampleAt(*multiComposite, assignmentFrame, lane), expected)) {
            std::cerr << "Multi Loop interleave source/lane assignment failed\n";
            return 1;
        }
    }

    multiOptions.rule = s3g::MultiLoopSourceRule::Order;
    multiComposite = s3g::buildMultiLoopComposite(multiSources, 4u, multiOptions);
    for (uint32_t lane = 0; lane < s3g::kLoopProcessorChannels; ++lane) {
        const uint32_t source = s3g::multiLoopOrderedSourceForLane(lane, 4u);
        const uint32_t channel = s3g::multiLoopSourceChannelForLane(lane, multiSources[source]->channels);
        const float expected = static_cast<float>(source * 1000u + channel);
        if (!near(sampleAt(*multiComposite, assignmentFrame, lane), expected)) {
            std::cerr << "Multi Loop ordered source/lane assignment failed\n";
            return 1;
        }
    }

    multiOptions.rule = s3g::MultiLoopSourceRule::Random;
    auto randomA = s3g::buildMultiLoopComposite(multiSources, 4u, multiOptions);
    auto randomB = s3g::buildMultiLoopComposite(multiSources, 4u, multiOptions);
    for (uint32_t lane = 0; lane < s3g::kLoopProcessorChannels; ++lane) {
        const uint32_t source = s3g::multiLoopHashLane(lane, 4u);
        const uint32_t channel = s3g::multiLoopSourceChannelForLane(lane, multiSources[source]->channels);
        const float expected = static_cast<float>(source * 1000u + channel);
        if (!near(sampleAt(*randomA, assignmentFrame, lane), expected)
            || !near(sampleAt(*randomA, assignmentFrame, lane), sampleAt(*randomB, assignmentFrame, lane))) {
            std::cerr << "Multi Loop random source/lane assignment was not deterministic\n";
            return 1;
        }
    }

    std::array<std::shared_ptr<const s3g::LoopProcessorSample>, s3g::kMultiLoopMaxSources> morphSources {};
    morphSources[0] = makeIdSample(0u, 1u);
    morphSources[1] = makeIdSample(1u, 1u);
    multiOptions.rule = s3g::MultiLoopSourceRule::Morph;
    multiOptions.sourceBlend = 1.0f;
    multiComposite = s3g::buildMultiLoopComposite(morphSources, 2u, multiOptions);
    if (!multiComposite
        || !near(sampleAt(*multiComposite, assignmentFrame, 0u), 0.0f)
        || !near(sampleAt(*multiComposite, assignmentFrame, 7u), 1000.0f)
        || sampleAt(*multiComposite, assignmentFrame, 3u) <= 0.0f
        || sampleAt(*multiComposite, assignmentFrame, 3u) >= 1000.0f) {
        std::cerr << "Multi Loop morph blend did not span sources as expected\n";
        return 1;
    }

    if (s3g::multiLoopSourceRateForIndex(0u, 4u, 1.0f) >= 1.0f
        || s3g::multiLoopSourceRateForIndex(3u, 4u, 1.0f) <= 1.0f
        || !near(s3g::multiLoopSourceRateForIndex(2u, 4u, 0.0f), 1.0f)) {
        std::cerr << "Multi Loop source-rate spread bounds changed unexpectedly\n";
        return 1;
    }

    s3g::LoopProcessorSample seamSource;
    seamSource.frames = 4096u;
    seamSource.channels = 1u;
    seamSource.sampleRate = 48000.0;
    seamSource.audio.assign(seamSource.frames, -1.0f);
    for (uint32_t i = 0; i < 512u; ++i) seamSource.audio[i] = 1.0f;
    const float seamBeforeWrap = s3g::multiLoopReadSourceSeam(seamSource, 0u, static_cast<double>(seamSource.frames - 1u));
    const float seamAfterWrap = s3g::multiLoopReadSourceSeam(seamSource, 0u, 0.0);
    if (std::abs(seamBeforeWrap - seamAfterWrap) > 0.20f) {
        std::cerr << "Multi Loop source seam smoothing failed: " << seamBeforeWrap << " / " << seamAfterWrap << "\n";
        return 1;
    }

    std::array<std::shared_ptr<const s3g::LoopProcessorSample>, s3g::kMultiLoopMaxSources> stressSources {};
    stressSources[0] = makeSineSample(0u, 1u);
    stressSources[1] = makeSineSample(1u, 2u);
    stressSources[2] = makeSineSample(2u, 4u);
    stressSources[3] = makeSineSample(3u, 24u);
    multiOptions.rule = s3g::MultiLoopSourceRule::Morph;
    multiOptions.sourceRateSpread = 0.35f;
    multiOptions.sourceBlend = 0.70f;
    auto stressComposite = s3g::buildMultiLoopComposite(stressSources, 4u, multiOptions);
    s3g::LoopProcessorEngine multiLoopEngine;
    multiLoopEngine.prepare(48000.0);
    s3g::LoopProcessorParams multiLoopParams;
    multiLoopParams.baseRate = 0.90f;
    multiLoopParams.rateSpread = -0.18f;
    multiLoopParams.driftAmount = 0.04f;
    multiLoopParams.relationCenter = 0.35f;
    multiLoopParams.relationGlideMs = 180.0f;
    multiLoopParams.loopStart = 0.12f;
    multiLoopParams.loopLength = 0.45f;
    multiLoopParams.xfadePct = 0.16f;
    multiLoopParams.seamDuck = 0.18f;
    multiLoopParams.gainDb = -18.0f;
    multiLoopEngine.setParams(multiLoopParams);
    float multiLoopPeak = 0.0f;
    float multiLoopMaxStep = 0.0f;
    float multiLoopPrev = 0.0f;
    for (int block = 0; block < 96; ++block) {
        if ((block % 12) == 0) {
            multiLoopParams.loopStart = static_cast<float>((block * 19) % 800) / 1000.0f;
            multiLoopParams.loopLength = 0.18f + static_cast<float>((block * 23) % 620) / 1000.0f;
            multiLoopParams.relationCenter = static_cast<float>((block * 17) % 100) / 100.0f;
            multiLoopEngine.setParams(multiLoopParams);
        }
        multiLoopEngine.process(stressComposite, loopOut, static_cast<uint32_t>(loopBuffers[0].size()));
        for (const auto& channel : loopBuffers) {
            for (float value : channel) {
                if (!std::isfinite(value)) {
                    std::cerr << "Multi Loop engine stress output is not finite\n";
                    return 1;
                }
                multiLoopPeak = std::max(multiLoopPeak, std::abs(value));
                multiLoopMaxStep = std::max(multiLoopMaxStep, std::abs(value - multiLoopPrev));
                multiLoopPrev = value;
            }
        }
    }
    if (multiLoopPeak <= 0.00001f || multiLoopPeak > 1.0f) {
        std::cerr << "Multi Loop engine stress peak outside expected range: " << multiLoopPeak << "\n";
        return 1;
    }
    if (multiLoopMaxStep > 0.40f) {
        std::cerr << "Multi Loop engine stress step too large: " << multiLoopMaxStep << "\n";
        return 1;
    }

    s3g::MacroDelay macroDelay;
    macroDelay.prepare(48000.0, s3g::kMacroDelayChannels, 2.5);
    s3g::MacroDelayParams macroParams;
    macroParams.timeMs = 75.0f;
    macroParams.feedback = 0.42f;
    macroParams.tone = 0.55f;
    macroParams.character = 0.35f;
    macroParams.smear = 0.20f;
    macroParams.spread = 0.35f;
    macroParams.deviation = 0.25f;
    macroParams.skew = 0.20f;
    macroParams.mix = 0.65f;
    macroParams.outputGainDb = -3.0f;
    macroDelay.setParams(macroParams);
    float macroIn[s3g::kMacroDelayChannels] {};
    float macroOut[s3g::kMacroDelayChannels] {};
    float macroPeak = 0.0f;
    float macroTailPeak = 0.0f;
    for (int i = 0; i < 48000; ++i) {
        for (uint32_t ch = 0; ch < s3g::kMacroDelayChannels; ++ch) {
            macroIn[ch] = (i == static_cast<int>(ch * 127u)) ? 0.5f : 0.0f;
        }
        macroDelay.processFrame(macroIn, macroOut);
        for (float value : macroOut) {
            if (!std::isfinite(value)) {
                std::cerr << "Macro Delay output is not finite\n";
                return 1;
            }
            macroPeak = std::max(macroPeak, std::abs(value));
            if (i > 12000) {
                macroTailPeak = std::max(macroTailPeak, std::abs(value));
            }
        }
    }
    if (macroPeak <= 0.00001f || macroPeak > 1.01f) {
        std::cerr << "Macro Delay peak outside expected range: " << macroPeak << "\n";
        return 1;
    }
    if (macroTailPeak <= 0.000001f) {
        std::cerr << "Macro Delay did not preserve a silence-fed tail\n";
        return 1;
    }

    s3g::MacroPitch macroPitch;
    macroPitch.prepare(48000.0, s3g::kMacroPitchChannels);
    s3g::MacroPitchParams macroPitchParams;
    macroPitchParams.pitchSemitones = 7.0f;
    macroPitchParams.fineCents = -8.0f;
    macroPitchParams.windowMs = 72.0f;
    macroPitchParams.spread = 0.22f;
    macroPitchParams.deviation = 0.18f;
    macroPitchParams.skew = -0.12f;
    macroPitchParams.center = 0.55f;
    macroPitchParams.glideMs = 120.0f;
    macroPitchParams.mix = 0.65f;
    macroPitchParams.outputGainDb = -6.0f;
    macroPitch.setParams(macroPitchParams);
    float macroPitchIn[s3g::kMacroPitchChannels] {};
    float macroPitchOut[s3g::kMacroPitchChannels] {};
    float macroPitchPeak = 0.0f;
    for (int i = 0; i < 24000; ++i) {
        for (uint32_t ch = 0; ch < s3g::kMacroPitchChannels; ++ch) {
            macroPitchIn[ch] = std::sin(6.28318530718f * (110.0f + static_cast<float>(ch) * 17.0f) * static_cast<float>(i) / 48000.0f) * 0.18f;
        }
        macroPitch.processFrame(macroPitchIn, macroPitchOut);
        for (float value : macroPitchOut) {
            if (!std::isfinite(value)) {
                std::cerr << "Macro Pitch output is not finite\n";
                return 1;
            }
            macroPitchPeak = std::max(macroPitchPeak, std::abs(value));
        }
    }
    if (macroPitchPeak <= 0.00001f || macroPitchPeak > 1.0f) {
        std::cerr << "Macro Pitch peak outside expected range: " << macroPitchPeak << "\n";
        return 1;
    }

    s3g::BufferProcessor bufferProcessor;
    bufferProcessor.prepare(48000.0, s3g::kBufferProcessorChannels, 4.0);
    s3g::BufferProcessorParams bufferParams;
    bufferParams.sizeMs = 70.0f;
    bufferParams.rate = 1.35f;
    bufferParams.crossfade = 0.18f;
    bufferParams.repeat = 0.82f;
    bufferParams.skip = 0.55f;
    bufferParams.skipGrid = 3.0f;
    bufferParams.skipJam = 0.44f;
    bufferParams.skipChase = 0.36f;
    bufferParams.skipSync = 1.0f;
    bufferParams.reverse = 0.35f;
    bufferParams.crush = 0.18f;
    bufferParams.error = 0.28f;
    bufferParams.errorMode = 1.0f;
    bufferParams.memory = 0.0f;
    bufferParams.spread = 0.42f;
    bufferParams.deviation = 0.35f;
    bufferParams.skew = -0.18f;
    bufferParams.center = 0.48f;
    bufferParams.glideMs = 90.0f;
    bufferParams.mix = 0.72f;
    bufferParams.outputGainDb = -6.0f;
    bufferProcessor.setTransport(96.0, true);
    bufferProcessor.setParams(bufferParams);
    float bufferIn[s3g::kBufferProcessorChannels] {};
    float bufferOut[s3g::kBufferProcessorChannels] {};
    float bufferPeak = 0.0f;
    float bufferMaxStep = 0.0f;
    std::array<float, s3g::kBufferProcessorChannels> bufferLast {};
    for (int i = 0; i < 48000; ++i) {
        if (i == 18000) {
            bufferParams.sizeMs = 34.0f;
            bufferParams.rate = -0.85f;
            bufferParams.skip = 0.82f;
            bufferParams.skipGrid = 4.0f;
            bufferParams.skipJam = 0.70f;
            bufferParams.skipChase = 0.68f;
            bufferParams.reverse = 0.70f;
            bufferParams.crush = 0.34f;
            bufferParams.error = 0.52f;
            bufferParams.errorMode = 4.0f;
            bufferParams.memory = 0.24f;
            bufferParams.deviation = 0.58f;
            bufferProcessor.captureMemory();
            bufferProcessor.setParams(bufferParams);
        }
        for (uint32_t ch = 0; ch < s3g::kBufferProcessorChannels; ++ch) {
            const float t = static_cast<float>(i) / 48000.0f;
            bufferIn[ch] = (std::sin(6.28318530718f * (83.0f + static_cast<float>(ch) * 29.0f) * t)
                + 0.35f * std::sin(6.28318530718f * (670.0f + static_cast<float>(ch) * 47.0f) * t)) * 0.16f;
        }
        bufferProcessor.processFrame(bufferIn, bufferOut);
        for (uint32_t ch = 0; ch < s3g::kBufferProcessorChannels; ++ch) {
            const float value = bufferOut[ch];
            if (!std::isfinite(value)) {
                std::cerr << "Buffer Processor output is not finite\n";
                return 1;
            }
            bufferPeak = std::max(bufferPeak, std::abs(value));
            bufferMaxStep = std::max(bufferMaxStep, std::abs(value - bufferLast[ch]));
            bufferLast[ch] = value;
        }
    }
    if (bufferPeak <= 0.00001f || bufferPeak > 1.0f || bufferMaxStep > 0.90f) {
        std::cerr << "Buffer Processor peak/step outside expected range: " << bufferPeak << " / " << bufferMaxStep << "\n";
        return 1;
    }
    bufferProcessor.reset();
    bufferParams.skip = 0.0f;
    bufferParams.skipJam = 0.0f;
    bufferParams.skipChase = 0.0f;
    bufferParams.error = 1.0f;
    bufferParams.errorMode = 4.0f;
    bufferParams.memory = 0.0f;
    bufferParams.mix = 1.0f;
    bufferProcessor.setParams(bufferParams);
    float bufferSilencePeak = 0.0f;
    std::fill(std::begin(bufferIn), std::end(bufferIn), 0.0f);
    for (int i = 0; i < 12000; ++i) {
        bufferProcessor.processFrame(bufferIn, bufferOut);
        for (float value : bufferOut) {
            if (!std::isfinite(value)) {
                std::cerr << "Buffer Processor silence output is not finite\n";
                return 1;
            }
            bufferSilencePeak = std::max(bufferSilencePeak, std::abs(value));
        }
    }
    if (bufferSilencePeak > 0.000001f) {
        std::cerr << "Buffer Processor ERR produced a silence noise floor: " << bufferSilencePeak << "\n";
        return 1;
    }

    s3g::AmbiPointEncoder pointEncoder;
    pointEncoder.prepare(48000.0, s3g::kAmbiPointEncoderPrototypePoints);
    s3g::AmbiPointEncoderParams pointParams;
    pointParams.activePoints = s3g::kAmbiPointEncoderPrototypePoints;
    pointParams.selectedPoint = 0;
    pointParams.selectedAzimuthDeg = -35.0f;
    pointParams.selectedElevationDeg = 18.0f;
    pointParams.selectedDistance = 0.85f;
    pointParams.motionMode = s3g::AmbiPointMotionMode::Orbit;
    pointParams.motionAmount = 0.35f;
    pointParams.rateHz = 0.04f;
    pointParams.swirl = 0.05f;
    pointParams.outputGainDb = -12.0f;
    pointEncoder.setParams(pointParams);
    std::array<std::array<float, 512>, s3g::kAmbiPointEncoderPrototypePoints> pointInputBuffers {};
    std::array<std::array<float, 512>, s3g::k3OaChannels> pointOutputBuffers {};
    std::array<const float*, s3g::kAmbiPointEncoderPrototypePoints> pointInputs {};
    std::array<float*, s3g::k3OaChannels> pointOutputs {};
    for (uint32_t ch = 0; ch < s3g::kAmbiPointEncoderPrototypePoints; ++ch) {
        pointInputs[ch] = pointInputBuffers[ch].data();
    }
    for (uint32_t ch = 0; ch < s3g::k3OaChannels; ++ch) {
        pointOutputs[ch] = pointOutputBuffers[ch].data();
    }
    float pointEncoderPeak = 0.0f;
    for (int block = 0; block < 32; ++block) {
        for (uint32_t ch = 0; ch < s3g::kAmbiPointEncoderPrototypePoints; ++ch) {
            for (uint32_t i = 0; i < pointInputBuffers[ch].size(); ++i) {
                pointInputBuffers[ch][i] = std::sin(6.28318530718f * (90.0f + ch * 11.0f) * static_cast<float>(block * 512 + i) / 48000.0f) * 0.03f;
            }
        }
        pointEncoder.processBlock(pointInputs.data(), pointOutputs.data(), static_cast<uint32_t>(pointOutputBuffers[0].size()));
        for (const auto& channel : pointOutputBuffers) {
            for (float value : channel) {
                if (!std::isfinite(value)) {
                    std::cerr << "Ambi Point Encoder output is not finite\n";
                    return 1;
                }
                pointEncoderPeak = std::max(pointEncoderPeak, std::abs(value));
            }
        }
    }
    if (pointEncoderPeak <= 0.00001f || pointEncoderPeak > 1.0f) {
        std::cerr << "Ambi Point Encoder peak outside expected range: " << pointEncoderPeak << "\n";
        return 1;
    }

    s3g::AmbiSpeakerDecoder speakerDecoder;
    speakerDecoder.prepare(48000.0);
    s3g::AmbiSpeakerDecoderParams speakerParams;
    speakerParams.layout = s3g::AmbiSpeakerLayoutPreset::Sphere24;
    speakerParams.mode = s3g::AmbiSpeakerDecoderMode::Epad;
    speakerParams.weighting = s3g::AmbiSpeakerDecoderWeighting::MaxRe;
    speakerParams.order = 7;
    speakerParams.outputGainDb = -12.0f;
    speakerDecoder.setParams(speakerParams);
    float decoderIn[s3g::kAmbiSpeakerDecoderMaxChannels] {};
    float decoderOut[s3g::kAmbiSpeakerDecoderMaxSpeakers] {};
    decoderIn[0] = 0.25f;
    decoderIn[1] = 0.05f;
    decoderIn[7] = -0.04f;
    decoderIn[31] = 0.02f;
    decoderIn[63] = -0.01f;
    speakerDecoder.processFrame(decoderIn, decoderOut);
    float speakerDecoderPeak = 0.0f;
    for (uint32_t ch = 0; ch < s3g::kAmbiSpeakerDecoderMaxSpeakers; ++ch) {
        if (!std::isfinite(decoderOut[ch])) {
            std::cerr << "Ambi Speaker Decoder output is not finite\n";
            return 1;
        }
        speakerDecoderPeak = std::max(speakerDecoderPeak, std::abs(decoderOut[ch]));
    }
    if (speakerDecoderPeak <= 0.000001f || speakerDecoderPeak > 1.0f) {
        std::cerr << "Ambi Speaker Decoder peak outside expected range: " << speakerDecoderPeak << "\n";
        return 1;
    }
    speakerParams.weighting = s3g::AmbiSpeakerDecoderWeighting::InPhase;
    speakerDecoder.setParams(speakerParams);
    speakerDecoder.processFrame(decoderIn, decoderOut);
    for (uint32_t ch = 0; ch < s3g::kAmbiSpeakerDecoderMaxSpeakers; ++ch) {
        if (!std::isfinite(decoderOut[ch])) {
            std::cerr << "Ambi Speaker Decoder weighted output is not finite\n";
            return 1;
        }
    }
    speakerParams.layout = s3g::AmbiSpeakerLayoutPreset::Quad;
    speakerParams.order = 1;
    speakerDecoder.setParams(speakerParams);
    speakerDecoder.processFrame(decoderIn, decoderOut);
    for (uint32_t ch = 4; ch < s3g::kAmbiSpeakerDecoderMaxSpeakers; ++ch) {
        if (std::abs(decoderOut[ch]) > 0.000001f) {
            std::cerr << "Ambi Speaker Decoder did not zero inactive speakers\n";
            return 1;
        }
    }
    speakerParams.layout = s3g::AmbiSpeakerLayoutPreset::QuadOverhead6;
    speakerDecoder.setParams(speakerParams);
    if (std::abs(speakerDecoder.speaker(0).azimuthDeg - -45.0f) > 0.001f
        || std::abs(speakerDecoder.speaker(4).azimuthDeg - -90.0f) > 0.001f
        || std::abs(speakerDecoder.speaker(5).azimuthDeg - 90.0f) > 0.001f) {
        std::cerr << "Ambi Speaker Decoder quad+overhead order changed unexpectedly\n";
        return 1;
    }
    speakerParams.layout = s3g::AmbiSpeakerLayoutPreset::Dome24;
    speakerDecoder.setParams(speakerParams);
    if (std::abs(speakerDecoder.speaker(0).azimuthDeg - -30.0f) > 0.001f
        || std::abs(speakerDecoder.speaker(2).azimuthDeg - -90.0f) > 0.001f
        || std::abs(speakerDecoder.speaker(12).elevationDeg - 32.0f) > 0.001f
        || std::abs(speakerDecoder.speaker(20).elevationDeg - 66.6f) > 0.001f) {
        std::cerr << "Ambi Speaker Decoder dome spiral order changed unexpectedly\n";
        return 1;
    }
    auto speakerToWorld = [](const s3g::AmbiSpeaker& speaker) {
        const auto dir = s3g::directionFromAed(speaker.azimuthDeg, speaker.elevationDeg);
        return s3g::Vec3 {
            dir.x * speaker.distance,
            dir.y * speaker.distance,
            dir.z * speaker.distance
        };
    };
    speakerParams.layout = s3g::AmbiSpeakerLayoutPreset::Cube17;
    speakerDecoder.setParams(speakerParams);
    const auto cubeLower = speakerToWorld(speakerDecoder.speaker(0));
    const auto cubeMiddle = speakerToWorld(speakerDecoder.speaker(4));
    const auto cubeUpper = speakerToWorld(speakerDecoder.speaker(12));
    if (std::abs(cubeLower.x - cubeMiddle.x) > 0.001f
        || std::abs(cubeLower.y - cubeMiddle.y) > 0.001f
        || std::abs(cubeUpper.x - cubeMiddle.x) > 0.001f
        || std::abs(cubeUpper.y - cubeMiddle.y) > 0.001f
        || std::sqrt(cubeLower.x * cubeLower.x + cubeLower.y * cubeLower.y + cubeLower.z * cubeLower.z)
            <= std::sqrt(cubeMiddle.x * cubeMiddle.x + cubeMiddle.y * cubeMiddle.y + cubeMiddle.z * cubeMiddle.z)) {
        std::cerr << "Ambi Speaker Decoder CUBE17 tier projection changed unexpectedly\n";
        return 1;
    }
    const auto cubePresetSpeaker0 = speakerDecoder.speaker(0);
    speakerParams.selectedSpeaker = 0;
    speakerParams.selectedAzimuthDeg = 0.0f;
    speakerParams.selectedElevationDeg = 0.0f;
    speakerParams.selectedDistance = 1.0f;
    speakerParams.width = 0.72f;
    speakerDecoder.setParams(speakerParams);
    if (std::abs(speakerDecoder.speaker(0).azimuthDeg - cubePresetSpeaker0.azimuthDeg) > 0.001f
        || std::abs(speakerDecoder.speaker(0).elevationDeg - cubePresetSpeaker0.elevationDeg) > 0.001f
        || std::abs(speakerDecoder.speaker(0).distance - cubePresetSpeaker0.distance) > 0.001f) {
        std::cerr << "Ambi Speaker Decoder preset geometry was changed by editable speaker fields\n";
        return 1;
    }
    speakerParams.layout = s3g::AmbiSpeakerLayoutPreset::Custom;
    speakerParams.activeSpeakers = 13;
    speakerParams.customField = s3g::AmbiSpeakerCustomField::Hemisphere;
    speakerDecoder.setParams(speakerParams);
    if (speakerDecoder.params().activeSpeakers != 13u) {
        std::cerr << "Ambi Speaker Decoder custom speaker count failed\n";
        return 1;
    }
    for (uint32_t i = 0; i < 13u; ++i) {
        if (speakerDecoder.speaker(i).elevationDeg < -0.001f) {
            std::cerr << "Ambi Speaker Decoder hemisphere custom layout dipped below horizon\n";
            return 1;
        }
    }
    speakerParams.activeSpeakers = 14;
    speakerParams.customField = s3g::AmbiSpeakerCustomField::FullSphere;
    speakerDecoder.setParams(speakerParams);
    bool fullSphereHasLower = false;
    for (uint32_t i = 0; i < 14u; ++i) {
        fullSphereHasLower = fullSphereHasLower || speakerDecoder.speaker(i).elevationDeg < -0.001f;
    }
    if (!fullSphereHasLower) {
        std::cerr << "Ambi Speaker Decoder full-sphere custom layout did not use lower hemisphere\n";
        return 1;
    }

    s3g::AmbiStereoDecoder ambiStereo;
    ambiStereo.prepare(48000.0);
    s3g::AmbiStereoParams ambiStereoParams;
    ambiStereoParams.order = 7;
    ambiStereoParams.layout = s3g::AmbiStereoVirtualLayout::Dome24;
    ambiStereoParams.method = s3g::AmbiStereoMethod::XyCardioid;
    ambiStereoParams.weighting = s3g::AmbiStereoWeighting::EnergyNormalized;
    ambiStereoParams.outputGainDb = -6.0f;
    ambiStereo.setParams(ambiStereoParams);
    float ambiStereoIn[s3g::kAmbiStereoDecoderMaxChannels] {};
    float ambiStereoLeft = 0.0f;
    float ambiStereoRight = 0.0f;
    ambiStereoIn[0] = 0.25f;
    ambiStereoIn[1] = 0.04f;
    ambiStereoIn[3] = 0.06f;
    ambiStereoIn[15] = -0.02f;
    ambiStereoIn[63] = 0.01f;
    ambiStereo.processFrame(ambiStereoIn, ambiStereoLeft, ambiStereoRight);
    if (!std::isfinite(ambiStereoLeft) || !std::isfinite(ambiStereoRight)) {
        std::cerr << "Ambi Stereo Decoder output is not finite\n";
        return 1;
    }
    if (std::max(std::abs(ambiStereoLeft), std::abs(ambiStereoRight)) <= 0.000001f) {
        std::cerr << "Ambi Stereo Decoder output is silent\n";
        return 1;
    }
    ambiStereoParams.layout = s3g::AmbiStereoVirtualLayout::Sphere32;
    ambiStereoParams.method = s3g::AmbiStereoMethod::SpacedOmni;
    ambiStereoParams.bassMonoHz = 90.0f;
    ambiStereo.setParams(ambiStereoParams);
    for (int i = 0; i < 4096; ++i) {
        ambiStereoIn[0] = std::sin(static_cast<float>(i) * 0.017f) * 0.12f;
        ambiStereoIn[3] = std::sin(static_cast<float>(i) * 0.023f) * 0.08f;
        ambiStereo.processFrame(ambiStereoIn, ambiStereoLeft, ambiStereoRight);
        if (!std::isfinite(ambiStereoLeft) || !std::isfinite(ambiStereoRight)) {
            std::cerr << "Ambi Stereo Decoder spaced/bass output is not finite\n";
            return 1;
        }
    }
    ambiStereoParams.bassMonoHz = 0.0f;
    ambiStereoParams.directivityPercent = 100.0f;
    const s3g::AmbiStereoMethod ambiStereoMethods[] = {
        s3g::AmbiStereoMethod::DualShotgun,
        s3g::AmbiStereoMethod::WideCardioid,
        s3g::AmbiStereoMethod::SupercardioidXy,
        s3g::AmbiStereoMethod::HypercardioidXy,
        s3g::AmbiStereoMethod::HeightFocus,
    };
    for (const auto method : ambiStereoMethods) {
        ambiStereoParams.method = method;
        ambiStereo.setParams(ambiStereoParams);
        ambiStereo.processFrame(ambiStereoIn, ambiStereoLeft, ambiStereoRight);
        if (!std::isfinite(ambiStereoLeft) || !std::isfinite(ambiStereoRight)) {
            std::cerr << "Ambi Stereo Decoder mic-pattern output is not finite\n";
            return 1;
        }
    }

    s3g::AmbiHeadDecoder headDecoder;
    headDecoder.prepare(48000.0);
    s3g::AmbiHeadParams headParams;
    headParams.order = 7;
    headParams.layout = s3g::AmbiStereoVirtualLayout::Dome24;
    headParams.head = s3g::AmbiHeadProfile::SyntheticMedium;
    headParams.mode = s3g::AmbiHeadMode::Binaural;
    headDecoder.updateParams(headParams);
    float headLeft = 0.0f;
    float headRight = 0.0f;
    headDecoder.processFrame(ambiStereoIn, headLeft, headRight);
    if (!std::isfinite(headLeft) || !std::isfinite(headRight)) {
        std::cerr << "Ambi Head Decoder binaural output is not finite\n";
        return 1;
    }
    if (std::max(std::abs(headLeft), std::abs(headRight)) <= 0.000001f) {
        std::cerr << "Ambi Head Decoder binaural output is silent\n";
        return 1;
    }
    headParams.mode = s3g::AmbiHeadMode::Transaural;
    headParams.xtcAmountPercent = 80.0f;
    headDecoder.updateParams(headParams);
    for (int i = 0; i < 256; ++i) {
        ambiStereoIn[0] = std::sin(static_cast<float>(i) * 0.011f) * 0.10f;
        ambiStereoIn[1] = std::sin(static_cast<float>(i) * 0.019f) * 0.07f;
        ambiStereoIn[3] = std::cos(static_cast<float>(i) * 0.013f) * 0.06f;
        headDecoder.processFrame(ambiStereoIn, headLeft, headRight);
        if (!std::isfinite(headLeft) || !std::isfinite(headRight)) {
            std::cerr << "Ambi Head Decoder transaural output is not finite\n";
            return 1;
        }
    }

    s3g::LayoutPanner layoutPanner;
    layoutPanner.prepare(48000.0);
    s3g::LayoutPannerParams layoutPannerParams;
    layoutPannerParams.layout = s3g::LayoutPannerPreset::Dome24NoOverhead;
    layoutPannerParams.method = s3g::LayoutPannerMethod::Distance;
    layoutPannerParams.outputGainDb = -12.0f;
    layoutPanner.setParams(layoutPannerParams);
    float pannerIn[s3g::kLayoutPannerSources] {};
    float pannerOut[s3g::kLayoutPannerMaxSpeakers] {};
    pannerIn[0] = 0.25f;
    layoutPanner.processFrame(pannerIn, pannerOut, s3g::kLayoutPannerSources);
    float pannerPeak = 0.0f;
    for (uint32_t ch = 0; ch < s3g::kLayoutPannerMaxSpeakers; ++ch) {
        if (!std::isfinite(pannerOut[ch])) {
            std::cerr << "Layout Panner output is not finite\n";
            return 1;
        }
        pannerPeak = std::max(pannerPeak, std::abs(pannerOut[ch]));
    }
    if (pannerPeak <= 0.000001f || pannerPeak > 1.0f) {
        std::cerr << "Layout Panner peak outside expected range: " << pannerPeak << "\n";
        return 1;
    }
    layoutPannerParams.layout = s3g::LayoutPannerPreset::Quad;
    layoutPanner.setParams(layoutPannerParams);
    if (layoutPanner.activeSpeakers() != 4u
        || std::abs(layoutPanner.speakers()[0].azimuthDeg - 45.0f) > 0.001f
        || std::abs(layoutPanner.speakers()[1].azimuthDeg - -45.0f) > 0.001f
        || std::abs(layoutPanner.speakers()[2].azimuthDeg - -135.0f) > 0.001f
        || std::abs(layoutPanner.speakers()[3].azimuthDeg - 135.0f) > 0.001f) {
        std::cerr << "Layout Panner quad layout is not square-corner ordered\n";
        return 1;
    }
    for (uint32_t ch = 0; ch < s3g::kLayoutPannerMaxSpeakers; ++ch) {
        pannerOut[ch] = 0.125f;
    }
    layoutPanner.processFrame(pannerIn, pannerOut, s3g::kLayoutPannerSources);
    for (uint32_t ch = 4; ch < s3g::kLayoutPannerMaxSpeakers; ++ch) {
        if (std::abs(pannerOut[ch]) > 0.000001f) {
            std::cerr << "Layout Panner did not zero inactive outputs\n";
            return 1;
        }
    }
    layoutPannerParams.layout = s3g::LayoutPannerPreset::QuadOverhead6;
    layoutPanner.setParams(layoutPannerParams);
    if (layoutPanner.activeSpeakers() != 6u
        || std::abs(layoutPanner.speakers()[0].azimuthDeg - 45.0f) > 0.001f
        || std::abs(layoutPanner.speakers()[1].azimuthDeg - -45.0f) > 0.001f
        || std::abs(layoutPanner.speakers()[2].azimuthDeg - -135.0f) > 0.001f
        || std::abs(layoutPanner.speakers()[3].azimuthDeg - 135.0f) > 0.001f
        || std::abs(layoutPanner.speakers()[4].azimuthDeg - 90.0f) > 0.001f
        || std::abs(layoutPanner.speakers()[5].azimuthDeg - -90.0f) > 0.001f
        || std::abs(layoutPanner.speakers()[4].elevationDeg - 60.0f) > 0.001f
        || std::abs(layoutPanner.speakers()[5].elevationDeg - 60.0f) > 0.001f) {
        std::cerr << "Layout Panner quad+overhead layout changed unexpectedly\n";
        return 1;
    }
    layoutPannerParams.layout = s3g::LayoutPannerPreset::Cube17;
    layoutPanner.setParams(layoutPannerParams);
    if (layoutPanner.activeSpeakers() != 17u
        || std::abs(layoutPanner.speakers()[16].elevationDeg - 90.0f) > 0.001f) {
        std::cerr << "Layout Panner cube17 layout changed unexpectedly\n";
        return 1;
    }
    layoutPannerParams.layout = s3g::LayoutPannerPreset::Dodeca12;
    layoutPanner.setParams(layoutPannerParams);
    if (layoutPanner.activeSpeakers() != 12u
        || std::abs(layoutPanner.speakers()[0].azimuthDeg - -31.717474f) > 0.001f
        || std::abs(layoutPanner.speakers()[11].elevationDeg - 31.717474f) > 0.001f) {
        std::cerr << "Layout Panner dodeca12 layout changed unexpectedly\n";
        return 1;
    }
    layoutPannerParams.layout = s3g::LayoutPannerPreset::Custom;
    layoutPannerParams.activeSpeakers = 9;
    layoutPannerParams.customShape = s3g::LayoutPannerCustomShape::Dome;
    layoutPanner.setParams(layoutPannerParams);
    bool domeHasLowerSpeaker = false;
    bool domeHasUpperSpeaker = false;
    for (uint32_t i = 0; i < layoutPanner.activeSpeakers(); ++i) {
        domeHasLowerSpeaker = domeHasLowerSpeaker || layoutPanner.speakers()[i].elevationDeg < -0.001f;
        domeHasUpperSpeaker = domeHasUpperSpeaker || layoutPanner.speakers()[i].elevationDeg > 30.0f;
    }
    if (layoutPanner.activeSpeakers() != 9u || domeHasLowerSpeaker || !domeHasUpperSpeaker) {
        std::cerr << "Layout Panner custom dome layout changed unexpectedly\n";
        return 1;
    }
    layoutPannerParams.activeSpeakers = 8;
    layoutPannerParams.customShape = s3g::LayoutPannerCustomShape::Auto;
    layoutPanner.setParams(layoutPannerParams);
    if (layoutPanner.activeSpeakers() != 8u
        || std::abs(layoutPanner.speakers()[0].azimuthDeg - -45.0f) > 0.001f
        || std::abs(layoutPanner.speakers()[0].elevationDeg - -35.26439f) > 0.001f
        || std::abs(layoutPanner.speakers()[7].azimuthDeg - 45.0f) > 0.001f
        || std::abs(layoutPanner.speakers()[7].elevationDeg - 35.26439f) > 0.001f) {
        std::cerr << "Layout Panner custom AUTO 8 did not default to cube\n";
        return 1;
    }
    layoutPannerParams.customShape = s3g::LayoutPannerCustomShape::Icosa;
    layoutPanner.setParams(layoutPannerParams);
    if (layoutPanner.activeSpeakers() != 12u) {
        std::cerr << "Layout Panner custom ICO did not force 12 speakers\n";
        return 1;
    }
    layoutPannerParams.customShape = s3g::LayoutPannerCustomShape::Dodeca;
    layoutPanner.setParams(layoutPannerParams);
    if (layoutPanner.activeSpeakers() != 20u) {
        std::cerr << "Layout Panner custom DODECA did not force 20 speakers\n";
        return 1;
    }
    layoutPanner.setSpeaker(0, { 12.0f, -8.0f, 1.25f });
    if (layoutPanner.params().layout != s3g::LayoutPannerPreset::Custom
        || std::abs(layoutPanner.speakers()[0].azimuthDeg - 12.0f) > 0.001f
        || std::abs(layoutPanner.speakers()[0].elevationDeg - -8.0f) > 0.001f
        || std::abs(layoutPanner.speakers()[0].distance - 1.25f) > 0.001f) {
        std::cerr << "Layout Panner custom speaker edit failed\n";
        return 1;
    }
    layoutPannerParams.layout = s3g::LayoutPannerPreset::Dome25;
    layoutPanner.setParams(layoutPannerParams);
    if (layoutPanner.activeSpeakers() != 25u
        || std::abs(layoutPanner.speakers()[24].elevationDeg - 90.0f) > 0.001f) {
        std::cerr << "Layout Panner dome25 layout changed unexpectedly\n";
        return 1;
    }
    layoutPannerParams.method = s3g::LayoutPannerMethod::Cosine;
    layoutPanner.setParams(layoutPannerParams);
    layoutPanner.processFrame(pannerIn, pannerOut, s3g::kLayoutPannerSources);
    for (uint32_t ch = 0; ch < s3g::kLayoutPannerMaxSpeakers; ++ch) {
        if (!std::isfinite(pannerOut[ch])) {
            std::cerr << "Layout Panner cosine output is not finite\n";
            return 1;
        }
    }

    s3g::AedMaskState sendState;
    s3g::AedMaskState retState;
    s3g::MixerState mixState;
    s3g::AedMaskParams maskParams;
    s3g::MixerParams mixParams;
    float hoaIn[s3g::k3OaChannels] {};
    float bus[s3g::k3OafxBusChannels] {};
    float hoaOut[s3g::k3OaChannels] {};
    hoaIn[0] = 0.5f;
    maskParams.width = 0.8f;
    maskParams.focus = 0.1f;
    s3g::process3OafxSendFrame(hoaIn, bus, sendState, maskParams, true, true);
    for (uint32_t ch = 0; ch < s3g::k3OafxBusChannels; ++ch) {
        if (!std::isfinite(bus[ch])) {
            std::cerr << "3OAFX send bus output is not finite\n";
            return 1;
        }
    }
    s3g::process3OafxReturnFrame(bus, hoaOut, retState, mixState, maskParams, mixParams);
    for (uint32_t ch = 0; ch < s3g::k3OaChannels; ++ch) {
        if (!std::isfinite(hoaOut[ch])) {
            std::cerr << "3OAFX return output is not finite\n";
            return 1;
        }
    }
    for (uint32_t ch = s3g::k3OafxVirtualSpeakers; ch < s3g::k3OafxBusChannels; ++ch) {
        bus[ch] = (ch & 1u) ? 12.0f : -12.0f;
    }
    mixParams.useIncomingMask = false;
    mixParams.dryTrim = 0.0f;
    s3g::process3OafxReturnFrame(bus, hoaOut, retState, mixState, maskParams, mixParams);
    for (uint32_t ch = 0; ch < s3g::k3OaChannels; ++ch) {
        if (!std::isfinite(hoaOut[ch]) || std::abs(hoaOut[ch]) > 4.0f) {
            std::cerr << "3OAFX return local-mask mode failed carrier corruption test\n";
            return 1;
        }
    }

    float single3OafxPeak[4] {};
    for (uint32_t kind = 0; kind < 4u; ++kind) {
        s3g::ThreeOafxSingleEffect fx;
        fx.prepare(48000.0);
        s3g::ThreeOafxSingleEffectParams fxParams;
        fxParams.kind = static_cast<s3g::ThreeOafxEffectKind>(kind);
        fxParams.dry = kind == 3u ? 1.0f : 0.65f;
        fxParams.output = 1.0f;
        fxParams.mask.azimuthDeg = -45.0f + static_cast<float>(kind) * 30.0f;
        fxParams.mask.elevationDeg = -8.0f + static_cast<float>(kind) * 5.0f;
        fxParams.mask.width = 0.72f;
        fxParams.mask.smoothing = 0.20f;
        fxParams.mask.level = 1.0f;
        fxParams.mix = kind == 3u ? 0.0f : 0.65f;
        fxParams.delayTimeMs = 120.0f;
        fxParams.delayFeedback = 0.22f;
        fxParams.pitchSemitones = 7.0f;
        fxParams.filterTone = 0.82f;
        fxParams.gain = kind == 3u ? 0.25f : 1.0f;
        fx.setParams(fxParams);
        fx.reset();
        float fxOut[s3g::k3OaChannels] {};
        for (uint32_t i = 0; i < 12000u; ++i) {
            hoaIn[0] = std::sin(static_cast<float>(i) * 0.019f) * 0.12f;
            hoaIn[1] = std::cos(static_cast<float>(i) * 0.013f) * 0.05f;
            hoaIn[2] = std::sin(static_cast<float>(i) * 0.011f + 0.3f) * 0.04f;
            hoaIn[3] = std::cos(static_cast<float>(i) * 0.017f + 0.7f) * 0.03f;
            for (uint32_t ch = 4; ch < s3g::k3OaChannels; ++ch) hoaIn[ch] = 0.0f;
            fx.processFrame(hoaIn, fxOut);
            for (float value : fxOut) {
                if (!std::isfinite(value)) {
                    std::cerr << "3OAFX single effect output is not finite for kind " << kind << "\n";
                    return 1;
                }
                single3OafxPeak[kind] = std::max(single3OafxPeak[kind], std::abs(value));
            }
        }
        if (single3OafxPeak[kind] <= 0.000001f || single3OafxPeak[kind] > 2.0f) {
            std::cerr << "3OAFX single effect peak outside expected range for kind " << kind << ": " << single3OafxPeak[kind] << "\n";
            return 1;
        }
    }
    s3g::ThreeOafxSingleEffect gainSpot;
    gainSpot.prepare(48000.0);
    s3g::ThreeOafxSingleEffectParams gainSpotParams;
    gainSpotParams.kind = s3g::ThreeOafxEffectKind::Gain;
    gainSpotParams.dry = 0.0f;
    gainSpotParams.output = 1.0f;
    gainSpotParams.gain = 1.0f;
    gainSpotParams.mask.azimuthDeg = 0.0f;
    gainSpotParams.mask.elevationDeg = 0.0f;
    gainSpotParams.mask.width = 0.80f;
    gainSpot.setParams(gainSpotParams);
    gainSpot.reset();
    for (uint32_t ch = 0; ch < s3g::k3OaChannels; ++ch) hoaIn[ch] = 0.0f;
    hoaIn[0] = 0.25f;
    gainSpot.processFrame(hoaIn, hoaOut);
    float gainSpotPeak = 0.0f;
    for (float value : hoaOut) {
        if (!std::isfinite(value)) {
            std::cerr << "3OAFX gain spotlight output is not finite\n";
            return 1;
        }
        gainSpotPeak = std::max(gainSpotPeak, std::abs(value));
    }
    if (gainSpotPeak <= 0.000001f) {
        std::cerr << "3OAFX gain spotlight did not open a masked dry window\n";
        return 1;
    }
    const float gainAzimuths[] = { -179.0f, -135.0f, -90.0f, -45.0f, 0.0f, 45.0f, 90.0f, 135.0f, 179.0f };
    float gainAzMinPeak = 1000.0f;
    float gainAzMaxPeak = 0.0f;
    for (float azimuth : gainAzimuths) {
        s3g::ThreeOafxSingleEffect azGain;
        azGain.prepare(48000.0);
        s3g::ThreeOafxSingleEffectParams azGainParams;
        azGainParams.kind = s3g::ThreeOafxEffectKind::Gain;
        azGainParams.dry = 0.0f;
        azGainParams.output = 1.0f;
        azGainParams.gain = 1.0f;
        azGainParams.mask.azimuthDeg = azimuth;
        azGainParams.mask.elevationDeg = 0.0f;
        azGainParams.mask.width = 0.80f;
        azGainParams.mask.smoothing = 0.0f;
        azGain.setParams(azGainParams);
        azGain.reset();
        float azPeak = 0.0f;
        for (uint32_t i = 0; i < 18000u; ++i) {
            for (uint32_t ch = 0; ch < s3g::k3OaChannels; ++ch) hoaIn[ch] = 0.0f;
            hoaIn[0] = 0.25f;
            azGain.processFrame(hoaIn, hoaOut);
            if (i > 12000u) {
                for (float value : hoaOut) {
                    if (!std::isfinite(value)) {
                        std::cerr << "3OAFX gain azimuth output is not finite\n";
                        return 1;
                    }
                    azPeak = std::max(azPeak, std::abs(value));
                }
            }
        }
        gainAzMinPeak = std::min(gainAzMinPeak, azPeak);
        gainAzMaxPeak = std::max(gainAzMaxPeak, azPeak);
    }
    if (gainAzMinPeak <= 0.002f || gainAzMaxPeak / std::max(0.000001f, gainAzMinPeak) > 10.0f) {
        std::cerr << "3OAFX gain azimuth coverage collapsed: min/max "
                  << gainAzMinPeak << " / " << gainAzMaxPeak << "\n";
        return 1;
    }
    float gainHoleMinDrop = 1000.0f;
    for (float azimuth : gainAzimuths) {
        s3g::ThreeOafxSingleEffect azGain;
        azGain.prepare(48000.0);
        s3g::ThreeOafxSingleEffectParams azGainParams;
        azGainParams.kind = s3g::ThreeOafxEffectKind::Gain;
        azGainParams.dry = 1.0f;
        azGainParams.output = 1.0f;
        azGainParams.gain = 0.0f;
        azGainParams.mask.azimuthDeg = azimuth;
        azGainParams.mask.elevationDeg = 0.0f;
        azGainParams.mask.width = 0.72f;
        azGainParams.mask.smoothing = 0.0f;
        azGain.setParams(azGainParams);
        azGain.reset();
        const auto sourceBasis = s3g::acnSn3dBasis(s3g::directionFromAed(azimuth, 0.0f));
        float dryEnergy = 0.0f;
        float wetEnergy = 0.0f;
        for (uint32_t i = 0; i < 18000u; ++i) {
            for (uint32_t ch = 0; ch < s3g::k3OaChannels; ++ch) {
                hoaIn[ch] = sourceBasis[ch] * 0.12f;
            }
            azGain.processFrame(hoaIn, hoaOut);
            if (i > 12000u) {
                for (uint32_t ch = 0; ch < s3g::k3OaChannels; ++ch) {
                    if (!std::isfinite(hoaOut[ch])) {
                        std::cerr << "3OAFX gain hole output is not finite\n";
                        return 1;
                    }
                    dryEnergy += hoaIn[ch] * hoaIn[ch];
                    wetEnergy += hoaOut[ch] * hoaOut[ch];
                }
            }
        }
        const float drop = 1.0f - wetEnergy / std::max(0.000001f, dryEnergy);
        gainHoleMinDrop = std::min(gainHoleMinDrop, drop);
    }
    if (gainHoleMinDrop <= 0.10f) {
        std::cerr << "3OAFX gain hole did not affect all azimuths; min drop "
                  << gainHoleMinDrop << "\n";
        return 1;
    }

    s3g::ThreeOafxSingleEffect delayNoBypass;
    delayNoBypass.prepare(48000.0);
    s3g::ThreeOafxSingleEffectParams delayNoBypassParams;
    delayNoBypassParams.kind = s3g::ThreeOafxEffectKind::Delay;
    delayNoBypassParams.dry = 0.0f;
    delayNoBypassParams.output = 1.0f;
    delayNoBypassParams.mix = 0.0f;
    delayNoBypassParams.delayTimeMs = 40.0f;
    delayNoBypassParams.delayFeedback = 0.25f;
    delayNoBypassParams.mask.azimuthDeg = 0.0f;
    delayNoBypassParams.mask.elevationDeg = 0.0f;
    delayNoBypassParams.mask.width = 1.0f;
    delayNoBypassParams.mask.level = 1.0f;
    delayNoBypass.setParams(delayNoBypassParams);
    delayNoBypass.reset();
    for (uint32_t i = 0; i < 512u; ++i) {
        for (uint32_t ch = 0; ch < s3g::k3OaChannels; ++ch) hoaIn[ch] = 0.0f;
        if (i == 0u) hoaIn[0] = 0.35f;
        delayNoBypass.processFrame(hoaIn, hoaOut);
    }
    delayNoBypassParams.mix = 1.0f;
    delayNoBypass.setParams(delayNoBypassParams);
    float delayNoBypassPeak = 0.0f;
    for (uint32_t i = 0; i < 3600u; ++i) {
        for (uint32_t ch = 0; ch < s3g::k3OaChannels; ++ch) hoaIn[ch] = 0.0f;
        delayNoBypass.processFrame(hoaIn, hoaOut);
        for (float value : hoaOut) {
            if (!std::isfinite(value)) {
                std::cerr << "3OAFX delay mix-zero state output is not finite\n";
                return 1;
            }
            delayNoBypassPeak = std::max(delayNoBypassPeak, std::abs(value));
        }
    }
    if (delayNoBypassPeak <= 0.000001f) {
        std::cerr << "3OAFX delay did not keep processing while MIX was zero\n";
        return 1;
    }

    auto runGroupRotateSmoke = [](auto& processor, uint32_t channels, const char* label) -> bool {
        constexpr uint32_t frames = 32u;
        std::array<std::array<float, frames>, s3g::kAmbiGroupRotateMaxChannels> inBuffers {};
        std::array<std::array<float, frames>, s3g::kAmbiGroupRotateMaxChannels> outBuffers {};
        std::array<float*, s3g::kAmbiGroupRotateMaxChannels> inPtrs {};
        std::array<float*, s3g::kAmbiGroupRotateMaxChannels> outPtrs {};
        for (uint32_t ch = 0; ch < channels; ++ch) {
            inPtrs[ch] = inBuffers[ch].data();
            outPtrs[ch] = outBuffers[ch].data();
            for (uint32_t i = 0; i < frames; ++i) {
                inBuffers[ch][i] = std::sin(static_cast<float>(ch + 1u) * 0.11f + static_cast<float>(i) * 0.03f) * 0.06f;
            }
        }
        s3g::AmbiGroupRotateParams params;
        params.yawDeg = 31.0f;
        params.pitchDeg = -11.0f;
        params.rollDeg = 17.0f;
        params.spread = 0.65f;
        params.tilt = 0.35f;
        params.twist = -0.25f;
        params.width = 0.90f;
        processor.setParams(params);
        processor.reset();
        processor.process(inPtrs.data(), channels, outPtrs.data(), channels, frames);
        float peak = 0.0f;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            for (float value : outBuffers[ch]) {
                if (!std::isfinite(value)) {
                    std::cerr << label << " output is not finite\n";
                    return false;
                }
                peak = std::max(peak, std::abs(value));
            }
        }
        if (peak <= 0.000001f || peak > 2.0f) {
            std::cerr << label << " peak outside expected range: " << peak << "\n";
            return false;
        }
        return true;
    };

    {
        s3g::AmbiGroupRotateParams seamParams {};
        seamParams.yawDeg = 180.0f;
        if (std::abs(s3g::sanitizeAmbiGroupRotateParams(seamParams).yawDeg - 180.0f) > 0.0001f) {
            std::cerr << "Ambi Group Rotate yaw endpoint wrapped too early\n";
            return 1;
        }
        seamParams.yawDeg = 181.0f;
        if (std::abs(s3g::sanitizeAmbiGroupRotateParams(seamParams).yawDeg + 179.0f) > 0.0001f) {
            std::cerr << "Ambi Group Rotate yaw did not wrap past +180\n";
            return 1;
        }
        seamParams.yawDeg = 170.0f;
        seamParams.spread = 1.0f;
        const auto spreadGroup = s3g::ambiGroupRotateParamsForGroup(seamParams, 3u, 4u);
        if (std::abs(spreadGroup.yawDeg + 70.0f) > 0.0001f) {
            std::cerr << "Ambi Group Rotate spread yaw clamped instead of wrapping\n";
            return 1;
        }
        seamParams.spread = -1.0f;
        const auto inverseSpreadGroup = s3g::ambiGroupRotateParamsForGroup(seamParams, 3u, 4u);
        if (std::abs(inverseSpreadGroup.yawDeg - 50.0f) > 0.0001f) {
            std::cerr << "Ambi Group Rotate negative spread did not invert group yaw\n";
            return 1;
        }
    }

    s3g::AmbiGroupRotateProcessor<4> groupRotate64;
    if (!runGroupRotateSmoke(groupRotate64, 64u, "Ambi Group Rotate 64")) {
        return 1;
    }
    s3g::AmbiGroupRotateProcessor<8> groupRotate128;
    if (!runGroupRotateSmoke(groupRotate128, 128u, "Ambi Group Rotate 128")) {
        return 1;
    }

    auto runGroupDepthSmoke = [](auto& processor, uint32_t channels, const char* label) -> bool {
        constexpr uint32_t frames = 128u;
        std::array<std::array<float, frames>, s3g::kAmbiGroupDepthMaxChannels> inBuffers {};
        std::array<std::array<float, frames>, s3g::kAmbiGroupDepthMaxChannels> outBuffers {};
        std::array<float*, s3g::kAmbiGroupDepthMaxChannels> inPtrs {};
        std::array<float*, s3g::kAmbiGroupDepthMaxChannels> outPtrs {};
        for (uint32_t ch = 0; ch < channels; ++ch) {
            inPtrs[ch] = inBuffers[ch].data();
            outPtrs[ch] = outBuffers[ch].data();
            const uint32_t order = s3g::ambiUtilityOrderForChannel(ch % s3g::kAmbiGroupDepthGroupChannels);
            for (uint32_t i = 0; i < frames; ++i) {
                inBuffers[ch][i] = std::sin(static_cast<float>(i) * 0.09f + static_cast<float>(ch) * 0.19f) * (0.04f + 0.01f * static_cast<float>(order));
            }
        }
        s3g::AmbiGroupDepthParams params;
        params.depth = 0.88f;
        params.spread = 0.72f;
        params.focus = 0.18f;
        params.air = 0.65f;
        params.tail = 0.45f;
        params.low = 0.25f;
        params.width = 0.85f;
        processor.prepare(48000.0);
        processor.setParams(params);
        processor.reset();
        processor.process(inPtrs.data(), channels, outPtrs.data(), channels, frames);
        float peak = 0.0f;
        float wEnergy = 0.0f;
        float highEnergy = 0.0f;
        uint32_t wCount = 0u;
        uint32_t highCount = 0u;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            const uint32_t lane = ch % s3g::kAmbiGroupDepthGroupChannels;
            const uint32_t order = s3g::ambiUtilityOrderForChannel(lane);
            for (float value : outBuffers[ch]) {
                if (!std::isfinite(value)) {
                    std::cerr << label << " output is not finite\n";
                    return false;
                }
                peak = std::max(peak, std::abs(value));
                if (order == 0u) wEnergy += value * value;
                if (order == 3u) highEnergy += value * value;
            }
            if (order == 0u) wCount += frames;
            if (order == 3u) highCount += frames;
        }
        if (peak <= 0.000001f || peak > 1.5f) {
            std::cerr << label << " peak outside expected range: " << peak << "\n";
            return false;
        }
        const float wAverage = wEnergy / static_cast<float>(std::max(1u, wCount));
        const float highAverage = highEnergy / static_cast<float>(std::max(1u, highCount));
        if (!(highAverage < wAverage * 0.85f)) {
            std::cerr << label << " did not soften high-order energy enough\n";
            return false;
        }
        return true;
    };

    s3g::AmbiGroupDepthProcessor<1> ambiDepth16;
    if (!runGroupDepthSmoke(ambiDepth16, 16u, "Ambi Depth 16")) {
        return 1;
    }
    s3g::AmbiGroupDepthProcessor<4> groupDepth64;
    if (!runGroupDepthSmoke(groupDepth64, 64u, "Ambi Group Depth 64")) {
        return 1;
    }
    s3g::AmbiGroupDepthProcessor<8> groupDepth128;
    if (!runGroupDepthSmoke(groupDepth128, 128u, "Ambi Group Depth 128")) {
        return 1;
    }

    s3g::AmbiOrderBandParams orderBandParams;
    orderBandParams.order = 7;
    orderBandParams.weighting = s3g::AmbiUtilityWeighting::MaxRe;
    orderBandParams.blend = 1.0f;
    orderBandParams.orderGain.fill(1.0f);
    s3g::AmbiOrderBandProcessor orderBand;
    orderBand.setParams(orderBandParams);
    orderBand.reset();
    std::array<std::array<float, 16>, s3g::kAmbiUtilityChannels> ambiUtilityInBuffers {};
    std::array<std::array<float, 16>, s3g::kAmbiUtilityChannels> ambiUtilityOutBuffers {};
    float* ambiUtilityIn[s3g::kAmbiUtilityChannels] {};
    float* ambiUtilityOut[s3g::kAmbiUtilityChannels] {};
    for (uint32_t ch = 0; ch < s3g::kAmbiUtilityChannels; ++ch) {
        ambiUtilityIn[ch] = ambiUtilityInBuffers[ch].data();
        ambiUtilityOut[ch] = ambiUtilityOutBuffers[ch].data();
        ambiUtilityInBuffers[ch][0] = ch == 0 ? 0.25f : 0.01f * std::sin(static_cast<float>(ch));
    }
    orderBand.process(ambiUtilityIn, ambiUtilityOut, s3g::kAmbiUtilityChannels, s3g::kAmbiUtilityChannels, 16);
    if (!std::isfinite(ambiUtilityOutBuffers[0][0]) || std::abs(ambiUtilityOutBuffers[0][0]) < 0.0001f) {
        std::cerr << "Ambi Order / Band output failed\n";
        return 1;
    }

    s3g::AmbiRotateParams rotateParams;
    rotateParams.order = 3;
    rotateParams.yawDeg = 35.0f;
    rotateParams.pitchDeg = 12.0f;
    rotateParams.rollDeg = -8.0f;
    s3g::AmbiRotateProcessor rotate;
    rotate.setParams(rotateParams);
    rotate.reset();
    rotate.process(ambiUtilityIn, ambiUtilityOut, s3g::kAmbiUtilityChannels, s3g::kAmbiUtilityChannels, 16);
    for (uint32_t ch = 0; ch < s3g::ambiUtilityChannelsForOrder(rotateParams.order); ++ch) {
        if (!std::isfinite(ambiUtilityOutBuffers[ch][0])) {
            std::cerr << "Ambi Rotate output is not finite\n";
            return 1;
        }
    }
    rotateParams.order = 7;
    rotate.setParams(rotateParams);
    rotate.reset();
    for (auto& ch : ambiUtilityInBuffers) {
        std::fill(ch.begin(), ch.end(), 0.0f);
    }
    for (auto& ch : ambiUtilityOutBuffers) {
        std::fill(ch.begin(), ch.end(), 0.0f);
    }
    for (uint32_t ch = 0; ch < s3g::ambiUtilityChannelsForOrder(3); ++ch) {
        ambiUtilityInBuffers[ch][0] = ch == 0u ? 0.20f : 0.006f * std::sin(static_cast<float>(ch));
    }
    rotate.process(ambiUtilityIn, ambiUtilityOut, s3g::kAmbiUtilityChannels, s3g::kAmbiUtilityChannels, 16);
    for (uint32_t ch = 0; ch < s3g::kAmbiUtilityChannels; ++ch) {
        for (float value : ambiUtilityOutBuffers[ch]) {
            if (!std::isfinite(value)) {
                std::cerr << "Ambi Rotate protected 7OA output is not finite\n";
                return 1;
            }
            if (ch >= s3g::ambiUtilityChannelsForOrder(3) && std::abs(value) > 0.000001f) {
                std::cerr << "Ambi Rotate generated higher-order output from 3OA input\n";
                return 1;
            }
        }
    }
    for (auto& ch : ambiUtilityInBuffers) {
        std::fill(ch.begin(), ch.end(), 0.0f);
    }
    for (auto& ch : ambiUtilityOutBuffers) {
        std::fill(ch.begin(), ch.end(), 1.0f);
    }
    rotate.process(ambiUtilityIn, ambiUtilityOut, s3g::kAmbiUtilityChannels, s3g::kAmbiUtilityChannels, 16);
    for (const auto& ch : ambiUtilityOutBuffers) {
        for (float value : ch) {
            if (std::abs(value) > 0.000001f) {
                std::cerr << "Ambi Rotate silent block was not cleared\n";
                return 1;
            }
        }
    }

    {
        s3g::AmbiGroupDepthProcessor<1> tailDepth;
        tailDepth.prepare(48000.0);
        s3g::AmbiGroupDepthParams tailParams {};
        tailParams.depth = 0.85f;
        tailParams.tail = 1.0f;
        tailParams.air = 0.20f;
        tailDepth.setParams(tailParams);
        tailDepth.reset();
        constexpr uint32_t tailFrames = 64u;
        std::array<std::array<float, tailFrames>, s3g::kAmbiGroupDepthGroupChannels> tailInBuffers {};
        std::array<std::array<float, tailFrames>, s3g::kAmbiGroupDepthGroupChannels> tailOutBuffers {};
        std::array<float*, s3g::kAmbiGroupDepthGroupChannels> tailIn {};
        std::array<float*, s3g::kAmbiGroupDepthGroupChannels> tailOut {};
        for (uint32_t ch = 0; ch < s3g::kAmbiGroupDepthGroupChannels; ++ch) {
            tailIn[ch] = tailInBuffers[ch].data();
            tailOut[ch] = tailOutBuffers[ch].data();
        }
        tailInBuffers[0][0] = 0.9f;
        tailInBuffers[1][0] = 0.25f;
        tailInBuffers[2][0] = -0.2f;
        tailDepth.process(tailIn.data(), s3g::kAmbiGroupDepthGroupChannels, tailOut.data(), s3g::kAmbiGroupDepthGroupChannels, tailFrames);
        for (auto& ch : tailInBuffers) {
            std::fill(ch.begin(), ch.end(), 0.0f);
        }
        float tailPeak = 0.0f;
        for (uint32_t block = 0; block < 120u; ++block) {
            for (auto& ch : tailOutBuffers) {
                std::fill(ch.begin(), ch.end(), 0.0f);
            }
            tailDepth.process(tailIn.data(), s3g::kAmbiGroupDepthGroupChannels, tailOut.data(), s3g::kAmbiGroupDepthGroupChannels, tailFrames);
            if (block > 8u) {
                for (const auto& ch : tailOutBuffers) {
                    for (float value : ch) {
                        if (!std::isfinite(value)) {
                            std::cerr << "Ambi Depth FDN tail output is not finite\n";
                            return 1;
                        }
                        tailPeak = std::max(tailPeak, std::abs(value));
                    }
                }
            }
        }
        if (tailPeak <= 0.000001f || tailPeak > 1.0f) {
            std::cerr << "Ambi Depth FDN tail peak outside expected range: " << tailPeak << "\n";
            return 1;
        }
    }

    auto ambiGrainSample = std::make_shared<s3g::AmbiGrainSample>();
    ambiGrainSample->frames = 96000;
    ambiGrainSample->channels = s3g::kAmbiGrainChannels;
    ambiGrainSample->sampleRate = 48000.0;
    ambiGrainSample->path = "synthetic-ambi-grain";
    ambiGrainSample->audio.assign(static_cast<size_t>(ambiGrainSample->frames) * ambiGrainSample->channels, 0.0f);
    for (uint32_t frame = 0; frame < ambiGrainSample->frames; ++frame) {
        const float t = static_cast<float>(frame) / static_cast<float>(ambiGrainSample->sampleRate);
        for (uint32_t ch = 0; ch < ambiGrainSample->channels; ++ch) {
            const float hz = 90.0f + static_cast<float>(ch) * 11.0f;
            ambiGrainSample->audio[static_cast<size_t>(frame) * ambiGrainSample->channels + ch] =
                std::sin(6.28318530718f * hz * t) * (0.08f + 0.004f * static_cast<float>(ch));
        }
    }
    s3g::AmbiGrainProcessor ambiGrain;
    ambiGrain.prepare(48000.0);
    s3g::AmbiGrainParams ambiGrainParams;
    ambiGrainParams.density = 240.0f;
    ambiGrainParams.grainMs = 4000.0f;
    const auto safeAmbiGrainParams = s3g::sanitizeAmbiGrainParams(ambiGrainParams);
    if (safeAmbiGrainParams.density > 8.001f) {
        std::cerr << "Ambi Grain density/GMS safety cap failed\n";
        return 1;
    }
    ambiGrainParams.order = 3;
    ambiGrainParams.mode = s3g::AmbiGrainMode::Cloud;
    ambiGrainParams.density = 80.0f;
    ambiGrainParams.grainMs = 55.0f;
    ambiGrainParams.positionJitter = 0.25f;
    ambiGrainParams.rateJitterOct = 0.08f;
    ambiGrainParams.outputGainDb = -18.0f;
    ambiGrain.setParams(ambiGrainParams);
    std::array<std::array<float, 256>, s3g::kAmbiGrainChannels> ambiGrainBuffers {};
    float* ambiGrainOut[s3g::kAmbiGrainChannels] {};
    for (uint32_t ch = 0; ch < s3g::kAmbiGrainChannels; ++ch) {
        ambiGrainOut[ch] = ambiGrainBuffers[ch].data();
    }
    float ambiGrainPeak = 0.0f;
    for (int block = 0; block < 48; ++block) {
        ambiGrain.process(ambiGrainSample, ambiGrainOut, s3g::kAmbiGrainChannels, static_cast<uint32_t>(ambiGrainBuffers[0].size()), true);
        for (const auto& channel : ambiGrainBuffers) {
            for (float value : channel) {
                if (!std::isfinite(value)) {
                    std::cerr << "Ambi Grain output is not finite\n";
                    return 1;
                }
                ambiGrainPeak = std::max(ambiGrainPeak, std::abs(value));
            }
        }
    }
    if (ambiGrainPeak <= 0.00001f || ambiGrainPeak > 1.0f) {
        std::cerr << "Ambi Grain peak outside expected range: " << ambiGrainPeak << "\n";
        return 1;
    }
    ambiGrainParams.sync = false;
    ambiGrainParams.envelope = s3g::AmbiGrainEnvelope::Gauss;
    ambiGrainParams.density = 120.0f;
    ambiGrainParams.grainMs = 35.0f;
    ambiGrain.setParams(ambiGrainParams);
    float ambiGrainAsyncPeak = 0.0f;
    for (int block = 0; block < 48; ++block) {
        ambiGrain.process(ambiGrainSample, ambiGrainOut, s3g::kAmbiGrainChannels, static_cast<uint32_t>(ambiGrainBuffers[0].size()), true);
        for (const auto& channel : ambiGrainBuffers) {
            for (float value : channel) {
                if (!std::isfinite(value)) {
                    std::cerr << "Ambi Grain async/env output is not finite\n";
                    return 1;
                }
                ambiGrainAsyncPeak = std::max(ambiGrainAsyncPeak, std::abs(value));
            }
        }
    }
    if (ambiGrainAsyncPeak <= 0.00001f || ambiGrainAsyncPeak > 1.0f) {
        std::cerr << "Ambi Grain async/env peak outside expected range: " << ambiGrainAsyncPeak << "\n";
        return 1;
    }
    ambiGrain.reset();
    ambiGrainParams.sync = true;
    ambiGrainParams.envelope = s3g::AmbiGrainEnvelope::Hann;
    ambiGrainParams.density = 3.0f;
    ambiGrainParams.grainMs = 1200.0f;
    ambiGrainParams.positionJitter = 0.4f;
    ambiGrainParams.rateJitterOct = 0.02f;
    ambiGrainParams.outputGainDb = -24.0f;
    ambiGrain.setParams(ambiGrainParams);
    float ambiGrainLongPeak = 0.0f;
    for (int block = 0; block < 300; ++block) {
        ambiGrain.process(ambiGrainSample, ambiGrainOut, s3g::kAmbiGrainChannels, static_cast<uint32_t>(ambiGrainBuffers[0].size()), true);
        for (const auto& channel : ambiGrainBuffers) {
            for (float value : channel) {
                if (!std::isfinite(value)) {
                    std::cerr << "Ambi Grain long-window output is not finite\n";
                    return 1;
                }
                ambiGrainLongPeak = std::max(ambiGrainLongPeak, std::abs(value));
            }
        }
    }
    if (ambiGrainLongPeak <= 0.00001f || ambiGrainLongPeak > 1.0f) {
        std::cerr << "Ambi Grain long-window peak outside expected range: " << ambiGrainLongPeak << "\n";
        return 1;
    }
    auto ambiGrainOneOaSample = std::make_shared<s3g::AmbiGrainSample>(*ambiGrainSample);
    ambiGrainOneOaSample->channels = 4u;
    ambiGrainOneOaSample->audio.assign(static_cast<size_t>(ambiGrainOneOaSample->frames) * ambiGrainOneOaSample->channels, 0.0f);
    for (uint32_t frame = 0; frame < ambiGrainOneOaSample->frames; ++frame) {
        for (uint32_t ch = 0; ch < ambiGrainOneOaSample->channels; ++ch) {
            ambiGrainOneOaSample->audio[static_cast<size_t>(frame) * ambiGrainOneOaSample->channels + ch] =
                ambiGrainSample->audio[static_cast<size_t>(frame) * ambiGrainSample->channels + ch];
        }
    }
    ambiGrain.reset();
    ambiGrainParams.order = 3;
    ambiGrainParams.density = 48.0f;
    ambiGrainParams.grainMs = 120.0f;
    ambiGrain.setParams(ambiGrainParams);
    float ambiGrainLowerOrderPeak = 0.0f;
    for (int block = 0; block < 48; ++block) {
        ambiGrain.process(ambiGrainOneOaSample, ambiGrainOut, s3g::kAmbiGrainChannels, static_cast<uint32_t>(ambiGrainBuffers[0].size()), true);
        for (uint32_t ch = 0; ch < ambiGrainOneOaSample->channels; ++ch) {
            for (float value : ambiGrainBuffers[ch]) {
                if (!std::isfinite(value)) {
                    std::cerr << "Ambi Grain lower-order output is not finite\n";
                    return 1;
                }
                ambiGrainLowerOrderPeak = std::max(ambiGrainLowerOrderPeak, std::abs(value));
            }
        }
    }
    if (ambiGrainLowerOrderPeak <= 0.00001f || ambiGrainLowerOrderPeak > 1.0f) {
        std::cerr << "Ambi Grain lower-order peak outside expected range: " << ambiGrainLowerOrderPeak << "\n";
        return 1;
    }



    s3g::ShardScatter shardScatter;
    if (!shardScatter.prepare(48000.0, 512u)) {
        std::cerr << "Shard Scatter prepare failed\n";
        return 1;
    }
    s3g::ShardScatterParams shardParams;
    shardParams.density = 14.0f;
    shardParams.grainMs = 120.0f;
    shardParams.guardMs = 90.0f;
    shardParams.scatterMs = 320.0f;
    shardParams.pitchSpread = 0.55f;
    shardParams.rotate = 0.8f;
    shardParams.width = 0.35f;
    shardParams.feedback = 0.28f;
    shardParams.gainDb = -8.0f;
    shardScatter.setParams(shardParams);
    std::array<float, 512> shardInL {};
    std::array<float, 512> shardInR {};
    std::array<std::array<float, 512>, s3g::kShardScatterChannels> shardOutBuffers {};
    std::array<float*, s3g::kShardScatterChannels> shardOut {};
    for (uint32_t ch = 0; ch < s3g::kShardScatterChannels; ++ch) shardOut[ch] = shardOutBuffers[ch].data();
    float shardPeak = 0.0f;
    float shardMaxStep = 0.0f;
    std::array<float, s3g::kShardScatterChannels> shardLast {};
    for (uint32_t block = 0; block < 240u; ++block) {
        if ((block % 24u) == 0u) {
            const bool alt = ((block / 24u) % 2u) != 0u;
            shardParams.density = alt ? 22.0f : 4.0f;
            shardParams.grainMs = alt ? 65.0f : 240.0f;
            shardParams.scatterMs = alt ? 1600.0f : 120.0f;
            shardParams.pitch = alt ? -0.65f : 1.0f;
            shardParams.rotate = alt ? -2.2f : 0.6f;
            shardParams.width = alt ? 0.85f : 0.12f;
            shardParams.feedback = alt ? 0.68f : 0.08f;
            shardParams.freeze = alt ? 0.35f : 0.0f;
            shardScatter.setParams(shardParams);
        }
        for (uint32_t i = 0; i < 512u; ++i) {
            const float t = static_cast<float>(block * 512u + i) / 48000.0f;
            shardInL[i] = (std::sin(6.28318530718f * 91.0f * t) + std::sin(6.28318530718f * 677.0f * t) * 0.35f) * 0.16f;
            shardInR[i] = (std::sin(6.28318530718f * 137.0f * t) + std::sin(6.28318530718f * 991.0f * t) * 0.30f) * 0.15f;
        }
        shardScatter.process(shardInL.data(), shardInR.data(), shardOut.data(), 512u);
        for (uint32_t ch = 0; ch < s3g::kShardScatterChannels; ++ch) {
            for (float value : shardOutBuffers[ch]) {
                if (!std::isfinite(value)) {
                    std::cerr << "Shard Scatter output is not finite\n";
                    return 1;
                }
                shardPeak = std::max(shardPeak, std::abs(value));
                shardMaxStep = std::max(shardMaxStep, std::abs(value - shardLast[ch]));
                shardLast[ch] = value;
            }
        }
    }
    if (shardPeak <= 0.00001f || shardPeak > 1.0f || shardMaxStep > 0.75f) {
        std::cerr << "Shard Scatter peak/step outside expected range: " << shardPeak << " / " << shardMaxStep << "\n";
        return 1;
    }

    s3g::ShardScatter shardSparse;
    if (!shardSparse.prepare(48000.0, 512u)) {
        std::cerr << "Shard Scatter sparse prepare failed\n";
        return 1;
    }
    s3g::ShardScatterParams shardSparseParams;
    shardSparseParams.density = 0.45f;
    shardSparseParams.grainMs = 55.0f;
    shardSparseParams.guardMs = 80.0f;
    shardSparseParams.scatterMs = 2400.0f;
    shardSparseParams.pitch = -0.85f;
    shardSparseParams.pitchSpread = 1.1f;
    shardSparseParams.rotate = 1.8f;
    shardSparseParams.width = 0.92f;
    shardSparseParams.feedback = 0.42f;
    shardSparseParams.dry = 0.0f;
    shardSparseParams.wet = 1.0f;
    shardSparseParams.gainDb = -5.0f;
    shardSparse.setParams(shardSparseParams);
    float shardSparsePeak = 0.0f;
    float shardSparseMaxStep = 0.0f;
    std::array<float, s3g::kShardScatterChannels> shardSparseLast {};
    for (uint32_t block = 0; block < 300u; ++block) {
        for (uint32_t i = 0; i < 512u; ++i) {
            const float t = static_cast<float>(block * 512u + i) / 48000.0f;
            const float gate = ((block + (i / 64u)) % 9u) == 0u ? 1.0f : 0.28f;
            shardInL[i] = (std::sin(6.28318530718f * 97.0f * t) + std::sin(6.28318530718f * 1447.0f * t) * 0.42f) * 0.18f * gate;
            shardInR[i] = (std::sin(6.28318530718f * 139.0f * t) + std::sin(6.28318530718f * 1133.0f * t) * 0.37f) * 0.17f * gate;
        }
        shardSparse.process(shardInL.data(), shardInR.data(), shardOut.data(), 512u);
        for (uint32_t ch = 0; ch < s3g::kShardScatterChannels; ++ch) {
            for (float value : shardOutBuffers[ch]) {
                if (!std::isfinite(value)) {
                    std::cerr << "Shard Scatter sparse output is not finite\n";
                    return 1;
                }
                shardSparsePeak = std::max(shardSparsePeak, std::abs(value));
                shardSparseMaxStep = std::max(shardSparseMaxStep, std::abs(value - shardSparseLast[ch]));
                shardSparseLast[ch] = value;
            }
        }
    }
    if (shardSparsePeak <= 0.00001f || shardSparsePeak > 1.0f || shardSparseMaxStep > 0.20f) {
        std::cerr << "Shard Scatter sparse high-SCAT de-click outside expected range: " << shardSparsePeak
                  << " / " << shardSparseMaxStep << "\n";
        return 1;
    }

    s3g::ShardScatter shardStop;
    if (!shardStop.prepare(48000.0, 512u)) {
        std::cerr << "Shard Scatter stop prepare failed\n";
        return 1;
    }
    s3g::ShardScatterParams shardStopParams;
    shardStopParams.density = 22.0f;
    shardStopParams.grainMs = 70.0f;
    shardStopParams.guardMs = 70.0f;
    shardStopParams.scatterMs = 1400.0f;
    shardStopParams.pitch = -0.55f;
    shardStopParams.pitchSpread = 0.75f;
    shardStopParams.rotate = -2.0f;
    shardStopParams.width = 0.80f;
    shardStopParams.feedback = 0.62f;
    shardStopParams.dry = 0.02f;
    shardStopParams.wet = 0.95f;
    shardStopParams.gainDb = -7.0f;
    shardStop.setParams(shardStopParams);
    float shardStopPeak = 0.0f;
    float shardStopMaxStep = 0.0f;
    float shardStopTailPeak = 0.0f;
    std::array<float, s3g::kShardScatterChannels> shardStopLast {};
    for (uint32_t block = 0; block < 132u; ++block) {
        const bool running = block < 72u;
        for (uint32_t i = 0; i < 512u; ++i) {
            const float t = static_cast<float>(block * 512u + i) / 48000.0f;
            shardInL[i] = running ? (std::sin(6.28318530718f * 123.0f * t) + std::sin(6.28318530718f * 971.0f * t) * 0.36f) * 0.18f : 0.0f;
            shardInR[i] = running ? (std::sin(6.28318530718f * 181.0f * t) + std::sin(6.28318530718f * 701.0f * t) * 0.31f) * 0.17f : 0.0f;
        }
        shardStop.process(shardInL.data(), shardInR.data(), shardOut.data(), 512u);
        for (uint32_t ch = 0; ch < s3g::kShardScatterChannels; ++ch) {
            for (float value : shardOutBuffers[ch]) {
                if (!std::isfinite(value)) {
                    std::cerr << "Shard Scatter stop output is not finite\n";
                    return 1;
                }
                shardStopPeak = std::max(shardStopPeak, std::abs(value));
                if (!running) {
                    shardStopMaxStep = std::max(shardStopMaxStep, std::abs(value - shardStopLast[ch]));
                    if (block >= 120u) shardStopTailPeak = std::max(shardStopTailPeak, std::abs(value));
                }
                shardStopLast[ch] = value;
            }
        }
    }
    if (shardStopPeak <= 0.00001f || shardStopPeak > 1.0f || shardStopMaxStep > 0.25f || shardStopTailPeak > 0.015f) {
        std::cerr << "Shard Scatter stop de-click outside expected range: peak=" << shardStopPeak
                  << " step=" << shardStopMaxStep << " tail=" << shardStopTailPeak << "\n";
        return 1;
    }

    s3g::OrbitDelay orbitDelay;
    if (!orbitDelay.prepare(48000.0, 512u)) {
        std::cerr << "Orbit Delay prepare failed\n";
        return 1;
    }
    s3g::OrbitDelayParams orbitParams;
    orbitParams.pos = 3.0f;
    orbitParams.spread = 5.5f;
    orbitParams.rotate = 0.18f;
    orbitParams.width = 4.0f;
    orbitParams.focus = 1.8f;
    orbitParams.delayMs = 95.0f;
    orbitParams.feedback = 0.42f;
    orbitParams.orbit = 1.5f;
    orbitParams.damp = 0.45f;
    orbitParams.wet = 0.72f;
    orbitParams.gainDb = -9.0f;
    orbitParams.stereo = 0.65f;
    orbitDelay.setParams(orbitParams);
    std::array<float, 512> orbitInL {};
    std::array<float, 512> orbitInR {};
    std::array<std::array<float, 512>, s3g::kOrbitDelayChannels> orbitOutBuffers {};
    std::array<float*, s3g::kOrbitDelayChannels> orbitOut {};
    for (uint32_t ch = 0; ch < s3g::kOrbitDelayChannels; ++ch) orbitOut[ch] = orbitOutBuffers[ch].data();
    float orbitPeak = 0.0f;
    float orbitMaxStep = 0.0f;
    std::array<float, s3g::kOrbitDelayChannels> orbitLast {};
    for (uint32_t block = 0; block < 260u; ++block) {
        if ((block % 26u) == 0u) {
            const bool alt = ((block / 26u) % 2u) != 0u;
            orbitParams.pos = alt ? 13.0f : 2.0f;
            orbitParams.spread = alt ? 10.0f : 2.5f;
            orbitParams.rotate = alt ? -1.4f : 0.55f;
            orbitParams.width = alt ? 9.5f : 1.2f;
            orbitParams.focus = alt ? 3.2f : 1.1f;
            orbitParams.delayMs = alt ? 420.0f : 55.0f;
            orbitParams.feedback = alt ? 0.72f : 0.16f;
            orbitParams.orbit = alt ? -3.0f : 2.0f;
            orbitParams.damp = alt ? 0.70f : 0.18f;
            orbitParams.wet = alt ? 0.92f : 0.42f;
            orbitParams.gainDb = alt ? -12.0f : -7.0f;
            orbitParams.stereo = alt ? 1.0f : 0.2f;
            orbitDelay.setParams(orbitParams);
        }
        for (uint32_t i = 0; i < 512u; ++i) {
            const float t = static_cast<float>(block * 512u + i) / 48000.0f;
            orbitInL[i] = (std::sin(6.28318530718f * 113.0f * t) + std::sin(6.28318530718f * 523.0f * t) * 0.33f) * 0.14f;
            orbitInR[i] = (std::sin(6.28318530718f * 151.0f * t) + std::sin(6.28318530718f * 739.0f * t) * 0.27f) * 0.13f;
        }
        orbitDelay.process(orbitInL.data(), orbitInR.data(), orbitOut.data(), 512u);
        for (uint32_t ch = 0; ch < s3g::kOrbitDelayChannels; ++ch) {
            for (float value : orbitOutBuffers[ch]) {
                if (!std::isfinite(value)) {
                    std::cerr << "Orbit Delay output is not finite\n";
                    return 1;
                }
                orbitPeak = std::max(orbitPeak, std::abs(value));
                orbitMaxStep = std::max(orbitMaxStep, std::abs(value - orbitLast[ch]));
                orbitLast[ch] = value;
            }
        }
    }
    if (orbitPeak <= 0.00001f || orbitPeak > 1.0f || orbitMaxStep > 0.75f) {
        std::cerr << "Orbit Delay peak/step outside expected range: " << orbitPeak << " / " << orbitMaxStep << "\n";
        return 1;
    }

    s3g::OrbitDelay orbitStop;
    if (!orbitStop.prepare(48000.0, 512u)) {
        std::cerr << "Orbit Delay stop prepare failed\n";
        return 1;
    }
    s3g::OrbitDelayParams orbitStopParams;
    orbitStopParams.pos = 5.0f;
    orbitStopParams.spread = 8.0f;
    orbitStopParams.rotate = 1.1f;
    orbitStopParams.width = 7.5f;
    orbitStopParams.focus = 2.4f;
    orbitStopParams.delayMs = 360.0f;
    orbitStopParams.feedback = 0.76f;
    orbitStopParams.orbit = -2.5f;
    orbitStopParams.damp = 0.28f;
    orbitStopParams.wet = 0.92f;
    orbitStopParams.gainDb = -10.0f;
    orbitStopParams.stereo = 1.0f;
    orbitStop.setParams(orbitStopParams);
    float orbitStopPeak = 0.0f;
    float orbitStopMaxStep = 0.0f;
    float orbitStopTailPeak = 0.0f;
    std::array<float, s3g::kOrbitDelayChannels> orbitStopLast {};
    for (uint32_t block = 0; block < 160u; ++block) {
        const bool running = block < 84u;
        for (uint32_t i = 0; i < 512u; ++i) {
            const float t = static_cast<float>(block * 512u + i) / 48000.0f;
            orbitInL[i] = running ? (std::sin(6.28318530718f * 107.0f * t) + std::sin(6.28318530718f * 619.0f * t) * 0.38f) * 0.16f : 0.0f;
            orbitInR[i] = running ? (std::sin(6.28318530718f * 149.0f * t) + std::sin(6.28318530718f * 887.0f * t) * 0.32f) * 0.15f : 0.0f;
        }
        orbitStop.process(orbitInL.data(), orbitInR.data(), orbitOut.data(), 512u);
        for (uint32_t ch = 0; ch < s3g::kOrbitDelayChannels; ++ch) {
            for (float value : orbitOutBuffers[ch]) {
                if (!std::isfinite(value)) {
                    std::cerr << "Orbit Delay stop output is not finite\n";
                    return 1;
                }
                orbitStopPeak = std::max(orbitStopPeak, std::abs(value));
                if (!running) {
                    orbitStopMaxStep = std::max(orbitStopMaxStep, std::abs(value - orbitStopLast[ch]));
                    if (block >= 148u) orbitStopTailPeak = std::max(orbitStopTailPeak, std::abs(value));
                }
                orbitStopLast[ch] = value;
            }
        }
    }
    if (orbitStopPeak <= 0.00001f || orbitStopPeak > 1.0f || orbitStopMaxStep > 0.20f || orbitStopTailPeak > 0.02f) {
        std::cerr << "Orbit Delay stop de-click outside expected range: peak=" << orbitStopPeak
                  << " step=" << orbitStopMaxStep << " tail=" << orbitStopTailPeak << "\n";
        return 1;
    }

    s3g::CascadeTaps cascadeTaps;
    if (!cascadeTaps.prepare(48000.0, 512u)) {
        std::cerr << "Cascade Taps prepare failed\n";
        return 1;
    }
    s3g::CascadeTapsParams cascadeParams;
    cascadeParams.pos = 4.0f;
    cascadeParams.rotate = 0.12f;
    cascadeParams.direction = 1.0f;
    cascadeParams.baseMs = 18.0f;
    cascadeParams.stepMs = 42.0f;
    cascadeParams.decay = 0.74f;
    cascadeParams.damp = 0.35f;
    cascadeParams.dry = 0.18f;
    cascadeParams.wet = 0.82f;
    cascadeParams.gainDb = -8.5f;
    cascadeParams.stereo = 0.5f;
    cascadeParams.soft = 0.62f;
    cascadeTaps.setParams(cascadeParams);
    std::array<float, 512> cascadeInL {};
    std::array<float, 512> cascadeInR {};
    std::array<std::array<float, 512>, s3g::kCascadeTapsChannels> cascadeOutBuffers {};
    std::array<float*, s3g::kCascadeTapsChannels> cascadeOut {};
    for (uint32_t ch = 0; ch < s3g::kCascadeTapsChannels; ++ch) cascadeOut[ch] = cascadeOutBuffers[ch].data();
    float cascadePeak = 0.0f;
    float cascadeMaxStep = 0.0f;
    std::array<float, s3g::kCascadeTapsChannels> cascadeLast {};
    for (uint32_t block = 0; block < 220u; ++block) {
        if ((block % 22u) == 0u) {
            const bool alt = ((block / 22u) % 2u) != 0u;
            cascadeParams.pos = alt ? 15.0f : 3.0f;
            cascadeParams.rotate = alt ? -0.9f : 0.35f;
            cascadeParams.direction = alt ? -1.0f : 1.0f;
            cascadeParams.baseMs = alt ? 70.0f : 12.0f;
            cascadeParams.stepMs = alt ? 118.0f : 26.0f;
            cascadeParams.decay = alt ? 0.92f : 0.58f;
            cascadeParams.damp = alt ? 0.62f : 0.12f;
            cascadeParams.dry = alt ? 0.06f : 0.28f;
            cascadeParams.wet = alt ? 0.95f : 0.58f;
            cascadeParams.gainDb = alt ? -12.0f : -6.0f;
            cascadeParams.stereo = alt ? 1.0f : 0.0f;
            cascadeParams.soft = alt ? 0.78f : 0.58f;
            cascadeTaps.setParams(cascadeParams);
        }
        for (uint32_t i = 0; i < 512u; ++i) {
            const float t = static_cast<float>(block * 512u + i) / 48000.0f;
            cascadeInL[i] = (std::sin(6.28318530718f * 83.0f * t) + std::sin(6.28318530718f * 611.0f * t) * 0.40f) * 0.13f;
            cascadeInR[i] = (std::sin(6.28318530718f * 127.0f * t) + std::sin(6.28318530718f * 887.0f * t) * 0.32f) * 0.12f;
        }
        cascadeTaps.process(cascadeInL.data(), cascadeInR.data(), cascadeOut.data(), 512u);
        for (uint32_t ch = 0; ch < s3g::kCascadeTapsChannels; ++ch) {
            for (float value : cascadeOutBuffers[ch]) {
                if (!std::isfinite(value)) {
                    std::cerr << "Cascade Taps output is not finite\n";
                    return 1;
                }
                cascadePeak = std::max(cascadePeak, std::abs(value));
                cascadeMaxStep = std::max(cascadeMaxStep, std::abs(value - cascadeLast[ch]));
                cascadeLast[ch] = value;
            }
        }
    }
    if (cascadePeak <= 0.00001f || cascadePeak > 1.0f || cascadeMaxStep > 0.75f) {
        std::cerr << "Cascade Taps peak/step outside expected range: " << cascadePeak << " / " << cascadeMaxStep << "\n";
        return 1;
    }

    s3g::CascadeTaps cascadeImpulse;
    if (!cascadeImpulse.prepare(48000.0, 512u)) {
        std::cerr << "Cascade Taps impulse prepare failed\n";
        return 1;
    }
    s3g::CascadeTapsParams cascadeImpulseParams;
    cascadeImpulseParams.pos = 1.0f;
    cascadeImpulseParams.rotate = 1.3f;
    cascadeImpulseParams.direction = -1.0f;
    cascadeImpulseParams.baseMs = 6.0f;
    cascadeImpulseParams.stepMs = 18.0f;
    cascadeImpulseParams.decay = 0.90f;
    cascadeImpulseParams.damp = 0.04f;
    cascadeImpulseParams.dry = 0.0f;
    cascadeImpulseParams.wet = 1.0f;
    cascadeImpulseParams.gainDb = -5.0f;
    cascadeImpulseParams.stereo = 1.0f;
    cascadeImpulseParams.soft = 0.88f;
    cascadeImpulse.setParams(cascadeImpulseParams);
    float cascadeImpulsePeak = 0.0f;
    float cascadeImpulseMaxStep = 0.0f;
    std::array<float, s3g::kCascadeTapsChannels> cascadeImpulseLast {};
    for (uint32_t block = 0; block < 180u; ++block) {
        for (uint32_t i = 0; i < 512u; ++i) {
            const uint32_t sample = block * 512u + i;
            const bool hit = (sample % 4096u) == 0u || (sample % 6173u) == 0u;
            cascadeInL[i] = hit ? 0.82f : 0.0f;
            cascadeInR[i] = hit ? -0.74f : 0.0f;
        }
        cascadeImpulse.process(cascadeInL.data(), cascadeInR.data(), cascadeOut.data(), 512u);
        for (uint32_t ch = 0; ch < s3g::kCascadeTapsChannels; ++ch) {
            for (float value : cascadeOutBuffers[ch]) {
                if (!std::isfinite(value)) {
                    std::cerr << "Cascade Taps impulse output is not finite\n";
                    return 1;
                }
                cascadeImpulsePeak = std::max(cascadeImpulsePeak, std::abs(value));
                cascadeImpulseMaxStep = std::max(cascadeImpulseMaxStep, std::abs(value - cascadeImpulseLast[ch]));
                cascadeImpulseLast[ch] = value;
            }
        }
    }
    if (cascadeImpulsePeak <= 0.00001f || cascadeImpulsePeak > 1.0f || cascadeImpulseMaxStep > 0.08f) {
        std::cerr << "Cascade Taps impulse suppression outside expected range: " << cascadeImpulsePeak
                  << " / " << cascadeImpulseMaxStep << "\n";
        return 1;
    }

#if S3G_HAS_ACCELERATE_FFT
    s3g::SpectralFftProcessor spectral;
    if (!spectral.prepare(2u, 1024u, 4u)) {
        std::cerr << "Spectral FFT prepare failed\n";
        return 1;
    }
    if (spectral.hopSize() != 256u || spectral.bins() != 513u) {
        std::cerr << "Spectral FFT geometry failed\n";
        return 1;
    }

    constexpr uint32_t spectralFrames = 8192u;
    std::array<float, spectralFrames> spectralInL {};
    std::array<float, spectralFrames> spectralInR {};
    std::array<float, spectralFrames> spectralOutL {};
    std::array<float, spectralFrames> spectralOutR {};
    for (uint32_t i = 0; i < spectralFrames; ++i) {
        const float t = static_cast<float>(i) / 48000.0f;
        spectralInL[i] = std::sin(6.28318530718f * 440.0f * t) * 0.2f;
        spectralInR[i] = std::sin(6.28318530718f * 713.0f * t) * 0.17f;
    }
    const float* spectralIn[2] = { spectralInL.data(), spectralInR.data() };
    float* spectralOut[2] = { spectralOutL.data(), spectralOutR.data() };
    uint32_t spectralKernelCalls = 0u;
    spectral.process(spectralIn, spectralOut, spectralFrames, [&](s3g::SpectralFrameView frame) {
        if (frame.bins != 513u || frame.fftSize != 1024u || frame.channel > 1u) {
            std::cerr << "Spectral FFT frame metadata failed\n";
            std::exit(1);
        }
        ++spectralKernelCalls;
    });
    if (spectralKernelCalls == 0u) {
        std::cerr << "Spectral FFT kernel was not called\n";
        return 1;
    }

    float spectralErr = 0.0f;
    float spectralPeak = 0.0f;
    const uint32_t spectralLatency = spectral.fftSize();
    for (uint32_t i = 2048u; i + spectralLatency < spectralFrames - 1024u; ++i) {
        const uint32_t outIndex = i + spectralLatency;
        if (!std::isfinite(spectralOutL[outIndex]) || !std::isfinite(spectralOutR[outIndex])) {
            std::cerr << "Spectral FFT output is not finite\n";
            return 1;
        }
        spectralPeak = std::max(spectralPeak, std::abs(spectralOutL[outIndex]));
        spectralPeak = std::max(spectralPeak, std::abs(spectralOutR[outIndex]));
        spectralErr = std::max(spectralErr, std::abs(spectralOutL[outIndex] - spectralInL[i]));
        spectralErr = std::max(spectralErr, std::abs(spectralOutR[outIndex] - spectralInR[i]));
    }
    if (spectralPeak <= 0.00001f || spectralErr > 0.02f) {
        std::cerr << "Spectral FFT passthrough outside expected range: peak=" << spectralPeak << " err=" << spectralErr << "\n";
        return 1;
    }
#endif

#if S3G_HAS_ACCELERATE_FFT
    s3g::SpectralSpray spectralSpray;
    if (!spectralSpray.prepare(48000.0, 2u, 1024u, 4u, 512u)) {
        std::cerr << "Spectral Spray prepare failed\n";
        return 1;
    }
    s3g::SpectralSprayParams sprayParams;
    sprayParams.mix = 1.0f;
    sprayParams.gainDb = -6.0f;
    sprayParams.sprayBins = 18.0f;
    sprayParams.drift = 0.35f;
    sprayParams.feedback = 0.22f;
    sprayParams.smear = 0.45f;
    sprayParams.phaseBlur = 0.25f;
    spectralSpray.setParams(sprayParams);
    std::array<float, 512> sprayInL {};
    std::array<float, 512> sprayInR {};
    std::array<float, 512> sprayOutL {};
    std::array<float, 512> sprayOutR {};
    const float* sprayIn[2] = { sprayInL.data(), sprayInR.data() };
    float* sprayOut[2] = { sprayOutL.data(), sprayOutR.data() };
    float spectralSprayPeak = 0.0f;
    for (uint32_t block = 0; block < 40u; ++block) {
        for (uint32_t i = 0; i < 512u; ++i) {
            const float t = static_cast<float>(block * 512u + i) / 48000.0f;
            sprayInL[i] = std::sin(6.28318530718f * 220.0f * t) * 0.12f + std::sin(6.28318530718f * 810.0f * t) * 0.06f;
            sprayInR[i] = std::sin(6.28318530718f * 330.0f * t) * 0.11f + std::sin(6.28318530718f * 1210.0f * t) * 0.05f;
        }
        spectralSpray.process(sprayIn, sprayOut, 512u);
        for (uint32_t i = 0; i < 512u; ++i) {
            if (!std::isfinite(sprayOutL[i]) || !std::isfinite(sprayOutR[i])) {
                std::cerr << "Spectral Spray output is not finite\n";
                return 1;
            }
            spectralSprayPeak = std::max(spectralSprayPeak, std::abs(sprayOutL[i]));
            spectralSprayPeak = std::max(spectralSprayPeak, std::abs(sprayOutR[i]));
        }
    }
    if (spectralSprayPeak <= 0.00001f || spectralSprayPeak > 1.0f) {
        std::cerr << "Spectral Spray peak outside expected range: " << spectralSprayPeak << "\n";
        return 1;
    }

    sprayParams.feedback = 0.85f;
    sprayParams.freeze = 0.35f;
    sprayParams.hold = 0.92f;
    sprayParams.gainDb = -9.0f;
    spectralSpray.setParams(sprayParams);
    float spectralSprayFeedbackPeak = 0.0f;
    for (uint32_t block = 0; block < 80u; ++block) {
        for (uint32_t i = 0; i < 512u; ++i) {
            const float t = static_cast<float>(block * 512u + i) / 48000.0f;
            sprayInL[i] = std::sin(6.28318530718f * 147.0f * t) * 0.18f;
            sprayInR[i] = std::sin(6.28318530718f * 311.0f * t) * 0.16f;
        }
        spectralSpray.process(sprayIn, sprayOut, 512u);
        for (uint32_t i = 0; i < 512u; ++i) {
            if (!std::isfinite(sprayOutL[i]) || !std::isfinite(sprayOutR[i])) {
                std::cerr << "Spectral Spray high-feedback output is not finite\n";
                return 1;
            }
            spectralSprayFeedbackPeak = std::max(spectralSprayFeedbackPeak, std::abs(sprayOutL[i]));
            spectralSprayFeedbackPeak = std::max(spectralSprayFeedbackPeak, std::abs(sprayOutR[i]));
        }
    }
    if (spectralSprayFeedbackPeak <= 0.00001f || spectralSprayFeedbackPeak > 1.0f) {
        std::cerr << "Spectral Spray high-feedback peak outside expected range: " << spectralSprayFeedbackPeak << "\n";
        return 1;
    }

    spectralSpray.reset();
    sprayParams = s3g::SpectralSprayParams {};
    sprayParams.gainDb = -6.0f;
    spectralSpray.setParams(sprayParams);
    float spectralSprayAutomationPeak = 0.0f;
    float spectralSprayAutomationMaxStep = 0.0f;
    float lastAutoL = 0.0f;
    float lastAutoR = 0.0f;
    for (uint32_t block = 0; block < 96u; ++block) {
        if ((block % 6u) == 0u) {
            const bool high = ((block / 6u) % 2u) != 0u;
            sprayParams.sprayBins = high ? 192.0f : 2.0f;
            sprayParams.drift = high ? 1.0f : 0.0f;
            sprayParams.hold = high ? 0.94f : 0.15f;
            sprayParams.freeze = high ? 0.55f : 0.0f;
            sprayParams.feedback = high ? 0.85f : 0.0f;
            sprayParams.smear = high ? 1.0f : 0.0f;
            sprayParams.holes = high ? 0.72f : 0.0f;
            sprayParams.phaseBlur = high ? 1.0f : 0.0f;
            sprayParams.loFreq = high ? 900.0f : 0.0f;
            sprayParams.hiFreq = high ? 4200.0f : 20000.0f;
            sprayParams.mix = high ? 1.0f : 0.18f;
            sprayParams.safety = high ? 0.38f : 0.92f;
            spectralSpray.setParams(sprayParams);
        }
        for (uint32_t i = 0; i < 512u; ++i) {
            const float t = static_cast<float>(block * 512u + i) / 48000.0f;
            sprayInL[i] = (std::sin(6.28318530718f * 173.0f * t) + std::sin(6.28318530718f * 1301.0f * t) * 0.45f) * 0.12f;
            sprayInR[i] = (std::sin(6.28318530718f * 257.0f * t) + std::sin(6.28318530718f * 1733.0f * t) * 0.40f) * 0.12f;
        }
        spectralSpray.process(sprayIn, sprayOut, 512u);
        for (uint32_t i = 0; i < 512u; ++i) {
            if (!std::isfinite(sprayOutL[i]) || !std::isfinite(sprayOutR[i])) {
                std::cerr << "Spectral Spray automation output is not finite\n";
                return 1;
            }
            spectralSprayAutomationPeak = std::max(spectralSprayAutomationPeak, std::abs(sprayOutL[i]));
            spectralSprayAutomationPeak = std::max(spectralSprayAutomationPeak, std::abs(sprayOutR[i]));
            spectralSprayAutomationMaxStep = std::max(spectralSprayAutomationMaxStep, std::abs(sprayOutL[i] - lastAutoL));
            spectralSprayAutomationMaxStep = std::max(spectralSprayAutomationMaxStep, std::abs(sprayOutR[i] - lastAutoR));
            lastAutoL = sprayOutL[i];
            lastAutoR = sprayOutR[i];
        }
    }
    if (spectralSprayAutomationPeak <= 0.00001f || spectralSprayAutomationPeak > 1.0f || spectralSprayAutomationMaxStep > 0.65f) {
        std::cerr << "Spectral Spray automation stress outside expected range: peak=" << spectralSprayAutomationPeak
                  << " step=" << spectralSprayAutomationMaxStep << "\n";
        return 1;
    }

    s3g::SpectralSpray spectralSpray8;
    if (!spectralSpray8.prepare(48000.0, 8u, 1024u, 4u, 512u)) {
        std::cerr << "8ch Spectral Spray prepare failed\n";
        return 1;
    }
    s3g::SpectralSprayParams spray8Params;
    spray8Params.mix = 1.0f;
    spray8Params.gainDb = -9.0f;
    spray8Params.sprayBins = 28.0f;
    spray8Params.drift = 0.42f;
    spray8Params.hold = 0.76f;
    spray8Params.feedback = 0.20f;
    spray8Params.smear = 0.52f;
    spray8Params.phaseBlur = 0.30f;
    spray8Params.safety = 0.76f;
    spectralSpray8.setParams(spray8Params);
    std::array<std::array<float, 512>, 8> spray8InBuffers {};
    std::array<std::array<float, 512>, 8> spray8OutBuffers {};
    std::array<const float*, 8> spray8In {};
    std::array<float*, 8> spray8Out {};
    for (uint32_t ch = 0; ch < 8u; ++ch) {
        spray8In[ch] = spray8InBuffers[ch].data();
        spray8Out[ch] = spray8OutBuffers[ch].data();
    }
    float spectralSpray8Peak = 0.0f;
    float spectralSpray8MaxStep = 0.0f;
    std::array<float, 8> spectralSpray8Last {};
    for (uint32_t block = 0; block < 56u; ++block) {
        for (uint32_t i = 0; i < 512u; ++i) {
            const float t = static_cast<float>(block * 512u + i) / 48000.0f;
            for (uint32_t ch = 0; ch < 8u; ++ch) {
                const float base = 111.0f + static_cast<float>(ch) * 53.0f;
                spray8InBuffers[ch][i] = (std::sin(6.28318530718f * base * t + static_cast<float>(ch) * 0.31f)
                    + std::sin(6.28318530718f * (base * 3.17f) * t) * 0.38f) * 0.09f;
            }
        }
        spectralSpray8.process(spray8In.data(), spray8Out.data(), 512u);
        for (uint32_t ch = 0; ch < 8u; ++ch) {
            for (float value : spray8OutBuffers[ch]) {
                if (!std::isfinite(value)) {
                    std::cerr << "8ch Spectral Spray output is not finite\n";
                    return 1;
                }
                spectralSpray8Peak = std::max(spectralSpray8Peak, std::abs(value));
                spectralSpray8MaxStep = std::max(spectralSpray8MaxStep, std::abs(value - spectralSpray8Last[ch]));
                spectralSpray8Last[ch] = value;
            }
        }
    }
    if (spectralSpray8Peak <= 0.00001f || spectralSpray8Peak > 1.0f || spectralSpray8MaxStep > 0.65f) {
        std::cerr << "8ch Spectral Spray stress outside expected range: peak=" << spectralSpray8Peak
                  << " step=" << spectralSpray8MaxStep << "\n";
        return 1;
    }

    s3g::SpectralTopologyProcessor spectralTopology;
    if (!spectralTopology.prepare(48000.0, s3g::kSpectralTopologyChannels, 1024u, 4u, 512u)) {
        std::cerr << "Spectral Topology prepare failed\n";
        return 1;
    }
    s3g::SpectralTopologySettings spectralTopologySettings;
    spectralTopologySettings.base = s3g::SpectralSprayParams {};
    spectralTopologySettings.base.mix = 1.0f;
    spectralTopologySettings.base.gainDb = -9.0f;
    spectralTopologySettings.base.sprayBins = 22.0f;
    spectralTopologySettings.base.drift = 0.30f;
    spectralTopologySettings.base.hold = 0.70f;
    spectralTopologySettings.base.feedback = 0.18f;
    spectralTopologySettings.base.smear = 0.45f;
    spectralTopologySettings.base.phaseBlur = 0.24f;
    spectralTopologySettings.base.safety = 0.76f;
    spectralTopologySettings.topology.amount = 0.65;
    spectralTopologySettings.topology.jitter = 0.22;
    spectralTopologySettings.topology.collapse = 0.18;
    spectralTopologySettings.topology.dirX = 0.30;
    spectralTopologySettings.topology.dirY = -0.18;
    spectralTopologySettings.topology.dirZ = 0.90;
    spectralTopologySettings.topology.twist = 0.24;
    spectralTopologySettings.topology.flare = 0.12;
    spectralTopologySettings.topology.shape = 11;
    spectralTopologySettings.topology.neighborCount = 2;
    spectralTopologySettings.topology.neighborRadius = 0.68;
    spectralTopologySettings.topology.centroidAmount = 0.24;
    for (uint32_t ch = 0; ch < s3g::kSpectralTopologyChannels; ++ch) {
        spectralTopology.setLaneParams(ch, s3g::spectralTopologyLaneParams(spectralTopologySettings, ch, s3g::kSpectralTopologyChannels));
    }
    std::array<std::array<float, 512>, s3g::kSpectralTopologyChannels> spectralTopologyInBuffers {};
    std::array<std::array<float, 512>, s3g::kSpectralTopologyChannels> spectralTopologyOutBuffers {};
    std::array<const float*, s3g::kSpectralTopologyChannels> spectralTopologyIn {};
    std::array<float*, s3g::kSpectralTopologyChannels> spectralTopologyOut {};
    for (uint32_t ch = 0; ch < s3g::kSpectralTopologyChannels; ++ch) {
        spectralTopologyIn[ch] = spectralTopologyInBuffers[ch].data();
        spectralTopologyOut[ch] = spectralTopologyOutBuffers[ch].data();
    }
    float spectralTopologyPeak = 0.0f;
    float spectralTopologyMaxStep = 0.0f;
    std::array<float, s3g::kSpectralTopologyChannels> spectralTopologyLast {};
    for (uint32_t block = 0; block < 64u; ++block) {
        if ((block % 8u) == 0u) {
            spectralTopologySettings.topology.motionPhase = static_cast<double>(block) * 0.027;
            spectralTopologySettings.topology.motionMode = 1;
            spectralTopologySettings.topology.motionDepth = 0.55;
            for (uint32_t ch = 0; ch < s3g::kSpectralTopologyChannels; ++ch) {
                spectralTopology.setLaneParams(ch, s3g::spectralTopologyLaneParams(spectralTopologySettings, ch, s3g::kSpectralTopologyChannels));
            }
        }
        for (uint32_t i = 0; i < 512u; ++i) {
            const float t = static_cast<float>(block * 512u + i) / 48000.0f;
            for (uint32_t ch = 0; ch < s3g::kSpectralTopologyChannels; ++ch) {
                const float base = 97.0f + static_cast<float>(ch) * 41.0f;
                spectralTopologyInBuffers[ch][i] = (std::sin(6.28318530718f * base * t)
                    + std::sin(6.28318530718f * (base * 4.23f) * t + static_cast<float>(ch) * 0.19f) * 0.32f) * 0.08f;
            }
        }
        spectralTopology.process(spectralTopologyIn.data(),
            s3g::kSpectralTopologyChannels,
            spectralTopologyOut.data(),
            s3g::kSpectralTopologyChannels,
            512u);
        for (uint32_t ch = 0; ch < s3g::kSpectralTopologyChannels; ++ch) {
            for (float value : spectralTopologyOutBuffers[ch]) {
                if (!std::isfinite(value)) {
                    std::cerr << "Spectral Topology output is not finite\n";
                    return 1;
                }
                spectralTopologyPeak = std::max(spectralTopologyPeak, std::abs(value));
                spectralTopologyMaxStep = std::max(spectralTopologyMaxStep, std::abs(value - spectralTopologyLast[ch]));
                spectralTopologyLast[ch] = value;
            }
        }
    }
    if (spectralTopologyPeak <= 0.00001f || spectralTopologyPeak > 1.0f || spectralTopologyMaxStep > 0.70f) {
        std::cerr << "Spectral Topology stress outside expected range: peak=" << spectralTopologyPeak
                  << " step=" << spectralTopologyMaxStep << "\n";
        return 1;
    }
#endif

    {
        auto nodeMixer = std::make_unique<s3g::NodeTrackMixer>();
        nodeMixer->prepare(48000.0);
        s3g::NodeTrackMixerParams nodeParams {};
        if (std::abs(nodeParams.cursorInfluence - 1.0f) > 0.0001f) {
            std::cerr << "Node track mixer cursor influence should default to 100%\n";
            return 1;
        }
        nodeParams.outputLayout = s3g::NodeTrackLayout::Octo;
        nodeParams.outputChannels = 8;
        nodeParams.nodeCount = 2;
        nodeParams.nodes[0].sourceLayout = s3g::NodeTrackLayout::Quad;
        nodeParams.nodes[0].sourceChannels = 4;
        nodeParams.nodes[0].inputStart = 1;
        nodeParams.nodes[0].levelDb = -3.0f;
        nodeParams.nodes[1].sourceLayout = s3g::NodeTrackLayout::Cube;
        nodeParams.nodes[1].sourceChannels = 8;
        nodeParams.nodes[1].inputStart = 5;
        nodeParams.nodes[1].levelDb = -9.0f;
        nodeParams.nodes[1].x = 0.4f;
        nodeParams.nodes[1].z = 0.3f;
        nodeParams.cursorInfluence = 0.35f;
        nodeParams.cursorRadius = 2.0f;
        nodeParams.cursorX = 0.25f;
        nodeMixer->setParams(nodeParams);

        constexpr uint32_t nodeFrames = 8;
        std::array<std::array<float, nodeFrames>, s3g::kNodeTrackMixerMaxChannels> nodeIn {};
        std::array<std::array<float, nodeFrames>, s3g::kNodeTrackMixerMaxChannels> nodeOut {};
        std::array<float*, s3g::kNodeTrackMixerMaxChannels> nodeInPtrs {};
        std::array<float*, s3g::kNodeTrackMixerMaxChannels> nodeOutPtrs {};
        for (uint32_t ch = 0; ch < s3g::kNodeTrackMixerMaxChannels; ++ch) {
            nodeInPtrs[ch] = nodeIn[ch].data();
            nodeOutPtrs[ch] = nodeOut[ch].data();
            for (uint32_t frame = 0; frame < nodeFrames; ++frame) {
                nodeIn[ch][frame] = static_cast<float>(ch + 1u) * 0.002f + static_cast<float>(frame) * 0.0003f;
            }
        }
        nodeMixer->process(nodeInPtrs.data(), s3g::kNodeTrackMixerMaxChannels, nodeOutPtrs.data(), s3g::kNodeTrackMixerMaxChannels, nodeFrames);
        float nodePeak = 0.0f;
        for (uint32_t ch = 0; ch < 8u; ++ch) {
            for (float value : nodeOut[ch]) {
                if (!std::isfinite(value)) {
                    std::cerr << "Node track mixer output is not finite\n";
                    return 1;
                }
                nodePeak = std::max(nodePeak, std::abs(value));
            }
        }
        if (nodePeak <= 0.000001f || nodePeak > 1.0f) {
            std::cerr << "Node track mixer peak outside expected range: " << nodePeak << "\n";
            return 1;
        }
        {
            s3g::NodeTrackMixer hillMixer;
            hillMixer.prepare(48000.0);
            s3g::NodeTrackMixerParams hillParams {};
            hillParams.nodeCount = 1;
            hillParams.nodes[0].scale = 1.0f;
            hillParams.nodes[0].focus = 1.0f;
            hillParams.cursorInfluence = 1.0f;
            hillParams.cursorX = 1.01f;
            hillMixer.setParams(hillParams);
            const float outside = hillMixer.nodeWeights()[0];
            hillParams.cursorX = 0.99f;
            hillMixer.setParams(hillParams);
            const float edge = hillMixer.nodeWeights()[0];
            hillParams.cursorX = 0.50f;
            hillMixer.setParams(hillParams);
            const float inner = hillMixer.nodeWeights()[0];
            if (outside != 0.0f || !(edge > 0.0f && edge < inner && inner < 1.0f)) {
                std::cerr << "Node track mixer cursor hill is not smooth at node edge\n";
                return 1;
            }

            const auto base = s3g::nodeTrackLayoutPoint(0, 4, s3g::NodeTrackLayout::Quad);
            const auto spun = s3g::nodeTrackRotatePoint(base, 90.0f, 30.0f);
            const float baseLen = std::sqrt(base.x * base.x + base.y * base.y + base.z * base.z);
            const float spunLen = std::sqrt(spun.x * spun.x + spun.y * spun.y + spun.z * spun.z);
            const float delta = std::abs(base.x - spun.x) + std::abs(base.y - spun.y) + std::abs(base.z - spun.z);
            if (std::abs(baseLen - spunLen) > 0.0001f || delta < 0.2f) {
                std::cerr << "Node track mixer az/el source rotation is not geometric\n";
                return 1;
            }

            hillParams.cursorZ = 1.5f;
            hillParams.nodes[0].z = -1.25f;
            hillParams.lockZ = true;
            const auto locked = s3g::sanitizeNodeTrackMixerParams(hillParams);
            hillParams.lockZ = false;
            const auto unlocked = s3g::sanitizeNodeTrackMixerParams(hillParams);
            if (locked.cursorZ != 0.0f || locked.nodes[0].z != 0.0f
                || std::abs(unlocked.cursorZ - 1.5f) > 0.0001f
                || std::abs(unlocked.nodes[0].z + 1.25f) > 0.0001f) {
                std::cerr << "Node track mixer Z lock did not preserve the flat mix plane\n";
                return 1;
            }
        }

        s3g::AmbiNodeTrackMixer ambiNodeMixer;
        ambiNodeMixer.prepare(48000.0);
        s3g::AmbiNodeTrackMixerParams ambiClampParams {};
        ambiClampParams.order = s3g::AmbiNodeTrackOrder::O1;
        ambiClampParams.nodeCount = 12;
        ambiClampParams.nodes[7].inputStart = 3;
        ambiNodeMixer.setParams(ambiClampParams);
        if (ambiNodeMixer.ambiChannels() != 16u
            || ambiNodeMixer.params().order != s3g::AmbiNodeTrackOrder::O3
            || ambiNodeMixer.params().nodeCount != s3g::kAmbiNodeBusMixerMaxNodes
            || ambiNodeMixer.params().nodes[7].inputStart != 113u) {
            std::cerr << "Ambi node bus mixer did not lock to 8 fixed 3OA nodes\n";
            return 1;
        }
        s3g::AmbiNodeTrackMixerParams ambiNodeParams {};
        ambiNodeParams.order = s3g::AmbiNodeTrackOrder::O3;
        ambiNodeParams.nodeCount = 2;
        ambiNodeParams.nodes[0].inputStart = 1;
        ambiNodeParams.nodes[0].levelDb = -3.0f;
        ambiNodeParams.nodes[0].x = -0.4f;
        ambiNodeParams.nodes[1].inputStart = 17;
        ambiNodeParams.nodes[1].levelDb = -9.0f;
        ambiNodeParams.nodes[1].x = 0.7f;
        ambiNodeParams.cursorInfluence = 0.4f;
        ambiNodeParams.cursorX = 0.6f;
        ambiNodeParams.cursorRadius = 2.0f;
        ambiNodeMixer.setParams(ambiNodeParams);

        std::array<std::array<float, nodeFrames>, s3g::kNodeTrackMixerMaxChannels> ambiNodeOut {};
        std::array<float*, s3g::kNodeTrackMixerMaxChannels> ambiNodeOutPtrs {};
        for (uint32_t ch = 0; ch < s3g::kNodeTrackMixerMaxChannels; ++ch) {
            ambiNodeOutPtrs[ch] = ambiNodeOut[ch].data();
        }
        ambiNodeMixer.process(nodeInPtrs.data(), s3g::kNodeTrackMixerMaxChannels, ambiNodeOutPtrs.data(), s3g::kNodeTrackMixerMaxChannels, nodeFrames);
        float ambiNodePeak = 0.0f;
        for (uint32_t ch = 0; ch < 16u; ++ch) {
            for (float value : ambiNodeOut[ch]) {
                if (!std::isfinite(value)) {
                    std::cerr << "Ambi node track mixer output is not finite\n";
                    return 1;
                }
                ambiNodePeak = std::max(ambiNodePeak, std::abs(value));
            }
        }
        if (ambiNodePeak <= 0.000001f || ambiNodePeak > 1.0f) {
            std::cerr << "Ambi node track mixer peak outside expected range: " << ambiNodePeak << "\n";
            return 1;
        }
    }


    s3g::WaveGeometryProcessor waveGeometry;
    waveGeometry.prepare(48000.0, s3g::kWaveGeometryChannels, 0u, 0u, 128u);
    s3g::WaveGeometrySettings waveGeometrySettings {};
    waveGeometrySettings.base.fold = 0.55f;
    waveGeometrySettings.base.drive = 0.45f;
    waveGeometrySettings.base.edge = 0.25f;
    waveGeometrySettings.base.bits = 0.25f;
    waveGeometrySettings.base.mix = 1.0f;
    waveGeometrySettings.topology.amount = 0.75;
    waveGeometrySettings.topology.dirX = 0.4;
    waveGeometrySettings.topology.dirY = 0.6;
    waveGeometrySettings.topology.dirZ = 0.2;
    for (uint32_t ch = 0; ch < s3g::kWaveGeometryChannels; ++ch) {
        waveGeometry.setLaneParams(ch, s3g::waveGeometryLaneParams(waveGeometrySettings, ch, s3g::kWaveGeometryChannels));
    }
    constexpr uint32_t waveFrames = 128u;
    std::array<std::array<float, waveFrames>, s3g::kWaveGeometryChannels> waveIn {};
    std::array<std::array<float, waveFrames>, s3g::kWaveGeometryChannels> waveOut {};
    std::array<const float*, s3g::kWaveGeometryChannels> waveInPtrs {};
    std::array<float*, s3g::kWaveGeometryChannels> waveOutPtrs {};
    for (uint32_t ch = 0; ch < s3g::kWaveGeometryChannels; ++ch) {
        waveInPtrs[ch] = waveIn[ch].data();
        waveOutPtrs[ch] = waveOut[ch].data();
        for (uint32_t frame = 0; frame < waveFrames; ++frame) {
            waveIn[ch][frame] = std::sin(static_cast<float>(frame) * 0.071f + static_cast<float>(ch) * 0.19f) * 0.25f;
        }
    }
    waveGeometry.process(waveInPtrs.data(), s3g::kWaveGeometryChannels, waveOutPtrs.data(), s3g::kWaveGeometryChannels, waveFrames);
    float waveGeometryPeak = 0.0f;
    float waveGeometryDelta = 0.0f;
    for (uint32_t ch = 0; ch < s3g::kWaveGeometryChannels; ++ch) {
        for (uint32_t frame = 0; frame < waveFrames; ++frame) {
            if (!std::isfinite(waveOut[ch][frame])) {
                std::cerr << "Wave geometry output is not finite\n";
                return 1;
            }
            waveGeometryPeak = std::max(waveGeometryPeak, std::abs(waveOut[ch][frame]));
            waveGeometryDelta = std::max(waveGeometryDelta, std::abs(waveOut[ch][frame] - waveIn[ch][frame]));
        }
    }
    if (waveGeometryPeak <= 0.000001f || waveGeometryPeak > 1.0f || waveGeometryDelta <= 0.00001f) {
        std::cerr << "Wave geometry peak/delta outside expected range: " << waveGeometryPeak << " / " << waveGeometryDelta << "\n";
        return 1;
    }

    std::cout << "s3g-dsp smoke test passed\n";
    std::cout << "layout speakers: " << s3g::kVirtualSpeakerCount << "\n";
    std::cout << "gain ch1 sample4: " << samples[0][3] << "\n";
    std::cout << "lane patch row8: " << patch.rowMask(7) << "\n";
    std::cout << "mc stereo L/R: " << stereoOut[0] << " / " << stereoOut[1] << "\n";
    std::cout << "mc quad L/R/RB/LB: " << quadOut[0] << " / " << quadOut[1] << " / " << quadOut[2] << " / " << quadOut[3] << "\n";
    std::cout << "delay processor impulse: " << delayOut[0] << "\n";
    std::cout << "delay processor stress peak: " << delayStressPeak << "\n";
    std::cout << "loop processor peak: " << loopPeak << "\n";
    std::cout << "loop processor XFD stress peak: " << loopXfdStressPeak << "\n";
    std::cout << "loop processor region stress peak/step: " << loopRegionStressPeak << " / " << loopRegionStressMaxStep << "\n";
    std::cout << "macro delay peak: " << macroPeak << "\n";
    std::cout << "macro delay tail peak: " << macroTailPeak << "\n";
    std::cout << "macro pitch peak: " << macroPitchPeak << "\n";
    std::cout << "buffer processor peak/step: " << bufferPeak << " / " << bufferMaxStep << "\n";
    std::cout << "wave geometry peak/delta: " << waveGeometryPeak << " / " << waveGeometryDelta << "\n";
    std::cout << "Ambi point encoder peak: " << pointEncoderPeak << "\n";
    std::cout << "Ambi speaker decoder peak: " << speakerDecoderPeak << "\n";
    std::cout << "3OAFX return W: " << hoaOut[0] << "\n";
    std::cout << "3OAFX single peaks: "
              << single3OafxPeak[0] << " / "
              << single3OafxPeak[1] << " / "
              << single3OafxPeak[2] << " / "
              << single3OafxPeak[3] << "\n";
    std::cout << "Ambi order/band W: " << ambiUtilityOutBuffers[0][0] << "\n";
    std::cout << "Ambi grain peak: " << ambiGrainPeak << "\n";
    std::cout << "Ambi grain async/env peak: " << ambiGrainAsyncPeak << "\n";
    std::cout << "Ambi grain long peak: " << ambiGrainLongPeak << "\n";
    std::cout << "Ambi grain lower-order peak: " << ambiGrainLowerOrderPeak << "\n";
    std::cout << "shard scatter peak/step: " << shardPeak << " / " << shardMaxStep << "\n";
    std::cout << "shard scatter sparse peak/step: " << shardSparsePeak << " / " << shardSparseMaxStep << "\n";
    std::cout << "shard scatter stop step/tail: " << shardStopMaxStep << " / " << shardStopTailPeak << "\n";
    std::cout << "orbit delay peak/step: " << orbitPeak << " / " << orbitMaxStep << "\n";
    std::cout << "orbit delay stop step/tail: " << orbitStopMaxStep << " / " << orbitStopTailPeak << "\n";
    std::cout << "cascade taps peak/step: " << cascadePeak << " / " << cascadeMaxStep << "\n";
    std::cout << "cascade taps impulse peak/step: " << cascadeImpulsePeak << " / " << cascadeImpulseMaxStep << "\n";
#if S3G_HAS_ACCELERATE_FFT
    std::cout << "spectral FFT passthrough err: " << spectralErr << "\n";
    std::cout << "spectral spray peak: " << spectralSprayPeak << "\n";
    std::cout << "spectral spray high-feedback peak: " << spectralSprayFeedbackPeak << "\n";
    std::cout << "spectral spray automation peak/step: " << spectralSprayAutomationPeak << " / " << spectralSprayAutomationMaxStep << "\n";
    std::cout << "8ch spectral spray peak/step: " << spectralSpray8Peak << " / " << spectralSpray8MaxStep << "\n";
    std::cout << "spectral topology peak/step: " << spectralTopologyPeak << " / " << spectralTopologyMaxStep << "\n";
#endif
    return 0;
}
