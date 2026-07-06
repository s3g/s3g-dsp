#include "s3g_24ch_layout.h"
#include "s3g_3oafx.h"
#include "s3g_delay_processor.h"
#include "s3g_gain.h"
#include "s3g_lane_patch.h"
#include "s3g_mc_to_stereo.h"
#include "s3g_resonant_terrain.h"

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


    s3g::ResonantTerrain modal;
    modal.prepare(48000.0);
    s3g::ResonantTerrainParams modalParams;
    modalParams.density = 0.45f;
    modalParams.outputGainDb = -18.0f;
    modal.setParams(modalParams);
    modal.noteOn(60, 0.8f);
    float modalOut[s3g::kResonantTerrainChannels] {};
    float modalPeak = 0.0f;
    for (int i = 0; i < 48000; ++i) {
        modal.processFrame(modalOut);
        for (float value : modalOut) {
            if (!std::isfinite(value)) {
                std::cerr << "Resonant Terrain output is not finite\n";
                return 1;
            }
            modalPeak = std::max(modalPeak, std::abs(value));
        }
    }
    if (modalPeak <= 0.00001f || modalPeak > 1.1f) {
        std::cerr << "Resonant Terrain peak outside expected range: " << modalPeak << "\n";
        return 1;
    }

    s3g::ResonantTerrain modalStress;
    modalStress.prepare(48000.0);
    s3g::ResonantTerrainParams modalStressParams;
    modalStressParams.density = 1.0f;
    modalStressParams.decay = 1.0f;
    modalStressParams.brightness = 1.0f;
    modalStressParams.harmonicity = 1.0f;
    modalStressParams.exciterTone = 1.0f;
    modalStressParams.midiInfluence = 1.0f;
    modalStressParams.outputGainDb = -6.0f;
    modalStress.setParams(modalStressParams);
    float modalStressPeak = 0.0f;
    for (int i = 0; i < 192000; ++i) {
        if ((i % 12000) == 0) {
            modalStress.noteOn(48 + ((i / 12000) % 36), 1.0f);
        }
        if ((i % 12000) == 6000) {
            modalStress.noteOff(48 + ((i / 12000) % 36));
        }
        modalStress.processFrame(modalOut);
        for (float value : modalOut) {
            if (!std::isfinite(value)) {
                std::cerr << "Resonant Terrain stress output is not finite\n";
                return 1;
            }
            modalStressPeak = std::max(modalStressPeak, std::abs(value));
        }
    }
    if (modalStressPeak > 1.01f) {
        std::cerr << "Resonant Terrain stress peak exceeded limiter: " << modalStressPeak << "\n";
        return 1;
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
    std::cout << "resonant terrain peak: " << modalPeak << "\n";
    std::cout << "resonant terrain stress peak: " << modalStressPeak << "\n";
    std::cout << "3OAFX return W: " << hoaOut[0] << "\n";
    return 0;
}
