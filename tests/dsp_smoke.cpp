#include "s3g_24ch_layout.h"
#include "s3g_3oafx.h"
#include "s3g_ambisonic_point_encoder.h"
#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_delay_processor.h"
#include "s3g_gain.h"
#include "s3g_lane_patch.h"
#include "s3g_loop_processor.h"
#include "s3g_layout_panner.h"
#include "s3g_mc_to_stereo.h"
#include "s3g_macro_delay.h"
#include "s3g_macro_pitch.h"

#include <array>
#include <cmath>
#include <iostream>

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
                    std::cerr << "3OAFX Point Encoder output is not finite\n";
                    return 1;
                }
                pointEncoderPeak = std::max(pointEncoderPeak, std::abs(value));
            }
        }
    }
    if (pointEncoderPeak <= 0.00001f || pointEncoderPeak > 1.0f) {
        std::cerr << "3OAFX Point Encoder peak outside expected range: " << pointEncoderPeak << "\n";
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
            std::cerr << "3OAFX Speaker Decoder output is not finite\n";
            return 1;
        }
        speakerDecoderPeak = std::max(speakerDecoderPeak, std::abs(decoderOut[ch]));
    }
    if (speakerDecoderPeak <= 0.000001f || speakerDecoderPeak > 1.0f) {
        std::cerr << "3OAFX Speaker Decoder peak outside expected range: " << speakerDecoderPeak << "\n";
        return 1;
    }
    speakerParams.weighting = s3g::AmbiSpeakerDecoderWeighting::InPhase;
    speakerDecoder.setParams(speakerParams);
    speakerDecoder.processFrame(decoderIn, decoderOut);
    for (uint32_t ch = 0; ch < s3g::kAmbiSpeakerDecoderMaxSpeakers; ++ch) {
        if (!std::isfinite(decoderOut[ch])) {
            std::cerr << "3OAFX Speaker Decoder weighted output is not finite\n";
            return 1;
        }
    }
    speakerParams.layout = s3g::AmbiSpeakerLayoutPreset::Quad;
    speakerParams.order = 1;
    speakerDecoder.setParams(speakerParams);
    speakerDecoder.processFrame(decoderIn, decoderOut);
    for (uint32_t ch = 4; ch < s3g::kAmbiSpeakerDecoderMaxSpeakers; ++ch) {
        if (std::abs(decoderOut[ch]) > 0.000001f) {
            std::cerr << "3OAFX Speaker Decoder did not zero inactive speakers\n";
            return 1;
        }
    }
    speakerParams.layout = s3g::AmbiSpeakerLayoutPreset::QuadOverhead6;
    speakerDecoder.setParams(speakerParams);
    if (std::abs(speakerDecoder.speaker(0).azimuthDeg - -45.0f) > 0.001f
        || std::abs(speakerDecoder.speaker(4).azimuthDeg - -90.0f) > 0.001f
        || std::abs(speakerDecoder.speaker(5).azimuthDeg - 90.0f) > 0.001f) {
        std::cerr << "3OAFX Speaker Decoder quad+overhead order changed unexpectedly\n";
        return 1;
    }
    speakerParams.layout = s3g::AmbiSpeakerLayoutPreset::Dome24;
    speakerDecoder.setParams(speakerParams);
    if (std::abs(speakerDecoder.speaker(0).azimuthDeg - -30.0f) > 0.001f
        || std::abs(speakerDecoder.speaker(2).azimuthDeg - -90.0f) > 0.001f
        || std::abs(speakerDecoder.speaker(12).elevationDeg - 32.0f) > 0.001f
        || std::abs(speakerDecoder.speaker(20).elevationDeg - 66.6f) > 0.001f) {
        std::cerr << "3OAFX Speaker Decoder dome spiral order changed unexpectedly\n";
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
        std::cerr << "3OAFX Speaker Decoder CUBE17 tier projection changed unexpectedly\n";
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
        std::cerr << "3OAFX Speaker Decoder preset geometry was changed by editable speaker fields\n";
        return 1;
    }
    speakerParams.layout = s3g::AmbiSpeakerLayoutPreset::Custom;
    speakerParams.activeSpeakers = 13;
    speakerParams.customField = s3g::AmbiSpeakerCustomField::Hemisphere;
    speakerDecoder.setParams(speakerParams);
    if (speakerDecoder.params().activeSpeakers != 13u) {
        std::cerr << "3OAFX Speaker Decoder custom speaker count failed\n";
        return 1;
    }
    for (uint32_t i = 0; i < 13u; ++i) {
        if (speakerDecoder.speaker(i).elevationDeg < -0.001f) {
            std::cerr << "3OAFX Speaker Decoder hemisphere custom layout dipped below horizon\n";
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
        std::cerr << "3OAFX Speaker Decoder full-sphere custom layout did not use lower hemisphere\n";
        return 1;
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

    std::cout << "s3g-dsp smoke test passed\n";
    std::cout << "layout speakers: " << s3g::kVirtualSpeakerCount << "\n";
    std::cout << "gain ch1 sample4: " << samples[0][3] << "\n";
    std::cout << "lane patch row8: " << patch.rowMask(7) << "\n";
    std::cout << "mc stereo L/R: " << stereoOut[0] << " / " << stereoOut[1] << "\n";
    std::cout << "delay processor impulse: " << delayOut[0] << "\n";
    std::cout << "delay processor stress peak: " << delayStressPeak << "\n";
    std::cout << "loop processor peak: " << loopPeak << "\n";
    std::cout << "loop processor XFD stress peak: " << loopXfdStressPeak << "\n";
    std::cout << "loop processor region stress peak/step: " << loopRegionStressPeak << " / " << loopRegionStressMaxStep << "\n";
    std::cout << "macro delay peak: " << macroPeak << "\n";
    std::cout << "macro delay tail peak: " << macroTailPeak << "\n";
    std::cout << "macro pitch peak: " << macroPitchPeak << "\n";
    std::cout << "3OAFX point encoder peak: " << pointEncoderPeak << "\n";
    std::cout << "3OAFX speaker decoder peak: " << speakerDecoderPeak << "\n";
    std::cout << "3OAFX return W: " << hoaOut[0] << "\n";
    return 0;
}
