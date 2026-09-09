#include "s3g_sample_file_decode.h"

#if defined(_WIN32)
#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>
#endif

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace s3g::sample_file {

bool decodeWaveFile(const std::string& path,
    std::shared_ptr<const sample::SampleAsset>& assetOut,
    std::string& error)
{
    assetOut.reset();
#if defined(_WIN32)
    drwav decoder {};
    const auto nativePath = std::filesystem::u8path(path);
    if (!drwav_init_file_w(&decoder, nativePath.c_str(), nullptr)) {
        error = "COULD NOT OPEN WAV OR AIFF SAMPLE";
        return false;
    }
    struct DecoderGuard {
        drwav* decoder = nullptr;
        ~DecoderGuard()
        {
            if (decoder) drwav_uninit(decoder);
        }
    } guard { &decoder };

    if (decoder.channels < 1u
        || decoder.channels > sample::kMaximumAudioChannels
        || decoder.totalPCMFrameCount < 1u
        || decoder.totalPCMFrameCount
            > std::numeric_limits<uint32_t>::max()
        || decoder.sampleRate == 0u) {
        error = "USE A 1-16 CHANNEL WAV OR AIFF UNDER 2^32 FRAMES";
        return false;
    }

    const auto channels = static_cast<uint32_t>(decoder.channels);
    const auto requestedFrames = static_cast<uint32_t>(
        decoder.totalPCMFrameCount);
    if (static_cast<std::size_t>(requestedFrames)
            > std::numeric_limits<std::size_t>::max() / channels) {
        error = "SAMPLE IS TOO LARGE";
        return false;
    }

    std::vector<float> interleaved;
    try {
        interleaved.resize(static_cast<std::size_t>(requestedFrames)
            * channels);
    } catch (...) {
        error = "SAMPLE DECODE RAN OUT OF MEMORY";
        return false;
    }
    const auto readFrames = drwav_read_pcm_frames_f32(&decoder,
        requestedFrames, interleaved.data());
    if (readFrames == 0u) {
        error = "SAMPLE DECODE FAILED";
        return false;
    }

    const auto decodedFrames = static_cast<uint32_t>(readFrames);
    auto asset = std::make_shared<sample::SampleAsset>();
    asset->sampleRate = static_cast<double>(decoder.sampleRate);
    asset->channelCount = static_cast<uint8_t>(channels);
    try {
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            auto& output = asset->channels[channel];
            output.resize(decodedFrames);
            for (uint32_t frame = 0u; frame < decodedFrames; ++frame) {
                output[frame] = interleaved[
                    static_cast<std::size_t>(frame) * channels + channel];
            }
        }
    } catch (...) {
        error = "SAMPLE DECODE RAN OUT OF MEMORY";
        return false;
    }
    if (!asset->valid()) {
        error = "DECODED SAMPLE IS INVALID";
        return false;
    }

    assetOut = std::move(asset);
    error.clear();
    return true;
#else
    (void)path;
    error = "PORTABLE SAMPLE DECODER IS ONLY USED ON WINDOWS";
    return false;
#endif
}

} // namespace s3g::sample_file
