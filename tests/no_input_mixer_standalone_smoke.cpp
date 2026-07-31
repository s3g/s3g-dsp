#include "s3g_no_input_mixer_standalone_engine.h"

#include <clap/clap.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <vector>

extern "C" const clap_plugin_entry_t s3g_no_input_mixer_embedded_entry;
extern "C" const clap_plugin_entry_t s3g_nim_gesture_embedded_entry;
extern "C" const clap_plugin_entry_t s3g_mc_to_stereo_autogain_embedded_entry;
extern "C" const clap_plugin_entry_t s3g_mc_to_quad_autogain_embedded_entry;

namespace {

constexpr uint32_t kFrames = 256u;
constexpr uint32_t kChannels = 16u;

bool renderMode(s3g::standalone::NoInputMixerStandaloneEngine& engine,
    s3g::standalone::NoInputOutputMode mode, uint32_t outputOffset)
{
    std::array<std::array<float, kFrames>, kChannels> storage {};
    std::array<float*, kChannels> pointers {};
    for (uint32_t channel = 0u; channel < kChannels; ++channel)
        pointers[channel] = storage[channel].data();
    engine.setOutputChannelOffset(mode, outputOffset);
    engine.setOutputMode(mode);
    engine.setAudioEnabled(true);
    const uint32_t active = s3g::standalone::outputChannelsForMode(mode);
    std::array<double, kChannels> energy {};
    for (uint32_t block = 0u; block < 240u; ++block) {
        engine.render(pointers.data(), kChannels, kFrames);
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (const float value : storage[channel]) {
                if (!std::isfinite(value)) {
                    std::cerr << "Standalone output became non-finite\n";
                    return false;
                }
                energy[channel] += static_cast<double>(value) * value;
            }
        }
    }
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        const bool shouldBeActive = channel >= outputOffset
            && channel < outputOffset + active;
        if (shouldBeActive && !(energy[channel] > 1.0e-10)) {
            std::cerr << "Output mode " << static_cast<uint32_t>(mode)
                      << " left required channel " << channel + 1u
                      << " silent\n";
            return false;
        }
        if (!shouldBeActive && energy[channel] != 0.0) {
            std::cerr << "Output mode " << static_cast<uint32_t>(mode)
                      << " leaked into channel " << channel + 1u << "\n";
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    s3g::standalone::NoInputMixerStandaloneEngine engine;
    bool ok = engine.create(&s3g_no_input_mixer_embedded_entry,
        &s3g_nim_gesture_embedded_entry,
        &s3g_mc_to_stereo_autogain_embedded_entry,
        &s3g_mc_to_quad_autogain_embedded_entry);
    ok = ok && engine.prepare(48000.0, kFrames);

    std::array<std::array<float, kFrames>, kChannels> midiStorage {};
    std::array<float*, kChannels> midiPointers {};
    for (uint32_t channel = 0u; channel < kChannels; ++channel)
        midiPointers[channel] = midiStorage[channel].data();
    engine.enqueueMidi(0x9fu, 112u, 127u);
    engine.enqueueMidi(0xbfu, 99u, 0u);
    engine.enqueueMidi(0xbfu, 98u, 5u);
    engine.enqueueMidi(0xbfu, 6u, 64u);
    engine.enqueueMidi(0xbfu, 38u, 0u);
    engine.render(midiPointers.data(), kChannels, kFrames);
    engine.render(midiPointers.data(), kChannels, kFrames);
    engine.enqueueMidi(0x9fu, 112u, 127u);
    engine.render(midiPointers.data(), kChannels, kFrames);
    uint32_t feedbackCount = 0u;
    uint8_t status = 0u;
    uint8_t dataOne = 0u;
    uint8_t dataTwo = 0u;
    while (engine.dequeueMidiOutput(status, dataOne, dataTwo)) {
        ok = ok && status == 0xbfu && dataOne < 128u && dataTwo < 128u;
        ++feedbackCount;
    }
    ok = ok && feedbackCount >= 4u;

    ok = ok && renderMode(engine,
        s3g::standalone::NoInputOutputMode::StereoAutogain, 2u);
    ok = ok && renderMode(engine,
        s3g::standalone::NoInputOutputMode::QuadAutogain, 4u);
    ok = ok && renderMode(engine,
        s3g::standalone::NoInputOutputMode::DirectEight, 8u);

    engine.setAudioEnabled(false);
    std::array<std::array<float, kFrames>, kChannels> mutedStorage {};
    std::array<float*, kChannels> mutedPointers {};
    for (uint32_t channel = 0u; channel < kChannels; ++channel)
        mutedPointers[channel] = mutedStorage[channel].data();
    for (uint32_t block = 0u; block < 8u; ++block)
        engine.render(mutedPointers.data(), kChannels, kFrames);
    for (const auto& channel : mutedStorage) {
        if (std::any_of(channel.begin(), channel.end(), [](float value) {
                return std::abs(value) > 1.0e-6f;
            })) {
            std::cerr << "Standalone safety mute did not close\n";
            ok = false;
            break;
        }
    }

    engine.release();
    std::vector<uint8_t> noInputState;
    std::vector<uint8_t> gestureState;
    std::vector<uint8_t> stereoState;
    std::vector<uint8_t> quadState;
    ok = ok && engine.noInputPlugin().saveState(noInputState)
        && engine.gesturePlugin().saveState(gestureState)
        && engine.stereoPlugin().saveState(stereoState)
        && engine.quadPlugin().saveState(quadState)
        && !noInputState.empty() && !gestureState.empty()
        && !stereoState.empty()
        && !quadState.empty();
    uint32_t gestureLoopCount = 0u;
    if (gestureState.size() >= 21u)
        std::memcpy(&gestureLoopCount, gestureState.data() + 17u,
            sizeof(gestureLoopCount));
    ok = ok && gestureLoopCount == 1u;
    ok = ok && engine.noInputPlugin().loadState(noInputState)
        && engine.gesturePlugin().loadState(gestureState)
        && engine.stereoPlugin().loadState(stereoState)
        && engine.quadPlugin().loadState(quadState);
    engine.destroy();
    if (!ok) return 1;
    std::cout << "No Input Mixer standalone chain passed\n";
    return 0;
}
