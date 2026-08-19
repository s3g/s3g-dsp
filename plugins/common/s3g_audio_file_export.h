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
                if (!file) {
                    error = "COULD NOT CREATE THE WAV FILE";
                } else {
                    constexpr AVAudioFrameCount kWriteFrames = 65536u;
                    AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc]
                        initWithPCMFormat:[file processingFormat]
                        frameCapacity:kWriteFrames];
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

#endif
