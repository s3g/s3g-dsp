#include "s3g_24ch_layout.h"
#include "s3g_3oafx.h"
#include "s3g_diffusion_mesh.h"
#include "s3g_gain.h"
#include "s3g_lane_patch.h"
#include "s3g_tape_delay.h"

#include <array>
#include <cmath>
#include <iostream>

int main()
{
    if (s3g::kVirtualDome24.size() != s3g::kVirtualSpeakerCount) {
        std::cerr << "Unexpected 24ch layout size\n";
        return 1;
    }

    std::array<float, s3g::kVirtualSpeakerCount> in {};
    std::array<float, s3g::kVirtualSpeakerCount> out {};
    in[0] = 1.0f;

    s3g::DiffusionMesh24 mesh;
    mesh.setAmount(0.5f);
    mesh.setFeedback(0.1f);
    mesh.processFrame(in.data(), out.data());

    if (!std::isfinite(out[0])) {
        std::cerr << "Diffusion output is not finite\n";
        return 1;
    }

    std::array<float, s3g::kMaxRealtimeChannels> scalableIn {};
    std::array<float, s3g::kMaxRealtimeChannels> scalableOut {};
    scalableIn[0] = 1.0f;

    s3g::DiffusionMesh scalableMesh;
    scalableMesh.prepare(128);
    scalableMesh.setAmount(0.25f);
    scalableMesh.setFeedback(0.05f);
    scalableMesh.processFrame(scalableIn.data(), scalableOut.data());

    if (scalableMesh.channels() != 128 || !std::isfinite(scalableOut[0])) {
        std::cerr << "Scalable diffusion mesh failed\n";
        return 1;
    }

    std::array<float, s3g::kMaxRealtimeChannels> laneIn {};
    std::array<float, s3g::kMaxRealtimeChannels> laneOut {};
    laneIn[0] = 1.0f;
    laneIn[1] = 1.0f;
    s3g::DiffusionMesh laneMesh;
    laneMesh.prepare(2);
    laneMesh.setAmount(0.0f);
    laneMesh.setFeedback(0.0f);
    laneMesh.setChannelAmount(1, 1.0f);
    laneMesh.processFrame(laneIn.data(), laneOut.data());

    if (std::abs(laneOut[0] - 1.0f) > 0.0001f || std::abs(laneOut[1]) > 0.0001f) {
        std::cerr << "Per-lane diffusion amount failed\n";
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

    s3g::TapeDelay tape;
    tape.prepare(1000.0, 2, 1.0);
    tape.setChannelDelayMs(0, 10.0f);
    tape.setChannelFeedback(0, 0.0f);
    tape.setChannelTone(0, 1.0f);
    float tapeIn[2] {};
    float tapeOut[2] {};
    tapeIn[0] = 1.0f;
    tape.processFrame(tapeIn, tapeOut);
    tapeIn[0] = 0.0f;
    for (int i = 0; i < 10; ++i) {
        tape.processFrame(tapeIn, tapeOut);
    }
    if (std::abs(tapeOut[0] - 1.0f) > 0.0001f) {
        std::cerr << "Tape delay impulse timing failed\n";
        return 1;
    }

    s3g::TapeDelay tapeStress;
    tapeStress.prepare(48000.0, 8, 2.25);
    for (int ch = 0; ch < 8; ++ch) {
        tapeStress.setChannelDelayMs(ch, 80.0f + static_cast<float>(ch) * 41.0f);
        tapeStress.setChannelFeedback(ch, 0.78f);
        tapeStress.setChannelTone(ch, 0.45f);
        tapeStress.setChannelNetwork(ch, 0.55f);
        tapeStress.setChannelCharacter(ch, 0.85f);
        tapeStress.setChannelSmearAmount(ch, 0.90f);
        tapeStress.setChannelPitchSemitones(ch, ch % 2 == 0 ? 24.0f : -24.0f);
    }
    float tapeStressIn[8] {};
    float tapeStressOut[8] {};
    float tapeStressPeak = 0.0f;
    for (int i = 0; i < 96000; ++i) {
        if (i == 24000 || i == 48000 || i == 72000) {
            for (int ch = 0; ch < 8; ++ch) {
                tapeStress.setChannelDelayMs(ch, 140.0f + static_cast<float>((ch * 137 + i / 1000) % 1700));
            }
        }
        for (int ch = 0; ch < 8; ++ch) {
            tapeStressIn[ch] = std::sin(static_cast<float>(i) * (0.011f + static_cast<float>(ch) * 0.0027f)) * 0.12f;
        }
        tapeStress.processFrame(tapeStressIn, tapeStressOut);
        for (float value : tapeStressOut) {
            if (!std::isfinite(value)) {
                std::cerr << "Tape delay stress output is not finite\n";
                return 1;
            }
            tapeStressPeak = std::max(tapeStressPeak, std::abs(value));
        }
    }
    if (tapeStressPeak > 6.0f) {
        std::cerr << "Tape delay stress output exceeded safety expectation\n";
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
    std::cout << "diffusion ch1: " << out[0] << "\n";
    std::cout << "scalable diffusion channels: " << scalableMesh.channels() << "\n";
    std::cout << "lane diffusion ch2: " << laneOut[1] << "\n";
    std::cout << "gain ch1 sample4: " << samples[0][3] << "\n";
    std::cout << "lane patch row8: " << patch.rowMask(7) << "\n";
    std::cout << "tape delay impulse: " << tapeOut[0] << "\n";
    std::cout << "tape delay stress peak: " << tapeStressPeak << "\n";
    std::cout << "3OAFX return W: " << hoaOut[0] << "\n";
    return 0;
}
