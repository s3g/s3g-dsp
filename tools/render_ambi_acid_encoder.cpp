#include "s3g_ambi_acid_encoder.h"
#include "s3g_ambisonic_stereo_decoder.h"

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
    const std::array<char, 2u> bytes {{
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu),
    }};
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeU32(std::ofstream& stream, uint32_t value)
{
    const std::array<char, 4u> bytes {{
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu),
        static_cast<char>((value >> 16u) & 0xffu),
        static_cast<char>((value >> 24u) & 0xffu),
    }};
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

bool writeWav(const std::string& path,
    const std::vector<std::vector<float>>& audio, uint32_t sampleRate)
{
    if (audio.empty() || audio.front().empty() || audio.size() > 16u) {
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
    writeU16(stream, 0xfffeu);
    writeU16(stream, channels);
    writeU32(stream, sampleRate);
    writeU32(stream, sampleRate * blockAlign);
    writeU16(stream, blockAlign);
    writeU16(stream, 16u);
    writeU16(stream, 22u);
    writeU16(stream, 16u);
    writeU32(stream, 0u);
    constexpr std::array<char, 16u> pcmSubformat {{
        0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x10, 0x00,
        static_cast<char>(0x80), 0x00, 0x00, static_cast<char>(0xaa),
        0x00, 0x38, static_cast<char>(0x9b), 0x71,
    }};
    stream.write(pcmSubformat.data(),
        static_cast<std::streamsize>(pcmSubformat.size()));
    stream.write("data", 4);
    writeU32(stream, dataBytes);
    for (size_t frame = 0u; frame < frames; ++frame) {
        for (const auto& channel : audio) {
            const float bounded = std::clamp(channel[frame], -1.0f, 1.0f);
            const int16_t sample = static_cast<int16_t>(
                std::lround(bounded * 32767.0f));
            writeU16(stream, static_cast<uint16_t>(sample));
        }
    }
    return static_cast<bool>(stream);
}

bool parseListenMode(const std::string& name,
    s3g::AmbiFieldListenMode& mode)
{
    if (name == "off") mode = s3g::AmbiFieldListenMode::Off;
    else if (name == "follow") mode = s3g::AmbiFieldListenMode::Follow;
    else if (name == "counter") mode = s3g::AmbiFieldListenMode::Counter;
    else if (name == "balance") mode = s3g::AmbiFieldListenMode::Balance;
    else return false;
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cout
            << "Usage: s3g_ambi_acid_encoder_render OUTPUT.wav "
               "[SECONDS] [off|follow|counter|balance] "
               "[stereo|hoa] [ORDER 1-3]\n"
               "Default: 12 seconds, Balance Listener Mode, stereo preview, "
               "third order. HOA output uses ACN/SN3D channel order.\n";
        return 1;
    }

    float seconds = 12.0f;
    s3g::AmbiFieldListenMode listenMode =
        s3g::AmbiFieldListenMode::Balance;
    bool stereo = true;
    uint32_t order = 3u;
    try {
        if (argc >= 3) seconds = std::stof(argv[2]);
        if (argc >= 4 && !parseListenMode(argv[3], listenMode)) {
            std::cerr << "Unknown Listener Mode: " << argv[3] << "\n";
            return 1;
        }
        if (argc >= 5) {
            const std::string format = argv[4];
            if (format != "stereo" && format != "hoa") {
                std::cerr << "Output format must be stereo or hoa.\n";
                return 1;
            }
            stereo = format == "stereo";
        }
        if (argc >= 6) order = static_cast<uint32_t>(std::stoul(argv[5]));
    } catch (...) {
        std::cerr << "Duration and order must be numeric.\n";
        return 1;
    }
    seconds = std::clamp(seconds, 0.25f, 600.0f);
    order = std::clamp<uint32_t>(order, 1u, 3u);

    constexpr uint32_t sampleRate = 48000u;
    const uint32_t frames = static_cast<uint32_t>(seconds * sampleRate);
    const uint32_t hoaChannels = (order + 1u) * (order + 1u);
    const uint32_t outputChannels = stereo ? 2u : hoaChannels;
    std::vector<std::vector<float>> audio(outputChannels,
        std::vector<float>(frames, 0.0f));

    s3g::AmbiAcidEncoder acid;
    acid.prepare(sampleRate);
    auto acidParams = acid.params();
    acidParams.order = order;
    acidParams.fieldListenMode = listenMode;
    acid.setParams(acidParams);

    s3g::AmbiStereoDecoder decoder;
    decoder.prepare(sampleRate);
    auto decoderParams = decoder.params();
    decoderParams.order = order;
    decoderParams.layout = s3g::AmbiStereoVirtualLayout::Sphere32;
    decoderParams.method = s3g::AmbiStereoMethod::MsCardioid;
    decoderParams.weighting = s3g::AmbiStereoWeighting::MaxRe;
    decoderParams.stereoWidthPercent = 132.0f;
    decoderParams.bassMonoHz = 72.0f;
    decoderParams.outputGainDb = 10.0f;
    decoder.setParams(decoderParams);

    std::array<float, s3g::kAmbiAcidChannels> frame {};
    for (uint32_t sample = 0u; sample < frames; ++sample) {
        acid.processFrame(frame.data(), hoaChannels);
        if (stereo) {
            decoder.processFrame(frame.data(), audio[0u][sample],
                audio[1u][sample]);
        } else {
            for (uint32_t channel = 0u; channel < hoaChannels; ++channel) {
                audio[channel][sample] = frame[channel];
            }
        }
    }

    if (!writeWav(argv[1], audio, sampleRate)) {
        std::cerr << "Could not write " << argv[1] << "\n";
        return 1;
    }
    const char* modeNames[] { "Off", "Follow", "Counter", "Balance" };
    std::cout << "Rendered Chrome Burrow to " << argv[1] << " ("
              << outputChannels << " channels, "
              << (stereo ? "stereo preview" : "ACN/SN3D")
              << ", Listener "
              << modeNames[static_cast<uint32_t>(listenMode)] << ")\n";
    return 0;
}
