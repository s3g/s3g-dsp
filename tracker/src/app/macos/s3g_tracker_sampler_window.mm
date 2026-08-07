#import "s3g_tracker_sampler_window.h"

#import "s3g_tracker_controls.h"
#import "s3g_tracker_workspace.h"

#import <AudioToolbox/AudioToolbox.h>
#import <dispatch/dispatch.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

using s3g::tracker::InstrumentKind;
using s3g::tracker::audio::SampleSlice;
using s3g::tracker::audio::StereoSampleAnalysis;
using s3g::tracker::audio::StereoSampleAsset;
using s3g::tracker::app::TrackerViewState;
using s3g::tracker::app::WorkspaceCallbacks;

NSTextField* label(NSString* value, CGFloat size = 9.0)
{
    NSTextField* field = [NSTextField labelWithString:value];
    field.font = S3GTrackerFont(size, NSFontWeightMedium);
    field.textColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::TextSecondary);
    field.lineBreakMode = NSLineBreakByTruncatingMiddle;
    return field;
}

double samplerEnvelopeTimeFromSlider(double normalized) noexcept
{
    const double value = std::clamp(normalized, 0.0, 1.0);
    return s3g::tracker::audio::kMaximumSamplerEnvelopeMilliseconds
        * value * value * value;
}

double samplerEnvelopeSliderFromTime(double milliseconds) noexcept
{
    const double normalized = std::clamp(milliseconds
            / s3g::tracker::audio::kMaximumSamplerEnvelopeMilliseconds,
        0.0, 1.0);
    return std::cbrt(normalized);
}

NSString* samplerEnvelopeTimeLabel(double milliseconds)
{
    if (milliseconds < 10.0)
        return [NSString stringWithFormat:@"%.1f MS", milliseconds];
    if (milliseconds < 1000.0)
        return [NSString stringWithFormat:@"%.0f MS", milliseconds];
    return [NSString stringWithFormat:@"%.2f S", milliseconds / 1000.0];
}

class ScopedExtAudioFile {
public:
    explicit ScopedExtAudioFile(ExtAudioFileRef value) noexcept
        : value_(value) {}
    ~ScopedExtAudioFile()
    {
        if (value_) ExtAudioFileDispose(value_);
    }
    void close() noexcept
    {
        if (value_) ExtAudioFileDispose(value_);
        value_ = nullptr;
    }
    ScopedExtAudioFile(const ScopedExtAudioFile&) = delete;
    ScopedExtAudioFile& operator=(const ScopedExtAudioFile&) = delete;

private:
    ExtAudioFileRef value_ = nullptr;
};

bool decodeAudioFile(NSURL* url, std::shared_ptr<const StereoSampleAsset>& out,
    std::shared_ptr<const StereoSampleAnalysis>& analysisOut,
    std::string& error)
{
    ExtAudioFileRef file = nullptr;
    OSStatus status = ExtAudioFileOpenURL((__bridge CFURLRef)url, &file);
    if (status != noErr || !file) {
        error = "Could not open the selected audio file";
        return false;
    }
    ScopedExtAudioFile scopedFile(file);

    AudioStreamBasicDescription source {};
    UInt32 propertySize = sizeof(source);
    status = ExtAudioFileGetProperty(file,
        kExtAudioFileProperty_FileDataFormat, &propertySize, &source);
    if (status != noErr || source.mSampleRate <= 0.0
        || source.mChannelsPerFrame < 1u || source.mChannelsPerFrame > 2u) {
        error = "Sampler currently accepts mono or stereo audio files";
        return false;
    }
    SInt64 length = 0;
    propertySize = sizeof(length);
    status = ExtAudioFileGetProperty(file,
        kExtAudioFileProperty_FileLengthFrames, &propertySize, &length);
    if (status != noErr || length <= 0
        || static_cast<uint64_t>(length) > 0xffffffffu) {
        error = "Audio file has an unsupported or empty frame length";
        return false;
    }

    AudioStreamBasicDescription client {};
    client.mSampleRate = source.mSampleRate;
    client.mFormatID = kAudioFormatLinearPCM;
    client.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
    client.mBytesPerPacket = 4u * source.mChannelsPerFrame;
    client.mFramesPerPacket = 1u;
    client.mBytesPerFrame = client.mBytesPerPacket;
    client.mChannelsPerFrame = source.mChannelsPerFrame;
    client.mBitsPerChannel = 32u;
    status = ExtAudioFileSetProperty(file,
        kExtAudioFileProperty_ClientDataFormat, sizeof(client), &client);
    if (status != noErr) {
        error = "Could not configure the audio decoder";
        return false;
    }

    const auto channels = static_cast<std::size_t>(source.mChannelsPerFrame);
    const auto maximumSamples = static_cast<std::size_t>(
        std::numeric_limits<UInt32>::max() / sizeof(float));
    if (static_cast<uint64_t>(length)
        > static_cast<uint64_t>(maximumSamples / channels)) {
        error = "Audio file is too large for the current sampler loader";
        return false;
    }
    std::vector<float> interleaved(static_cast<std::size_t>(length)
        * channels, 0.0f);
    AudioBufferList buffers {};
    buffers.mNumberBuffers = 1u;
    buffers.mBuffers[0].mNumberChannels = source.mChannelsPerFrame;
    buffers.mBuffers[0].mDataByteSize = static_cast<UInt32>(
        interleaved.size() * sizeof(float));
    buffers.mBuffers[0].mData = interleaved.data();
    UInt32 frames = static_cast<UInt32>(length);
    status = ExtAudioFileRead(file, &frames, &buffers);
    scopedFile.close();
    if (status != noErr || frames == 0u) {
        error = "Audio decoding failed";
        return false;
    }

    auto asset = std::make_shared<StereoSampleAsset>();
    asset->sampleRate = source.mSampleRate;
    asset->left.resize(frames);
    if (channels == 2u) asset->right.resize(frames);
    for (std::size_t frame = 0u; frame < frames; ++frame) {
        asset->left[frame] = interleaved[frame * channels];
        if (channels == 2u)
            asset->right[frame] = interleaved[frame * channels + 1u];
    }
    if (!asset->valid()) {
        error = "Decoded audio contains unsupported sample data";
        return false;
    }
    auto analysis = std::make_shared<StereoSampleAnalysis>(
        s3g::tracker::audio::analyzeStereoSample(*asset));
    if (!analysis->validFor(*asset)) {
        error = "Waveform and transient analysis failed";
        return false;
    }
    out = std::move(asset);
    analysisOut = std::move(analysis);
    error.clear();
    return true;
}

void makeEqualSlices(s3g::tracker::StereoSamplerInstrumentSlot& slot,
    std::size_t count)
{
    if (!slot.asset) return;
    count = std::clamp<std::size_t>(count, 1u,
        s3g::tracker::audio::kMaximumSamplerSlices);
    const uint32_t frames = slot.asset->frameCount();
    count = std::min<std::size_t>(count, frames);
    slot.slices = {};
    slot.sliceCount = count;
    for (std::size_t index = 0u; index < count; ++index) {
        const auto start = static_cast<uint32_t>(
            static_cast<uint64_t>(frames) * index / count);
        const auto end = static_cast<uint32_t>(
            static_cast<uint64_t>(frames) * (index + 1u) / count);
        slot.slices[index] = { start, std::max(start + 1u, end),
            1.0f, false };
    }
}

s3g::tracker::StereoSamplerInstrumentSlot* samplerSlot(
    TrackerViewState* state, uint32_t nodeId)
{
    if (!state) return nullptr;
    const auto index = s3g::tracker::stereoSamplerRackSlotIndex(nodeId);
    return index < state->instrumentRack.samplerSlots.size()
        ? &state->instrumentRack.samplerSlots[index] : nullptr;
}

} // namespace

@protocol S3GTrackerSamplerWaveformDelegate <NSObject>
- (void)samplerWaveformSelectionChanged;
- (void)samplerWaveformSlicesChanged;
@end

@interface S3GTrackerSamplerWaveformView : NSView
@property(nonatomic, assign) TrackerViewState* trackerState;
@property(nonatomic) uint32_t nodeId;
@property(nonatomic) NSInteger selectedSlice;
@property(nonatomic, weak) id<S3GTrackerSamplerWaveformDelegate> delegate;
@property(nonatomic) BOOL snapToZero;
@property(nonatomic) CGFloat zoomFactor;
@property(nonatomic) uint32_t visibleStartFrame;
@property(nonatomic) uint32_t cursorFrame;
@property(nonatomic) uint32_t auditionFrame;
@property(nonatomic) NSInteger draggedMarker;
@property(nonatomic) BOOL sliceTableDirty;
- (void)addMarkerAtCursor;
- (void)deleteSelectedMarker;
- (void)zoomBy:(CGFloat)factor;
- (void)resetZoom;
@end

@implementation S3GTrackerSamplerWaveformView

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self) {
        _selectedSlice = 0;
        _zoomFactor = 1.0;
        _auditionFrame = std::numeric_limits<uint32_t>::max();
        _draggedMarker = -1;
    }
    return self;
}

- (s3g::tracker::StereoSamplerInstrumentSlot*)slot
{
    return samplerSlot(self.trackerState, self.nodeId);
}

- (uint32_t)visibleFrameCount:(uint32_t)totalFrames
{
    if (totalFrames == 0u) return 0u;
    self.zoomFactor = std::clamp<CGFloat>(self.zoomFactor, 1.0,
        [self maximumZoomFactorForTotalFrames:totalFrames]);
    return std::clamp<uint32_t>(static_cast<uint32_t>(std::ceil(
        static_cast<double>(totalFrames) / std::max<CGFloat>(1.0,
            self.zoomFactor))), 1u, totalFrames);
}

- (CGFloat)backingScale
{
    const CGFloat width = NSWidth(self.bounds);
    if (width > 0.0) {
        const NSSize backing = [self convertSizeToBacking:self.bounds.size];
        if (backing.width > 0.0)
            return std::max<CGFloat>(1.0, backing.width / width);
    }
    return std::max<CGFloat>(1.0,
        self.window ? self.window.backingScaleFactor : 1.0);
}

- (NSInteger)waveformPixelColumns
{
    const CGFloat scale = [self backingScale];
    return std::max<NSInteger>(1, static_cast<NSInteger>(std::floor(
        NSWidth(self.bounds) * scale)) - 2);
}

- (CGFloat)maximumZoomFactorForTotalFrames:(uint32_t)totalFrames
{
    if (totalFrames <= 1u) return 1.0;
    constexpr CGFloat kTargetPixelsPerFrame = 8.0;
    const CGFloat pixelColumns = static_cast<CGFloat>(
        [self waveformPixelColumns]);
    const CGFloat targetVisibleFrames = std::max<CGFloat>(2.0,
        std::floor(pixelColumns / kTargetPixelsPerFrame));
    return std::max<CGFloat>(1.0, std::min<CGFloat>(
        static_cast<CGFloat>(totalFrames),
        static_cast<CGFloat>(totalFrames) / targetVisibleFrames));
}

- (uint32_t)clampedVisibleStart:(uint32_t)totalFrames
{
    const uint32_t visible = [self visibleFrameCount:totalFrames];
    const uint32_t maximum = totalFrames > visible
        ? totalFrames - visible : 0u;
    self.visibleStartFrame = std::min(self.visibleStartFrame, maximum);
    return self.visibleStartFrame;
}

- (CGFloat)xForFrame:(uint32_t)frame totalFrames:(uint32_t)totalFrames
{
    const uint32_t visible = [self visibleFrameCount:totalFrames];
    const uint32_t start = [self clampedVisibleStart:totalFrames];
    return NSWidth(self.bounds) * (static_cast<CGFloat>(frame)
        - static_cast<CGFloat>(start)) / static_cast<CGFloat>(visible);
}

- (uint32_t)frameForX:(CGFloat)x totalFrames:(uint32_t)totalFrames
{
    const uint32_t visible = [self visibleFrameCount:totalFrames];
    const uint32_t start = [self clampedVisibleStart:totalFrames];
    const CGFloat fraction = std::clamp<CGFloat>(
        x / std::max<CGFloat>(1.0, NSWidth(self.bounds)), 0.0, 1.0);
    return std::min<uint32_t>(totalFrames - 1u,
        start + static_cast<uint32_t>(std::llround(fraction
            * static_cast<CGFloat>(visible - 1u))));
}

- (uint32_t)snappedFrame:(uint32_t)frame
{
    auto* slot = [self slot];
    if (!slot || !slot->asset || !self.snapToZero) return frame;
    const auto radius = static_cast<uint32_t>(std::max<double>(1.0,
        std::round(slot->asset->sampleRate * 0.004)));
    return s3g::tracker::audio::nearestStereoZeroFrame(*slot->asset,
        frame, radius);
}

- (NSInteger)markerNearX:(CGFloat)x
{
    auto* slot = [self slot];
    if (!slot || !slot->asset) return -1;
    const uint32_t frames = slot->asset->frameCount();
    NSInteger nearest = -1;
    CGFloat nearestDistance = 7.0;
    for (std::size_t index = 1u; index < slot->sliceCount; ++index) {
        const CGFloat markerX = [self xForFrame:slot->slices[index].startFrame
            totalFrames:frames];
        const CGFloat distance = std::abs(markerX - x);
        if (distance < nearestDistance) {
            nearest = static_cast<NSInteger>(index);
            nearestDistance = distance;
        }
    }
    return nearest;
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Workspace) setFill];
    NSRectFill(self.bounds);
    [S3GTrackerThemeColor(S3GTrackerThemeRole::BorderStrong) setStroke];
    NSFrameRect(NSInsetRect(self.bounds, 0.5, 0.5));
    auto* slot = [self slot];
    if (!slot || !slot->asset || slot->asset->left.empty()) {
        NSDictionary* attributes = @{
            NSForegroundColorAttributeName: S3GTrackerThemeColor(
                S3GTrackerThemeRole::TextFaint),
            NSFontAttributeName: S3GTrackerFont(10.0, NSFontWeightMedium),
        };
        [@"LOAD A MONO OR STEREO SAMPLE" drawAtPoint:NSMakePoint(18.0, 18.0)
            withAttributes:attributes];
        return;
    }
    const uint32_t frames = slot->asset->frameCount();
    const uint32_t visible = [self visibleFrameCount:frames];
    const uint32_t visibleStart = [self clampedVisibleStart:frames];
    const uint32_t visibleEnd = visibleStart + visible;
    const StereoSampleAnalysis* waveformAnalysis = slot->analysis
            && slot->analysis->validFor(*slot->asset)
        ? slot->analysis.get() : nullptr;
    for (std::size_t index = 0u; index < slot->sliceCount; ++index) {
        const CGFloat x = [self xForFrame:slot->slices[index].startFrame
            totalFrames:frames];
        if (static_cast<NSInteger>(index) == self.selectedSlice) {
            const CGFloat end = [self xForFrame:slot->slices[index].endFrame
                totalFrames:frames];
            [S3GTrackerThemeColor(S3GTrackerThemeRole::Selection) setFill];
            const CGFloat clippedStart = std::clamp<CGFloat>(x, 1.0,
                NSWidth(self.bounds) - 1.0);
            const CGFloat clippedEnd = std::clamp<CGFloat>(end, 1.0,
                NSWidth(self.bounds) - 1.0);
            if (clippedEnd > clippedStart)
                NSRectFill(NSMakeRect(clippedStart, 1.0,
                    clippedEnd - clippedStart, NSHeight(self.bounds) - 2.0));
        }
    }
    const CGFloat mid = NSMidY(self.bounds);
    const CGFloat backingScale = [self backingScale];
    const CGFloat pixelSize = 1.0 / backingScale;
    const NSInteger columns = [self waveformPixelColumns];
    const double framesPerPixel = static_cast<double>(visible)
        / static_cast<double>(columns);
    const CGFloat amplitudeScale = std::max<CGFloat>(1.0, mid - 5.0);
    if (framesPerPixel <= 1.0) {
        // At sample-level zoom an envelope becomes a row of disconnected
        // sticks. Follow the immutable PCM directly so adjacent sample values
        // remain legible on both standard- and high-density displays.
        const CGFloat plotStart = pixelSize;
        const CGFloat plotWidth = std::max<CGFloat>(pixelSize,
            NSWidth(self.bounds) - 2.0 * pixelSize);
        const auto sampleY = [&](float sample) {
            return mid - std::clamp<CGFloat>(static_cast<CGFloat>(sample),
                -1.0, 1.0) * amplitudeScale;
        };
        const auto sampleX = [&](uint32_t offset) {
            if (visible <= 1u) return plotStart + plotWidth * 0.5;
            return plotStart + static_cast<CGFloat>(offset) * plotWidth
                / static_cast<CGFloat>(visible - 1u);
        };
        NSBezierPath* leftPath = [NSBezierPath bezierPath];
        NSBezierPath* rightPath = slot->asset->right.empty()
            ? nil : [NSBezierPath bezierPath];
        leftPath.lineWidth = pixelSize;
        leftPath.lineJoinStyle = NSLineJoinStyleRound;
        leftPath.lineCapStyle = NSLineCapStyleRound;
        rightPath.lineWidth = pixelSize;
        rightPath.lineJoinStyle = NSLineJoinStyleRound;
        rightPath.lineCapStyle = NSLineCapStyleRound;
        for (uint32_t offset = 0u; offset < visible; ++offset) {
            const uint32_t frame = visibleStart + offset;
            const NSPoint leftPoint = NSMakePoint(sampleX(offset),
                sampleY(slot->asset->left[frame]));
            if (offset == 0u) [leftPath moveToPoint:leftPoint];
            else [leftPath lineToPoint:leftPoint];
            if (rightPath) {
                const NSPoint rightPoint = NSMakePoint(sampleX(offset),
                    sampleY(slot->asset->right[frame]));
                if (offset == 0u) [rightPath moveToPoint:rightPoint];
                else [rightPath lineToPoint:rightPoint];
            }
        }
        if (visible == 1u) {
            const CGFloat x = sampleX(0u);
            [leftPath lineToPoint:NSMakePoint(x + pixelSize,
                sampleY(slot->asset->left[visibleStart]))];
            if (rightPath)
                [rightPath lineToPoint:NSMakePoint(x + pixelSize,
                    sampleY(slot->asset->right[visibleStart]))];
        }
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Note, 0.55) setStroke];
        [rightPath stroke];
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Note) setStroke];
        [leftPath stroke];
    } else {
        NSBezierPath* path = [NSBezierPath bezierPath];
        constexpr double kRawPeakFramesPerPixel = 64.0;
        const bool useRawSamples = framesPerPixel
            <= kRawPeakFramesPerPixel;
        for (NSInteger x = 0; x < columns; ++x) {
            const uint32_t start = visibleStart + static_cast<uint32_t>(
                static_cast<uint64_t>(visible) * static_cast<uint64_t>(x)
                    / static_cast<uint64_t>(columns));
            const uint32_t end = std::min<uint32_t>(visibleEnd, std::max(
                start + 1u, visibleStart + static_cast<uint32_t>(
                    static_cast<uint64_t>(visible)
                        * static_cast<uint64_t>(x + 1)
                        / static_cast<uint64_t>(columns))));
            float minimum = std::numeric_limits<float>::max();
            float maximum = std::numeric_limits<float>::lowest();
            if (useRawSamples) {
                for (uint32_t frame = start; frame < end; ++frame) {
                    minimum = std::min(minimum, slot->asset->left[frame]);
                    maximum = std::max(maximum, slot->asset->left[frame]);
                    if (!slot->asset->right.empty()) {
                        minimum = std::min(minimum,
                            slot->asset->right[frame]);
                        maximum = std::max(maximum,
                            slot->asset->right[frame]);
                    }
                }
            } else if (waveformAnalysis) {
                const uint32_t stride = waveformAnalysis->peakStrideFrames;
                const std::size_t firstPeak = start / stride;
                const std::size_t lastPeak = std::min<std::size_t>(
                    waveformAnalysis->peaks.size() - 1u,
                    (end - 1u) / stride);
                for (std::size_t peak = firstPeak; peak <= lastPeak; ++peak) {
                    minimum = std::min(minimum,
                        waveformAnalysis->peaks[peak].minimum);
                    maximum = std::max(maximum,
                        waveformAnalysis->peaks[peak].maximum);
                }
            } else {
                minimum = maximum = slot->asset->left[start];
            }
            const CGFloat pointX = (static_cast<CGFloat>(x) + 1.5)
                * pixelSize;
            [path moveToPoint:NSMakePoint(pointX,
                mid - std::clamp<CGFloat>(static_cast<CGFloat>(maximum),
                    -1.0, 1.0) * amplitudeScale)];
            [path lineToPoint:NSMakePoint(pointX,
                mid - std::clamp<CGFloat>(static_cast<CGFloat>(minimum),
                    -1.0, 1.0) * amplitudeScale)];
        }
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Note) setStroke];
        path.lineWidth = pixelSize;
        CGContextRef context = NSGraphicsContext.currentContext.CGContext;
        CGContextSaveGState(context);
        CGContextSetShouldAntialias(context, false);
        [path stroke];
        CGContextRestoreGState(context);
    }
    if (waveformAnalysis) {
        auto transient = std::lower_bound(
            waveformAnalysis->transients.begin(),
            waveformAnalysis->transients.end(), visibleStart,
            [](const auto& candidate, uint32_t frame) {
                return candidate.frame < frame;
            });
        NSInteger lastColumn = -2;
        [S3GTrackerThemeColor(S3GTrackerThemeRole::TextFaint, 0.65) setFill];
        for (; transient != waveformAnalysis->transients.end()
             && transient->frame <= visibleEnd; ++transient) {
            const CGFloat x = [self xForFrame:transient->frame
                totalFrames:frames];
            const NSInteger column = static_cast<NSInteger>(std::floor(
                x * backingScale));
            if (column == lastColumn) continue;
            NSRectFill(NSMakeRect(static_cast<CGFloat>(column) * pixelSize,
                0.0, pixelSize, 6.0));
            lastColumn = column;
        }
    }
    for (std::size_t index = 0u; index < slot->sliceCount; ++index) {
        const uint32_t markerFrame = slot->slices[index].startFrame;
        if (markerFrame < visibleStart || markerFrame > visibleEnd) continue;
        const CGFloat x = [self xForFrame:markerFrame totalFrames:frames];
        [S3GTrackerThemeColor(index == static_cast<std::size_t>(
            std::max<NSInteger>(0, self.selectedSlice))
                ? S3GTrackerThemeRole::Live : S3GTrackerThemeRole::Note,
            0.9) setFill];
        const CGFloat alignedX = std::floor(x * backingScale) * pixelSize;
        NSRectFill(NSMakeRect(alignedX, 0.0,
            index == static_cast<std::size_t>(
                std::max<NSInteger>(0, self.selectedSlice))
                ? 2.0 * pixelSize : pixelSize,
            NSHeight(self.bounds)));
    }
    if (self.auditionFrame != std::numeric_limits<uint32_t>::max()
        && self.auditionFrame >= visibleStart
        && self.auditionFrame <= visibleEnd) {
        const CGFloat x = [self xForFrame:self.auditionFrame
            totalFrames:frames];
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Live) setFill];
        NSRectFill(NSMakeRect(std::floor(x * backingScale) * pixelSize,
            0.0, 2.0 * pixelSize, NSHeight(self.bounds)));
    }
}

- (void)mouseDown:(NSEvent*)event
{
    auto* slot = [self slot];
    if (!slot || !slot->asset) return;
    [self.window makeFirstResponder:self];
    const NSPoint point = [self convertPoint:event.locationInWindow
        fromView:nil];
    self.cursorFrame = [self frameForX:point.x
        totalFrames:slot->asset->frameCount()];
    NSInteger marker = [self markerNearX:point.x];
    self.draggedMarker = -1;
    self.sliceTableDirty = NO;
    if (event.clickCount >= 2 && marker < 0) {
        const uint32_t frame = [self snappedFrame:self.cursorFrame];
        const std::size_t oldCount = slot->sliceCount;
        if (s3g::tracker::audio::addSampleSliceMarker(slot->slices.data(),
                slot->sliceCount, slot->slices.size(), frame)) {
            for (std::size_t index = 1u; index < slot->sliceCount; ++index) {
                if (slot->slices[index].startFrame == frame) {
                    self.selectedSlice = static_cast<NSInteger>(index);
                    break;
                }
            }
            if (slot->sliceCount != oldCount)
                [self.delegate samplerWaveformSlicesChanged];
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (marker >= 0) {
        self.selectedSlice = marker;
        self.draggedMarker = marker;
    } else {
        for (std::size_t index = 0u; index < slot->sliceCount; ++index) {
            if (self.cursorFrame >= slot->slices[index].startFrame
                && self.cursorFrame < slot->slices[index].endFrame) {
                self.selectedSlice = static_cast<NSInteger>(index);
                break;
            }
        }
    }
    [self.delegate samplerWaveformSelectionChanged];
    [self setNeedsDisplay:YES];
}

- (void)mouseDragged:(NSEvent*)event
{
    auto* slot = [self slot];
    if (!slot || !slot->asset) return;
    const NSPoint point = [self convertPoint:event.locationInWindow
        fromView:nil];
    const NSInteger targetMarker = self.draggedMarker;
    if (targetMarker <= 0
        || static_cast<std::size_t>(targetMarker) >= slot->sliceCount) return;
    uint32_t frame = [self frameForX:point.x
        totalFrames:slot->asset->frameCount()];
    frame = [self snappedFrame:frame];
    if (s3g::tracker::audio::moveSampleSliceMarker(slot->slices.data(),
            slot->sliceCount, static_cast<std::size_t>(targetMarker), frame)) {
        self.cursorFrame = frame;
        self.sliceTableDirty = YES;
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (self.sliceTableDirty)
        [self.delegate samplerWaveformSlicesChanged];
    self.draggedMarker = -1;
    self.sliceTableDirty = NO;
}

- (void)rightMouseDown:(NSEvent*)event
{
    auto* slot = [self slot];
    if (!slot) return;
    const NSPoint point = [self convertPoint:event.locationInWindow
        fromView:nil];
    const NSInteger marker = [self markerNearX:point.x];
    if (marker > 0 && s3g::tracker::audio::deleteSampleSliceMarker(
            slot->slices.data(), slot->sliceCount,
            static_cast<std::size_t>(marker))) {
        self.selectedSlice = std::max<NSInteger>(0, marker - 1);
        [self.delegate samplerWaveformSlicesChanged];
    }
}

- (void)scrollWheel:(NSEvent*)event
{
    auto* slot = [self slot];
    if (!slot || !slot->asset) return;
    const uint32_t total = slot->asset->frameCount();
    if ((event.modifierFlags & NSEventModifierFlagShift) != 0
        || std::abs(event.scrollingDeltaX) > std::abs(event.scrollingDeltaY)) {
        const uint32_t visible = [self visibleFrameCount:total];
        const double delta = event.scrollingDeltaX != 0.0
            ? event.scrollingDeltaX : event.scrollingDeltaY;
        const int64_t moved = static_cast<int64_t>(self.visibleStartFrame)
            + static_cast<int64_t>(std::llround(delta * visible / 24.0));
        const uint32_t maximum = total > visible ? total - visible : 0u;
        self.visibleStartFrame = static_cast<uint32_t>(std::clamp<int64_t>(
            moved, 0, maximum));
    } else {
        const NSPoint point = [self convertPoint:event.locationInWindow
            fromView:nil];
        const uint32_t anchor = [self frameForX:point.x totalFrames:total];
        const CGFloat fraction = std::clamp<CGFloat>(point.x
            / std::max<CGFloat>(1.0, NSWidth(self.bounds)), 0.0, 1.0);
        self.zoomFactor = std::clamp<CGFloat>(self.zoomFactor
            * std::pow(1.12, -event.scrollingDeltaY), 1.0,
            [self maximumZoomFactorForTotalFrames:total]);
        const uint32_t visible = [self visibleFrameCount:total];
        const int64_t start = static_cast<int64_t>(anchor)
            - static_cast<int64_t>(std::llround(fraction * visible));
        const uint32_t maximum = total > visible ? total - visible : 0u;
        self.visibleStartFrame = static_cast<uint32_t>(std::clamp<int64_t>(
            start, 0, maximum));
    }
    [self setNeedsDisplay:YES];
}

- (void)addMarkerAtCursor
{
    auto* slot = [self slot];
    if (!slot || !slot->asset) return;
    uint32_t frame = self.cursorFrame;
    if (frame == 0u) {
        frame = [self clampedVisibleStart:slot->asset->frameCount()]
            + [self visibleFrameCount:slot->asset->frameCount()] / 2u;
    }
    frame = [self snappedFrame:frame];
    if (s3g::tracker::audio::addSampleSliceMarker(slot->slices.data(),
            slot->sliceCount, slot->slices.size(), frame)) {
        for (std::size_t index = 1u; index < slot->sliceCount; ++index)
            if (slot->slices[index].startFrame == frame)
                self.selectedSlice = static_cast<NSInteger>(index);
        [self.delegate samplerWaveformSlicesChanged];
    }
}

- (void)deleteSelectedMarker
{
    auto* slot = [self slot];
    if (!slot || self.selectedSlice <= 0) return;
    if (s3g::tracker::audio::deleteSampleSliceMarker(slot->slices.data(),
            slot->sliceCount,
            static_cast<std::size_t>(self.selectedSlice))) {
        --self.selectedSlice;
        [self.delegate samplerWaveformSlicesChanged];
    }
}

- (void)zoomBy:(CGFloat)factor
{
    auto* slot = [self slot];
    const uint32_t totalFrames = slot && slot->asset
        ? slot->asset->frameCount() : 0u;
    self.zoomFactor = std::clamp<CGFloat>(self.zoomFactor * factor,
        1.0, totalFrames > 0u
            ? [self maximumZoomFactorForTotalFrames:totalFrames] : 1.0);
    [self setNeedsDisplay:YES];
}

- (void)resetZoom
{
    self.zoomFactor = 1.0;
    self.visibleStartFrame = 0u;
    [self setNeedsDisplay:YES];
}

@end

@interface S3GTrackerSamplerWindowController () <NSWindowDelegate,
    S3GTrackerSamplerWaveformDelegate>
@property(nonatomic, assign) TrackerViewState* trackerState;
@property(nonatomic, assign) WorkspaceCallbacks* trackerCallbacks;
@property(nonatomic, strong) NSPopUpButton* instancePopup;
@property(nonatomic, strong) NSPopUpButton* sliceCountPopup;
@property(nonatomic, strong) NSPopUpButton* slicePopup;
@property(nonatomic, strong) NSTextField* baseNoteField;
@property(nonatomic, strong) NSTextField* fileLabel;
@property(nonatomic, strong) NSTextField* detailLabel;
@property(nonatomic, strong) NSButton* reverseButton;
@property(nonatomic, strong) NSSlider* gainSlider;
@property(nonatomic, strong) NSTextField* gainValueLabel;
@property(nonatomic, strong) NSSlider* attackSlider;
@property(nonatomic, strong) NSSlider* decaySlider;
@property(nonatomic, strong) NSSlider* sustainSlider;
@property(nonatomic, strong) NSSlider* releaseSlider;
@property(nonatomic, strong) NSTextField* attackValueLabel;
@property(nonatomic, strong) NSTextField* decayValueLabel;
@property(nonatomic, strong) NSTextField* sustainValueLabel;
@property(nonatomic, strong) NSTextField* releaseValueLabel;
@property(nonatomic, strong) NSButton* zeroSnapButton;
@property(nonatomic, strong) S3GTrackerActionButton* loadButton;
@property(nonatomic, strong) NSProgressIndicator* loadIndicator;
@property(nonatomic, strong) S3GTrackerSamplerWaveformView* waveform;
@property(nonatomic, strong) NSTimer* auditionTimer;
@property(nonatomic) NSTimeInterval auditionStartTime;
@property(nonatomic) uint32_t auditionStartFrame;
@property(nonatomic) uint32_t auditionEndFrame;
@property(nonatomic) BOOL auditionReverse;
@property(nonatomic) NSUInteger projectLoadGeneration;
@property(nonatomic) NSUInteger rackLoadGeneration;
@property(nonatomic) NSUInteger activeDecodeCount;
@property(nonatomic, strong) NSMutableDictionary<NSNumber*, NSNumber*>*
    manualLoadGenerations;
@property(nonatomic, strong) NSMutableSet<NSNumber*>* analysisPending;
@end

@implementation S3GTrackerSamplerWindowController

- (instancetype)initWithState:(TrackerViewState*)state
    callbacks:(WorkspaceCallbacks*)callbacks
{
    NSWindow* window = [[NSWindow alloc] initWithContentRect:
        NSMakeRect(0.0, 0.0, 880.0, 550.0)
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
            | NSWindowStyleMaskResizable)
        backing:NSBackingStoreBuffered defer:NO];
    self = [super initWithWindow:window];
    if (!self) return nil;
    self.trackerState = state;
    self.trackerCallbacks = callbacks;
    window.title = @"s3g Tracker — Stereo Slice Sampler";
    window.delegate = self;
    window.releasedWhenClosed = NO;
    window.minSize = NSMakeSize(720.0, 480.0);
    self.analysisPending = [NSMutableSet set];
    self.manualLoadGenerations = [NSMutableDictionary dictionary];

    S3GTrackerPanelView* root = [[S3GTrackerPanelView alloc]
        initWithFrame:window.contentView.bounds];
    root.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    window.contentView = root;

    NSStackView* controls = [[NSStackView alloc] initWithFrame:NSZeroRect];
    controls.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    controls.alignment = NSLayoutAttributeCenterY;
    controls.spacing = 8.0;
    controls.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:controls];

    self.instancePopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    self.instancePopup.target = self;
    self.instancePopup.action = @selector(instanceChanged:);
    [self.instancePopup.widthAnchor constraintEqualToConstant:170.0].active = YES;
    [controls addArrangedSubview:self.instancePopup];
    self.loadButton = [[S3GTrackerActionButton alloc]
        initWithFrame:NSZeroRect];
    self.loadButton.title = @"LOAD AUDIO";
    self.loadButton.target = self;
    self.loadButton.action = @selector(loadAudio:);
    [controls addArrangedSubview:self.loadButton];
    self.loadIndicator = [[NSProgressIndicator alloc]
        initWithFrame:NSZeroRect];
    self.loadIndicator.style = NSProgressIndicatorStyleSpinning;
    self.loadIndicator.displayedWhenStopped = NO;
    self.loadIndicator.controlSize = NSControlSizeSmall;
    [self.loadIndicator.widthAnchor constraintEqualToConstant:16.0].active = YES;
    [controls addArrangedSubview:self.loadIndicator];
    S3GTrackerActionButton* autoSlice = [[S3GTrackerActionButton alloc]
        initWithFrame:NSZeroRect];
    autoSlice.title = @"AUTO SLICE";
    autoSlice.target = self;
    autoSlice.action = @selector(autoSlice:);
    [controls addArrangedSubview:autoSlice];
    [controls addArrangedSubview:label(@"SLICES")];
    self.sliceCountPopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    for (NSNumber* count in @[ @1, @4, @8, @16, @32, @64 ]) {
        [self.sliceCountPopup addItemWithTitle:count.stringValue];
        self.sliceCountPopup.lastItem.representedObject = count;
    }
    self.sliceCountPopup.target = self;
    self.sliceCountPopup.action = @selector(sliceCountChanged:);
    [controls addArrangedSubview:self.sliceCountPopup];
    [controls addArrangedSubview:label(@"BASE NOTE")];
    self.baseNoteField = [[NSTextField alloc] initWithFrame:NSZeroRect];
    S3GTrackerStyleTextEditor(self.baseNoteField);
    self.baseNoteField.target = self;
    self.baseNoteField.action = @selector(baseNoteChanged:);
    [self.baseNoteField.widthAnchor constraintEqualToConstant:46.0].active = YES;
    [controls addArrangedSubview:self.baseNoteField];

    self.fileLabel = label(@"NO SAMPLE LOADED", 11.0);
    self.fileLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:self.fileLabel];
    self.detailLabel = label(@"NOTES MAP CONSECUTIVELY TO SLICES", 8.5);
    self.detailLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:self.detailLabel];
    self.waveform = [[S3GTrackerSamplerWaveformView alloc]
        initWithFrame:NSZeroRect];
    self.waveform.trackerState = state;
    self.waveform.delegate = self;
    self.waveform.snapToZero = YES;
    self.waveform.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:self.waveform];

    NSStackView* sliceControls = [[NSStackView alloc]
        initWithFrame:NSZeroRect];
    sliceControls.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    sliceControls.alignment = NSLayoutAttributeCenterY;
    sliceControls.spacing = 8.0;
    sliceControls.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:sliceControls];
    [sliceControls addArrangedSubview:label(@"EDIT SLICE")];
    self.slicePopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    self.slicePopup.target = self;
    self.slicePopup.action = @selector(sliceChanged:);
    [sliceControls addArrangedSubview:self.slicePopup];
    self.reverseButton = [NSButton checkboxWithTitle:@"REVERSE"
        target:self action:@selector(reverseChanged:)];
    self.reverseButton.font = S3GTrackerFont(9.0, NSFontWeightMedium);
    [sliceControls addArrangedSubview:self.reverseButton];
    [sliceControls addArrangedSubview:label(@"GAIN")];
    self.gainSlider = [NSSlider sliderWithValue:1.0 minValue:0.0
        maxValue:2.0 target:self action:@selector(gainChanged:)];
    self.gainSlider.continuous = NO;
    [self.gainSlider.widthAnchor constraintEqualToConstant:130.0].active = YES;
    [sliceControls addArrangedSubview:self.gainSlider];
    self.gainValueLabel = label(@"1.00", 9.0);
    [self.gainValueLabel.widthAnchor constraintEqualToConstant:38.0].active = YES;
    [sliceControls addArrangedSubview:self.gainValueLabel];
    S3GTrackerActionButton* audition = [[S3GTrackerActionButton alloc]
        initWithFrame:NSZeroRect];
    audition.title = @"▶ AUDITION";
    audition.target = self;
    audition.action = @selector(audition:);
    [sliceControls addArrangedSubview:audition];

    NSStackView* envelopeControls = [[NSStackView alloc]
        initWithFrame:NSZeroRect];
    envelopeControls.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    envelopeControls.alignment = NSLayoutAttributeCenterY;
    envelopeControls.spacing = 6.0;
    envelopeControls.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:envelopeControls];
    [envelopeControls addArrangedSubview:label(@"AMP ENVELOPE")];
    NSArray<NSString*>* envelopeNames = @[ @"A", @"D", @"S", @"R" ];
    NSArray<NSString*>* envelopeHelp = @[
        @"Attack time", @"Decay time", @"Sustain level", @"Release time"
    ];
    NSMutableArray<NSSlider*>* envelopeSliders = [NSMutableArray array];
    NSMutableArray<NSTextField*>* envelopeValues = [NSMutableArray array];
    for (NSUInteger index = 0u; index < envelopeNames.count; ++index) {
        [envelopeControls addArrangedSubview:label(envelopeNames[index])];
        NSSlider* slider = [NSSlider sliderWithValue:0.0 minValue:0.0
            maxValue:1.0 target:self action:@selector(envelopeChanged:)];
        slider.continuous = NO;
        slider.toolTip = envelopeHelp[index];
        [slider.widthAnchor constraintEqualToConstant:72.0].active = YES;
        [envelopeControls addArrangedSubview:slider];
        NSTextField* value = label(@"—", 8.5);
        value.toolTip = envelopeHelp[index];
        [value.widthAnchor constraintEqualToConstant:54.0].active = YES;
        [envelopeControls addArrangedSubview:value];
        [envelopeSliders addObject:slider];
        [envelopeValues addObject:value];
    }
    self.attackSlider = envelopeSliders[0u];
    self.decaySlider = envelopeSliders[1u];
    self.sustainSlider = envelopeSliders[2u];
    self.releaseSlider = envelopeSliders[3u];
    self.attackValueLabel = envelopeValues[0u];
    self.decayValueLabel = envelopeValues[1u];
    self.sustainValueLabel = envelopeValues[2u];
    self.releaseValueLabel = envelopeValues[3u];

    NSStackView* editControls = [[NSStackView alloc]
        initWithFrame:NSZeroRect];
    editControls.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    editControls.alignment = NSLayoutAttributeCenterY;
    editControls.spacing = 7.0;
    editControls.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:editControls];
    S3GTrackerActionButton* addMarker = [[S3GTrackerActionButton alloc]
        initWithFrame:NSZeroRect];
    addMarker.title = @"+ MARKER";
    addMarker.target = self;
    addMarker.action = @selector(addMarker:);
    [editControls addArrangedSubview:addMarker];
    S3GTrackerActionButton* deleteMarker = [[S3GTrackerActionButton alloc]
        initWithFrame:NSZeroRect];
    deleteMarker.title = @"− MARKER";
    deleteMarker.target = self;
    deleteMarker.action = @selector(deleteMarker:);
    [editControls addArrangedSubview:deleteMarker];
    self.zeroSnapButton = [NSButton checkboxWithTitle:@"ZERO SNAP"
        target:self action:@selector(zeroSnapChanged:)];
    self.zeroSnapButton.state = NSControlStateValueOn;
    self.zeroSnapButton.font = S3GTrackerFont(9.0, NSFontWeightMedium);
    [editControls addArrangedSubview:self.zeroSnapButton];
    [editControls addArrangedSubview:label(@"ZOOM")];
    for (NSArray* specification in @[
            @[ @"−", NSStringFromSelector(@selector(zoomOut:)) ],
            @[ @"+", NSStringFromSelector(@selector(zoomIn:)) ],
            @[ @"FIT", NSStringFromSelector(@selector(zoomReset:)) ] ]) {
        S3GTrackerActionButton* button = [[S3GTrackerActionButton alloc]
            initWithFrame:NSZeroRect];
        button.title = specification[0];
        button.target = self;
        button.action = NSSelectorFromString(specification[1]);
        [editControls addArrangedSubview:button];
    }
    NSTextField* markerHint = label(
        @"DOUBLE-CLICK TO ADD  •  DRAG TO MOVE  •  RIGHT-CLICK TO DELETE",
        8.0);
    [editControls addArrangedSubview:markerHint];

    [NSLayoutConstraint activateConstraints:@[
        [controls.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:18.0],
        [controls.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-18.0],
        [controls.topAnchor constraintEqualToAnchor:root.topAnchor constant:18.0],
        [self.fileLabel.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:18.0],
        [self.fileLabel.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-18.0],
        [self.fileLabel.topAnchor constraintEqualToAnchor:controls.bottomAnchor constant:18.0],
        [self.detailLabel.leadingAnchor constraintEqualToAnchor:self.fileLabel.leadingAnchor],
        [self.detailLabel.topAnchor constraintEqualToAnchor:self.fileLabel.bottomAnchor constant:4.0],
        [self.waveform.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:18.0],
        [self.waveform.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-18.0],
        [self.waveform.topAnchor constraintEqualToAnchor:self.detailLabel.bottomAnchor constant:14.0],
        [self.waveform.bottomAnchor constraintEqualToAnchor:editControls.topAnchor constant:-12.0],
        [editControls.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:18.0],
        [editControls.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-18.0],
        [editControls.bottomAnchor constraintEqualToAnchor:envelopeControls.topAnchor constant:-10.0],
        [envelopeControls.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:18.0],
        [envelopeControls.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-18.0],
        [envelopeControls.bottomAnchor constraintEqualToAnchor:sliceControls.topAnchor constant:-10.0],
        [sliceControls.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:18.0],
        [sliceControls.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-18.0],
        [self.waveform.heightAnchor constraintGreaterThanOrEqualToConstant:180.0],
    ]];
    S3GTrackerRestoreWindowFrame(window, @"S3GTrackerSamplerWindow");
    [self reloadModel];
    return self;
}

- (uint32_t)selectedNode
{
    NSNumber* represented = self.instancePopup.selectedItem.representedObject;
    if (represented) return represented.unsignedIntValue;
    return self.trackerState ? self.trackerState->selectedRackInstrument
                             : s3g::tracker::kInvalidInstrumentNode;
}

- (s3g::tracker::StereoSamplerInstrumentSlot*)selectedSlot
{
    return samplerSlot(self.trackerState, [self selectedNode]);
}

- (void)ensureAnalysisForNode:(uint32_t)node
{
    auto* slot = samplerSlot(self.trackerState, node);
    if (!slot || !slot->asset) return;
    if (slot->analysis && slot->analysis->validFor(*slot->asset)) return;
    slot->analysis.reset();
    NSNumber* key = @(node);
    if ([self.analysisPending containsObject:key]) return;
    [self.analysisPending addObject:key];
    const std::shared_ptr<const StereoSampleAsset> asset = slot->asset;
    __weak S3GTrackerSamplerWindowController* weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        std::shared_ptr<const StereoSampleAnalysis> analysis;
        try {
            auto derived = std::make_shared<StereoSampleAnalysis>(
                s3g::tracker::audio::analyzeStereoSample(*asset));
            if (derived->validFor(*asset)) analysis = std::move(derived);
        } catch (...) {
            analysis.reset();
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            S3GTrackerSamplerWindowController* strongSelf = weakSelf;
            if (!strongSelf) return;
            [strongSelf.analysisPending removeObject:key];
            auto* current = samplerSlot(strongSelf.trackerState, node);
            if (analysis && current && current->asset == asset) {
                current->analysis = analysis;
                if (strongSelf.waveform.nodeId == node)
                    [strongSelf reloadModel];
            }
        });
    });
}

- (void)publish
{
    [self.auditionTimer invalidate];
    self.auditionTimer = nil;
    self.waveform.auditionFrame = std::numeric_limits<uint32_t>::max();
    if (self.trackerCallbacks && self.trackerCallbacks->instrumentRackChanged)
        self.trackerCallbacks->instrumentRackChanged();
    [self reloadModel];
}

- (void)publishHydratedAssets
{
    if (self.trackerCallbacks
        && self.trackerCallbacks->instrumentRackReloaded) {
        self.trackerCallbacks->instrumentRackReloaded();
    } else if (self.trackerCallbacks
        && self.trackerCallbacks->instrumentRackChanged) {
        self.trackerCallbacks->instrumentRackChanged();
    }
    [self reloadModel];
}

- (void)beginDecodeActivity
{
    ++self.activeDecodeCount;
    self.loadButton.enabled = NO;
    [self.loadIndicator startAnimation:nil];
}

- (void)endDecodeActivity
{
    if (self.activeDecodeCount > 0u) --self.activeDecodeCount;
    if (self.activeDecodeCount != 0u) return;
    self.loadButton.enabled = YES;
    [self.loadIndicator stopAnimation:nil];
}

- (void)reloadModel
{
    if (!self.window || !self.trackerState) return;
    const uint32_t preferred = s3g::tracker::isStereoSamplerInstrumentNode(
        self.trackerState->selectedRackInstrument)
        ? self.trackerState->selectedRackInstrument : [self selectedNode];
    [self.instancePopup removeAllItems];
    NSInteger selection = 0;
    NSInteger index = 0;
    for (std::size_t rackIndex = 0u;
         rackIndex < self.trackerState->instrumentRack.instruments.size();
         ++rackIndex) {
        const auto& instrument = self.trackerState->instrumentRack.instruments[
            rackIndex];
        if (!instrument.active
            || instrument.kind != InstrumentKind::StereoSliceSampler) continue;
        [self.instancePopup addItemWithTitle:[NSString stringWithFormat:
            @"%02lu  STEREO SAMPLER",
            static_cast<unsigned long>(rackIndex)]];
        self.instancePopup.lastItem.representedObject = @(instrument.nodeId);
        if (instrument.nodeId == preferred) selection = index;
        ++index;
    }
    if (self.instancePopup.numberOfItems > 0)
        [self.instancePopup selectItemAtIndex:selection];
    const uint32_t node = [self selectedNode];
    self.trackerState->selectedRackInstrument = node;
    self.waveform.nodeId = node;
    auto* slot = [self selectedSlot];
    if (!slot) return;
    [self ensureAnalysisForNode:node];
    self.baseNoteField.integerValue = slot->baseNote;
    self.attackSlider.doubleValue = samplerEnvelopeSliderFromTime(
        slot->envelope.attackMilliseconds);
    self.decaySlider.doubleValue = samplerEnvelopeSliderFromTime(
        slot->envelope.decayMilliseconds);
    self.sustainSlider.doubleValue = slot->envelope.sustain;
    self.releaseSlider.doubleValue = samplerEnvelopeSliderFromTime(
        slot->envelope.releaseMilliseconds);
    self.attackValueLabel.stringValue = samplerEnvelopeTimeLabel(
        slot->envelope.attackMilliseconds);
    self.decayValueLabel.stringValue = samplerEnvelopeTimeLabel(
        slot->envelope.decayMilliseconds);
    self.sustainValueLabel.stringValue = [NSString stringWithFormat:@"%.2f",
        slot->envelope.sustain];
    self.releaseValueLabel.stringValue = samplerEnvelopeTimeLabel(
        slot->envelope.releaseMilliseconds);
    self.fileLabel.stringValue = slot->filePath.empty()
        ? @"NO SAMPLE LOADED"
        : [NSString stringWithUTF8String:slot->filePath.c_str()];
    self.detailLabel.stringValue = slot->asset
        ? [NSString stringWithFormat:@"%u FRAMES  •  %.1f KHZ  •  %lu SLICES  •  %lu TRANSIENTS  •  NOTE %u–%u",
            slot->asset->frameCount(), slot->asset->sampleRate / 1000.0,
            static_cast<unsigned long>(slot->sliceCount),
            static_cast<unsigned long>(slot->analysis
                ? slot->analysis->transients.size() : 0u), slot->baseNote,
            static_cast<unsigned>(std::min<std::size_t>(127u,
                slot->baseNote + std::max<std::size_t>(1u,
                    slot->sliceCount) - 1u))]
        : @"NOTES MAP CONSECUTIVELY TO SLICES";
    [self.slicePopup removeAllItems];
    for (std::size_t slice = 0u; slice < slot->sliceCount; ++slice) {
        const auto token = s3g::tracker::audio::formatSamplerSliceToken(
            static_cast<uint8_t>(slice));
        uint8_t note = 0u;
        const bool mapped = s3g::tracker::audio::samplerNoteForSlice(
            slot->baseNote, static_cast<uint8_t>(slice), note);
        [self.slicePopup addItemWithTitle:mapped
            ? [NSString stringWithFormat:@"%s  NOTE %u", token.data(), note]
            : [NSString stringWithFormat:@"%s  NOTE —", token.data()]];
    }
    if (self.slicePopup.numberOfItems > 0) {
        const NSInteger selected = std::clamp<NSInteger>(
            self.waveform.selectedSlice, 0,
            self.slicePopup.numberOfItems - 1);
        [self.slicePopup selectItemAtIndex:selected];
        self.waveform.selectedSlice = selected;
        self.reverseButton.state = slot->slices[
            static_cast<std::size_t>(selected)].reverse
            ? NSControlStateValueOn : NSControlStateValueOff;
        self.gainSlider.doubleValue = slot->slices[
            static_cast<std::size_t>(selected)].gain;
        self.gainValueLabel.stringValue = [NSString stringWithFormat:@"%.2f",
            self.gainSlider.doubleValue];
    }
    constexpr NSInteger kCustomSliceCountTag = 0x5333;
    for (NSMenuItem* item in self.sliceCountPopup.itemArray.copy) {
        if (item.tag == kCustomSliceCountTag)
            [self.sliceCountPopup.menu removeItem:item];
    }
    bool matchedSliceCount = false;
    for (NSMenuItem* item in self.sliceCountPopup.itemArray) {
        NSNumber* represented = item.representedObject;
        if ([represented isKindOfClass:NSNumber.class]
            && represented.unsignedIntegerValue == slot->sliceCount) {
            [self.sliceCountPopup selectItem:item];
            matchedSliceCount = true;
            break;
        }
    }
    if (!matchedSliceCount && slot->sliceCount != 0u) {
        [self.sliceCountPopup addItemWithTitle:[NSString stringWithFormat:
            @"CUSTOM %lu", static_cast<unsigned long>(slot->sliceCount)]];
        self.sliceCountPopup.lastItem.representedObject = @(slot->sliceCount);
        self.sliceCountPopup.lastItem.tag = kCustomSliceCountTag;
        [self.sliceCountPopup selectItem:self.sliceCountPopup.lastItem];
    }
    [self.waveform setNeedsDisplay:YES];
}

- (void)hydratePersistedAssets
{
    if (!self.trackerState) return;
    const NSUInteger generation = ++self.projectLoadGeneration;
    struct ReloadRequest {
        uint32_t node = s3g::tracker::kInvalidInstrumentNode;
        std::string path;
    };
    struct ReloadResult {
        uint32_t node = s3g::tracker::kInvalidInstrumentNode;
        std::string path;
        std::shared_ptr<const StereoSampleAsset> asset;
        std::shared_ptr<const StereoSampleAnalysis> analysis;
        std::string error;
    };
    std::vector<ReloadRequest> requests;
    for (std::size_t slotIndex = 0u;
         slotIndex < self.trackerState->instrumentRack.samplerSlots.size();
         ++slotIndex) {
        const auto& slot
            = self.trackerState->instrumentRack.samplerSlots[slotIndex];
        if (!slot.asset && !slot.filePath.empty()) {
            requests.push_back({
                s3g::tracker::stereoSamplerNodeForRackSlot(slotIndex),
                slot.filePath,
            });
        }
    }
    if (requests.empty()) return;
    [self beginDecodeActivity];
    __weak S3GTrackerSamplerWindowController* weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        std::vector<ReloadResult> decoded;
        decoded.reserve(requests.size());
        for (const auto& request : requests) {
            @autoreleasepool {
                ReloadResult result;
                result.node = request.node;
                result.path = request.path;
                NSString* path = [NSString stringWithUTF8String:
                    request.path.c_str()];
                NSURL* url = path ? [NSURL fileURLWithPath:path] : nil;
                if (url) {
                    try {
                        (void)decodeAudioFile(url, result.asset,
                            result.analysis, result.error);
                    } catch (...) {
                        result.asset.reset();
                        result.analysis.reset();
                        result.error =
                            "audio decoding or analysis ran out of resources";
                    }
                } else {
                    result.error = "saved sample path is not valid UTF-8";
                }
                decoded.push_back(std::move(result));
            }
        }
        auto decodedShared
            = std::make_shared<std::vector<ReloadResult>>(std::move(decoded));
        dispatch_async(dispatch_get_main_queue(), ^{
            S3GTrackerSamplerWindowController* owner = weakSelf;
            if (!owner) return;
            [owner endDecodeActivity];
            if (generation != owner.projectLoadGeneration) return;
            bool changed = false;
            for (auto& result : *decodedShared) {
                auto* slot = samplerSlot(owner.trackerState, result.node);
                if (!slot || slot->filePath != result.path)
                    continue;
                if (!result.asset) {
                    if (owner.trackerCallbacks
                        && owner.trackerCallbacks->reportError) {
                        owner.trackerCallbacks->reportError(
                            "Could not restore sampler file " + result.path
                            + (result.error.empty() ? ""
                                : ": " + result.error));
                    }
                    continue;
                }
                slot->asset = std::move(result.asset);
                slot->analysis = std::move(result.analysis);
                bool slicesValid = slot->sliceCount > 0u;
                for (std::size_t index = 0u;
                     slicesValid && index < slot->sliceCount; ++index) {
                    const auto& slice = slot->slices[index];
                    slicesValid = slice.startFrame < slice.endFrame
                        && slice.endFrame <= slot->asset->frameCount();
                }
                if (!slicesValid) makeEqualSlices(*slot, 16u);
                changed = true;
            }
            if (changed) [owner publishHydratedAssets];
            else [owner reloadModel];
        });
    });
}

- (void)reloadPersistedAssets
{
    if (!self.trackerState) return;
    // Applying a project invalidates decodes that still target the previous
    // rack. Reopening the editor later retries any unresolved file paths.
    ++self.rackLoadGeneration;
    [self hydratePersistedAssets];
}

- (void)showWindow:(id)sender
{
    [super showWindow:sender];
    if (self.window.miniaturized) [self.window deminiaturize:sender];
    [self.window makeKeyAndOrderFront:sender];
    [self hydratePersistedAssets];
}

- (void)instanceChanged:(id)sender
{
    (void)sender;
    if (self.trackerState)
        self.trackerState->selectedRackInstrument = [self selectedNode];
    self.waveform.selectedSlice = 0;
    self.waveform.auditionFrame = std::numeric_limits<uint32_t>::max();
    [self.auditionTimer invalidate];
    self.auditionTimer = nil;
    [self.waveform resetZoom];
    [self reloadModel];
}

- (void)loadAudio:(id)sender
{
    (void)sender;
    if (![self selectedSlot]) return;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.message = @"Load a mono or stereo sample into this instrument";
    __weak S3GTrackerSamplerWindowController* weakSelf = self;
    [panel beginSheetModalForWindow:self.window completionHandler:
        ^(NSModalResponse response) {
        S3GTrackerSamplerWindowController* strongSelf = weakSelf;
        NSURL* url = panel.URL;
        if (!strongSelf || response != NSModalResponseOK || !url) return;
        const uint32_t node = [strongSelf selectedNode];
        NSNumber* generationKey = @(node);
        const NSUInteger generation = [strongSelf.manualLoadGenerations[
            generationKey] unsignedIntegerValue] + 1u;
        strongSelf.manualLoadGenerations[generationKey] = @(generation);
        const NSUInteger rackGeneration = strongSelf.rackLoadGeneration;
        [strongSelf beginDecodeActivity];
        strongSelf.detailLabel.stringValue =
            @"DECODING AUDIO AND ANALYZING TRANSIENTS…";
        NSString* path = url.path.copy;
        dispatch_async(dispatch_get_global_queue(
            QOS_CLASS_USER_INITIATED, 0), ^{
            std::shared_ptr<const StereoSampleAsset> asset;
            std::shared_ptr<const StereoSampleAnalysis> analysis;
            std::string error;
            bool decoded = false;
            try {
                decoded = decodeAudioFile(url, asset, analysis, error);
            } catch (...) {
                error = "Audio decoding or analysis ran out of resources";
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                S3GTrackerSamplerWindowController* owner = weakSelf;
                if (!owner) return;
                [owner endDecodeActivity];
                if (rackGeneration != owner.rackLoadGeneration
                    || generation != [owner.manualLoadGenerations[
                        @(node)] unsignedIntegerValue]) return;
                auto* target = samplerSlot(owner.trackerState, node);
                if (!decoded || !target) {
                    [owner reloadModel];
                    NSAlert* alert = [[NSAlert alloc] init];
                    alert.messageText = @"Sample could not be loaded";
                    alert.informativeText = [NSString stringWithUTF8String:
                        error.empty() ? "Sampler instance is unavailable"
                                      : error.c_str()];
                    [alert beginSheetModalForWindow:owner.window
                        completionHandler:nil];
                    return;
                }
                target->asset = std::move(asset);
                target->analysis = std::move(analysis);
                target->filePath = path.UTF8String ? path.UTF8String : "";
                makeEqualSlices(*target, 16u);
                if (owner.waveform.nodeId == node) {
                    owner.waveform.selectedSlice = 0;
                    [owner.waveform resetZoom];
                }
                [owner publish];
            });
        });
    }];
}

- (void)sliceCountChanged:(id)sender
{
    (void)sender;
    auto* slot = [self selectedSlot];
    NSNumber* value = self.sliceCountPopup.selectedItem.representedObject;
    if (!slot || !slot->asset || !value) return;
    makeEqualSlices(*slot, value.unsignedIntegerValue);
    self.waveform.selectedSlice = 0;
    [self publish];
}

- (void)baseNoteChanged:(id)sender
{
    (void)sender;
    auto* slot = [self selectedSlot];
    if (!slot) return;
    slot->baseNote = static_cast<uint8_t>(std::clamp<NSInteger>(
        self.baseNoteField.integerValue, 0, 127));
    [self publish];
}

- (void)sliceChanged:(id)sender
{
    (void)sender;
    self.waveform.selectedSlice = std::max<NSInteger>(0,
        self.slicePopup.indexOfSelectedItem);
    [self reloadModel];
}

- (void)reverseChanged:(id)sender
{
    (void)sender;
    auto* slot = [self selectedSlot];
    const NSInteger selected = self.slicePopup.indexOfSelectedItem;
    if (!slot || selected < 0
        || static_cast<std::size_t>(selected) >= slot->sliceCount) return;
    slot->slices[static_cast<std::size_t>(selected)].reverse
        = self.reverseButton.state == NSControlStateValueOn;
    [self publish];
}

- (void)gainChanged:(id)sender
{
    (void)sender;
    auto* slot = [self selectedSlot];
    const NSInteger selected = self.slicePopup.indexOfSelectedItem;
    if (!slot || selected < 0
        || static_cast<std::size_t>(selected) >= slot->sliceCount) return;
    slot->slices[static_cast<std::size_t>(selected)].gain
        = static_cast<float>(std::clamp(self.gainSlider.doubleValue,
            0.0, 2.0));
    [self publish];
}

- (void)envelopeChanged:(id)sender
{
    auto* slot = [self selectedSlot];
    if (!slot) return;
    if (sender == self.attackSlider) {
        slot->envelope.attackMilliseconds = samplerEnvelopeTimeFromSlider(
            self.attackSlider.doubleValue);
    } else if (sender == self.decaySlider) {
        slot->envelope.decayMilliseconds = samplerEnvelopeTimeFromSlider(
            self.decaySlider.doubleValue);
    } else if (sender == self.sustainSlider) {
        slot->envelope.sustain = static_cast<float>(std::clamp(
            self.sustainSlider.doubleValue, 0.0, 1.0));
    } else if (sender == self.releaseSlider) {
        slot->envelope.releaseMilliseconds = samplerEnvelopeTimeFromSlider(
            self.releaseSlider.doubleValue);
    } else {
        return;
    }
    [self publish];
}

- (void)autoSlice:(id)sender
{
    (void)sender;
    auto* slot = [self selectedSlot];
    if (!slot || !slot->asset || !slot->analysis) return;
    const uint32_t snapRadius = self.waveform.snapToZero
        ? static_cast<uint32_t>(std::max<double>(1.0,
            std::round(slot->asset->sampleRate * 0.004))) : 0u;
    const auto slices = s3g::tracker::audio::makeTransientSampleSlices(
        *slot->asset, *slot->analysis,
        s3g::tracker::audio::kMaximumSamplerSlices, snapRadius);
    if (slices.empty()) return;
    slot->slices = {};
    slot->sliceCount = slices.size();
    std::copy(slices.begin(), slices.end(), slot->slices.begin());
    self.waveform.selectedSlice = 0;
    [self publish];
}

- (void)addMarker:(id)sender
{
    (void)sender;
    [self.waveform addMarkerAtCursor];
}

- (void)deleteMarker:(id)sender
{
    (void)sender;
    [self.waveform deleteSelectedMarker];
}

- (void)zeroSnapChanged:(id)sender
{
    (void)sender;
    self.waveform.snapToZero =
        self.zeroSnapButton.state == NSControlStateValueOn;
}

- (void)zoomOut:(id)sender
{
    (void)sender;
    [self.waveform zoomBy:0.5];
}

- (void)zoomIn:(id)sender
{
    (void)sender;
    [self.waveform zoomBy:2.0];
}

- (void)zoomReset:(id)sender
{
    (void)sender;
    [self.waveform resetZoom];
}

- (void)samplerWaveformSelectionChanged
{
    [self reloadModel];
}

- (void)samplerWaveformSlicesChanged
{
    [self publish];
}

- (void)updateAuditionPlayhead:(NSTimer*)timer
{
    (void)timer;
    auto* slot = [self selectedSlot];
    if (!slot || !slot->asset || self.auditionEndFrame <= self.auditionStartFrame) {
        [self.auditionTimer invalidate];
        self.auditionTimer = nil;
        self.waveform.auditionFrame = std::numeric_limits<uint32_t>::max();
        [self.waveform setNeedsDisplay:YES];
        return;
    }
    const NSTimeInterval elapsed = [NSDate timeIntervalSinceReferenceDate]
        - self.auditionStartTime;
    const uint64_t advanced = static_cast<uint64_t>(std::max<double>(0.0,
        elapsed * slot->asset->sampleRate));
    const uint32_t length = self.auditionEndFrame - self.auditionStartFrame;
    if (advanced >= length) {
        [self.auditionTimer invalidate];
        self.auditionTimer = nil;
        self.waveform.auditionFrame = std::numeric_limits<uint32_t>::max();
    } else {
        self.waveform.auditionFrame = self.auditionReverse
            ? self.auditionEndFrame - 1u - static_cast<uint32_t>(advanced)
            : self.auditionStartFrame + static_cast<uint32_t>(advanced);
    }
    [self.waveform setNeedsDisplay:YES];
}

- (void)audition:(id)sender
{
    (void)sender;
    auto* slot = [self selectedSlot];
    if (!slot || !slot->asset || !self.trackerCallbacks
        || !self.trackerCallbacks->auditionInstrument) return;
    const auto slice = static_cast<uint8_t>(std::clamp<NSInteger>(
        self.slicePopup.indexOfSelectedItem, 0, 127));
    uint8_t note = 0u;
    if (!s3g::tracker::audio::samplerNoteForSlice(slot->baseNote, slice,
            note)) return;
    self.trackerCallbacks->auditionInstrument([self selectedNode], note, 0.9f);
    const auto& selected = slot->slices[slice];
    [self.auditionTimer invalidate];
    self.auditionStartTime = [NSDate timeIntervalSinceReferenceDate];
    self.auditionStartFrame = selected.startFrame;
    self.auditionEndFrame = selected.endFrame;
    self.auditionReverse = selected.reverse;
    self.waveform.auditionFrame = selected.reverse
        ? selected.endFrame - 1u : selected.startFrame;
    self.auditionTimer = [NSTimer scheduledTimerWithTimeInterval:(1.0 / 60.0)
        target:self selector:@selector(updateAuditionPlayhead:)
        userInfo:nil repeats:YES];
}

- (void)windowWillClose:(NSNotification*)notification
{
    (void)notification;
    [self.auditionTimer invalidate];
    self.auditionTimer = nil;
}

@end
