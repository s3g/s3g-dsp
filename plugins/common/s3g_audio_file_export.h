#pragma once

#if defined(__APPLE__) && defined(__OBJC__)

#import <AVFoundation/AVFoundation.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace s3g::audio_file {

inline bool writePlanarFloatWaveAtomically(const std::string& path,
    double sampleRate, uint32_t channelCount, uint32_t frameCount,
    const float* const* channels, std::string& error)
{
    if (path.empty() || !std::isfinite(sampleRate) || sampleRate <= 0.0
        || channelCount == 0u
        || frameCount == 0u || !channels) {
        error = "EXPORT TARGET OR RENDERED AUDIO IS INVALID";
        return false;
    }
    for (uint32_t channel = 0u; channel < channelCount; ++channel) {
        if (!channels[channel]) {
            error = "EXPORT AUDIO CHANNEL IS MISSING";
            return false;
        }
    }

    std::string temporaryPath;
    bool wrote = false;
    @autoreleasepool {
        NSString* target = [[NSFileManager defaultManager]
            stringWithFileSystemRepresentation:path.c_str()
            length:path.size()];
        if (!target) {
            error = "EXPORT PATH IS INVALID";
        } else {
            NSString* temporary = [target stringByAppendingFormat:
                @".s3g-%@.tmp.wav", [[NSUUID UUID] UUIDString]];
            const char* temporaryBytes = [temporary fileSystemRepresentation];
            if (!temporaryBytes) {
                error = "EXPORT TEMPORARY PATH IS INVALID";
            } else {
                temporaryPath = temporaryBytes;
                NSDictionary<NSString*, id>* settings = @{
                    AVFormatIDKey: @(kAudioFormatLinearPCM),
                    AVSampleRateKey: @(sampleRate),
                    AVNumberOfChannelsKey: @(channelCount),
                    AVLinearPCMBitDepthKey: @32,
                    AVLinearPCMIsFloatKey: @YES,
                    AVLinearPCMIsBigEndianKey: @NO,
                    AVLinearPCMIsNonInterleaved: @NO,
                };
                NSError* fileError = nil;
                AVAudioFile* file = [[AVAudioFile alloc]
                    initForWriting:[NSURL fileURLWithPath:temporary]
                    settings:settings
                    commonFormat:AVAudioPCMFormatFloat32
                    interleaved:NO error:&fileError];
#if !__has_feature(objc_arc)
                [file autorelease];
#endif
                if (!file) {
                    error = "COULD NOT CREATE THE WAV FILE";
                } else {
                    constexpr AVAudioFrameCount kWriteFrames = 65536u;
                    AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc]
                        initWithPCMFormat:[file processingFormat]
                        frameCapacity:kWriteFrames];
#if !__has_feature(objc_arc)
                    [buffer autorelease];
#endif
                    if (!buffer || ![buffer floatChannelData]) {
                        error = "COULD NOT PREPARE THE WAV WRITER";
                    } else {
                        wrote = true;
                        uint32_t offset = 0u;
                        while (offset < frameCount) {
                            const AVAudioFrameCount count
                                = std::min<AVAudioFrameCount>(kWriteFrames,
                                    frameCount - offset);
                            [buffer setFrameLength:count];
                            for (uint32_t channel = 0u;
                                 channel < channelCount; ++channel) {
                                std::copy_n(channels[channel] + offset,
                                    count,
                                    [buffer floatChannelData][channel]);
                            }
                            fileError = nil;
                            if (![file writeFromBuffer:buffer
                                    error:&fileError]) {
                                error = "COULD NOT WRITE THE WAV FILE";
                                wrote = false;
                                break;
                            }
                            offset += count;
                        }
                    }
                }
            }
        }
    }

    if (!wrote) {
        if (!temporaryPath.empty())
            (void)std::remove(temporaryPath.c_str());
        return false;
    }
    if (std::rename(temporaryPath.c_str(), path.c_str()) != 0) {
        (void)std::remove(temporaryPath.c_str());
        error = "COULD NOT MOVE THE COMPLETED WAV INTO PLACE";
        return false;
    }
    error.clear();
    return true;
}

} // namespace s3g::audio_file

#elif defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace s3g::audio_file {
// Preserve channel order (including ACN): a zero channel mask deliberately
// avoids assigning speaker positions. Publish only a completely written WAV.
inline bool writePlanarFloatWaveAtomically(const std::string& path,
    double sampleRate, uint32_t channelCount, uint32_t frameCount,
    const float* const* channels, std::string& error)
{
    const uint64_t bytes = uint64_t(channelCount) * frameCount * sizeof(float);
    if (path.empty() || !std::isfinite(sampleRate) || sampleRate < 1
        || sampleRate > 768000 || channelCount == 0 || channelCount > 16
        || frameCount == 0 || !channels || bytes > UINT32_MAX - 72u) {
        error = "EXPORT TARGET OR RENDERED AUDIO IS INVALID"; return false;
    }
    for (uint32_t ch = 0; ch < channelCount; ++ch)
        if (!channels[ch]) { error = "EXPORT AUDIO CHANNEL IS MISSING"; return false; }
    const auto target = std::filesystem::u8path(path);
    static std::atomic<uint64_t> serial {0};
    std::filesystem::path temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 64 && file == INVALID_HANDLE_VALUE; ++attempt) {
        temporary = target;
        temporary += L".s3g-" + std::to_wstring(GetCurrentProcessId()) + L"-"
            + std::to_wstring(serial.fetch_add(1)) + L".tmp.wav";
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS) break;
    }
    if (file == INVALID_HANDLE_VALUE) { error = "COULD NOT CREATE THE WAV FILE"; return false; }
    const auto write = [file](const void* data, DWORD count) {
        DWORD written = 0; return WriteFile(file, data, count, &written, nullptr) && written == count;
    };
    // RIFF + extensible float fmt + fact + data, all little-endian.
    std::vector<uint8_t> header;
    const auto u16 = [&header](uint16_t v) { header.push_back(uint8_t(v)); header.push_back(uint8_t(v >> 8)); };
    const auto u32 = [&u16](uint32_t v) { u16(uint16_t(v)); u16(uint16_t(v >> 16)); };
    const auto tag = [&header](const char* v) { header.insert(header.end(), v, v + 4); };
    tag("RIFF"); u32(uint32_t(bytes) + 72u); tag("WAVE"); tag("fmt "); u32(40);
    u16(0xfffe); u16(uint16_t(channelCount)); u32(uint32_t(std::lround(sampleRate)));
    u32(uint32_t(std::lround(sampleRate)) * channelCount * 4u);
    u16(uint16_t(channelCount * 4)); u16(32); u16(22); u16(32); u32(0);
    u32(3); u16(0); u16(0x10); // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
    for (auto v : {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}) header.push_back(uint8_t(v));
    tag("fact"); u32(4); u32(frameCount); tag("data"); u32(uint32_t(bytes));
    bool ok = write(header.data(), DWORD(header.size()));
    std::vector<float> interleaved(size_t(4096) * channelCount);
    for (uint32_t offset = 0; ok && offset < frameCount;) {
        const auto count = std::min(4096u, frameCount - offset);
        for (uint32_t i = 0; i < count; ++i) for (uint32_t ch = 0; ch < channelCount; ++ch)
            interleaved[size_t(i) * channelCount + ch] = channels[ch][offset + i];
        ok = write(interleaved.data(), count * channelCount * 4u); offset += count;
    }
    if (ok) ok = FlushFileBuffers(file) != 0;
    if (!CloseHandle(file)) ok = false;
    if (ok) ok = MoveFileExW(temporary.c_str(), target.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) { DeleteFileW(temporary.c_str()); error = "COULD NOT COMPLETE THE WAV EXPORT"; return false; }
    error.clear(); return true;
}
} // namespace s3g::audio_file

#endif
