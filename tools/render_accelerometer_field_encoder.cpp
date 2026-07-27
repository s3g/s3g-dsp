#include "s3g_accelerometer_field_encoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void writeU16(std::ofstream& stream, uint16_t value)
{
    const std::array<char, 2> bytes {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu),
    };
    stream.write(bytes.data(), bytes.size());
}

void writeU32(std::ofstream& stream, uint32_t value)
{
    const std::array<char, 4> bytes {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu),
        static_cast<char>((value >> 16u) & 0xffu),
        static_cast<char>((value >> 24u) & 0xffu),
    };
    stream.write(bytes.data(), bytes.size());
}

bool writeMultichannelWav(const std::string& path,
    const std::vector<std::vector<float>>& audio, uint32_t sampleRate)
{
    if (audio.empty() || audio.front().empty()
        || audio.size() > s3g::kAccelerometerFieldMaxChannels) {
        return false;
    }
    const size_t frames = audio.front().size();
    for (const auto& channel : audio) {
        if (channel.size() != frames) return false;
    }
    const uint16_t channels = static_cast<uint16_t>(audio.size());
    const uint16_t blockAlign = channels * 2u;
    const uint32_t dataBytes = static_cast<uint32_t>(
        frames * static_cast<size_t>(blockAlign));
    std::ofstream stream(path, std::ios::binary);
    if (!stream) return false;
    stream.write("RIFF", 4);
    writeU32(stream, 60u + dataBytes);
    stream.write("WAVEfmt ", 8);
    writeU32(stream, 40u);
    writeU16(stream, 0xfffeu); // WAVE_FORMAT_EXTENSIBLE
    writeU16(stream, channels);
    writeU32(stream, sampleRate);
    writeU32(stream, sampleRate * blockAlign);
    writeU16(stream, blockAlign);
    writeU16(stream, 16u);
    writeU16(stream, 22u);
    writeU16(stream, 16u);
    writeU32(stream, 0u); // ACN or body lanes, not a speaker-bed mask.
    constexpr std::array<char, 16> pcmSubformat {{
        0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x10, 0x00,
        static_cast<char>(0x80), 0x00, 0x00, static_cast<char>(0xaa),
        0x00, 0x38, static_cast<char>(0x9b), 0x71,
    }};
    stream.write(pcmSubformat.data(), pcmSubformat.size());
    stream.write("data", 4);
    writeU32(stream, dataBytes);
    for (size_t frame = 0u; frame < frames; ++frame) {
        const auto sample16 = [](float sample) {
            const float bounded = std::max(-1.0f, std::min(1.0f, sample));
            return static_cast<int16_t>(std::lround(bounded * 32767.0f));
        };
        for (const auto& channel : audio) {
            writeU16(stream, static_cast<uint16_t>(sample16(channel[frame])));
        }
    }
    return static_cast<bool>(stream);
}

void printPresets()
{
    for (uint32_t index = 0u;
        index < s3g::kAccelerometerFieldPresetCount; ++index) {
        const auto& info = s3g::accelerometerFieldFactoryPresetInfo(index);
        std::cout << index << ": " << info.name << " — "
                  << info.description << "\n";
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2 || std::string(argv[1]) == "--list") {
        std::cout << "Usage: s3g_accelerometer_field_encoder_render "
                     "OUTPUT.wav [PRESET 0-12] [SECONDS] [hoa|raw] [ORDER 1-3]\n"
                     "Default: third-order ACN/SN3D. Raw writes eight body stems.\n\n";
        printPresets();
        return argc < 2 ? 1 : 0;
    }

    uint32_t preset = 0u;
    float seconds = 12.0f;
    bool raw = false;
    uint32_t order = 3u;
    try {
        if (argc >= 3) preset = static_cast<uint32_t>(std::stoul(argv[2]));
        if (argc >= 4) seconds = std::stof(argv[3]);
        if (argc >= 5) raw = std::string(argv[4]) == "raw";
        if (argc >= 6) order = static_cast<uint32_t>(std::stoul(argv[5]));
    } catch (...) {
        std::cerr << "Preset and duration must be numeric.\n";
        return 1;
    }
    preset = std::min<uint32_t>(
        preset, s3g::kAccelerometerFieldPresetCount - 1u);
    seconds = std::max(0.25f, std::min(600.0f, seconds));
    order = std::clamp<uint32_t>(order, 1u, s3g::kAccelerometerFieldMaxOrder);

    constexpr uint32_t sampleRate = 48000u;
    const uint32_t frames = static_cast<uint32_t>(seconds * sampleRate);
    auto params = s3g::accelerometerFieldFactoryPreset(preset);
    params.ambisonicOrder = order;
    params.outputMode = raw
        ? s3g::AccelerometerFieldOutputMode::BodyStems
        : s3g::AccelerometerFieldOutputMode::Ambisonic;
    const uint32_t channels = raw
        ? s3g::kAccelerometerFieldSensorCount
        : (order + 1u) * (order + 1u);
    std::vector<std::vector<float>> audio(
        channels, std::vector<float>(frames, 0.0f));
    std::vector<float*> outputs(channels, nullptr);
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        outputs[channel] = audio[channel].data();
    }
    s3g::AccelerometerFieldEncoder engine;
    engine.prepare(sampleRate);
    engine.setParams(params);
    engine.reset();
    engine.process(nullptr, outputs.data(), channels, frames);

    if (!writeMultichannelWav(argv[1], audio, sampleRate)) {
        std::cerr << "Could not write " << argv[1] << "\n";
        return 1;
    }
    std::cout << "Rendered "
              << s3g::accelerometerFieldFactoryPresetInfo(preset).name
              << " to " << argv[1] << " (" << channels << " channels, "
              << (raw ? "body stems" : "ACN/SN3D") << ")\n";
    return 0;
}
