#include "s3g_vox_builder.h"
#include "s3g_vox_source_synth.h"
#include "s3g_cocoa_gui.h"

#import <AVFoundation/AVFoundation.h>
#import <Cocoa/Cocoa.h>

#include <world/dio.h>
#include <world/harvest.h>
#include <world/stonemask.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr CGFloat kGuiWidth = 1100.0;
constexpr CGFloat kGuiHeight = 780.0;
constexpr CGFloat kRightX = 804.0;
constexpr CGFloat kRightWidth = 278.0;
constexpr CGFloat kControlTrackX = 914.0;
constexpr CGFloat kControlTrackWidth = 104.0;

constexpr const char* kDefaultAliases =
    "a e i o u\n"
    "ka ke ki ko ku\n"
    "sa se si so su\n"
    "ta te ti to tu\n"
    "na ne ni no nu\n"
    "ma me mi mo mu\n"
    "ra re ri ro ru";

struct WorldMetrics {
    int baseMidi = 60;
    float voicedRatio = 0.0f;
};

struct DecodedWav {
    std::vector<float> samples;
    int sampleRate = 0;
    s3g::VoxBuilderLevelReport level;
};

static NSColor* color(int rgb, double alpha = 1.0)
{
    return s3g::clap_gui::color(rgb, alpha);
}

static bool isWavURL(NSURL* url)
{
    if (!url) return false;
    NSString* extension = [[url pathExtension] lowercaseString];
    return [extension isEqualToString:@"wav"] || [extension isEqualToString:@"wave"];
}

static bool decodeMonoWav(NSURL* url, DecodedWav& decoded)
{
    NSError* error = nil;
    AVAudioFile* file = [[AVAudioFile alloc] initForReading:url error:&error];
    if (!file || error || [file length] <= 0 || [file length] > UINT32_MAX) return false;
    AVAudioFormat* format = [file processingFormat];
    AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc]
        initWithPCMFormat:format frameCapacity:static_cast<AVAudioFrameCount>([file length])];
    if (![file readIntoBuffer:buffer error:&error] || error || ![buffer floatChannelData]) return false;

    const uint32_t channels = std::max<uint32_t>(1u, [format channelCount]);
    const uint32_t frames = [buffer frameLength];
    decoded.samples.assign(frames, 0.0f);
    decoded.sampleRate = static_cast<int>(std::lround([format sampleRate]));
    float* const* channelData = [buffer floatChannelData];
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        float sum = 0.0f;
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            sum += channelData[channel][frame];
        }
        decoded.samples[frame] = std::clamp(
            sum / static_cast<float>(channels), -1.0f, 1.0f);
    }
    return decoded.sampleRate > 0 && !decoded.samples.empty();
}

static WorldMetrics analyzeWorldPitch(const std::vector<float>& samples,
                                      int sampleRate,
                                      uint64_t start,
                                      uint64_t end)
{
    WorldMetrics result {};
    if (sampleRate <= 0 || samples.size() < 256u || start >= samples.size()) return result;
    end = std::min<uint64_t>(end, samples.size());
    if (end <= start + 255u || end - start > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        return result;
    }
    std::vector<double> source(static_cast<size_t>(end - start));
    for (size_t i = 0u; i < source.size(); ++i) {
        source[i] = static_cast<double>(std::clamp(samples[static_cast<size_t>(start) + i], -1.0f, 1.0f));
    }

    HarvestOption harvest {};
    InitializeHarvestOption(&harvest);
    harvest.frame_period = 5.0;
    harvest.f0_floor = 40.0;
    harvest.f0_ceil = 1600.0;
    const int frameCount = GetSamplesForHarvest(
        sampleRate, static_cast<int>(source.size()), harvest.frame_period);
    if (frameCount <= 0) return result;
    std::vector<double> positions(static_cast<size_t>(frameCount));
    std::vector<double> f0(static_cast<size_t>(frameCount));
    Harvest(source.data(), static_cast<int>(source.size()), sampleRate, &harvest,
        positions.data(), f0.data());

    std::vector<double> voiced;
    voiced.reserve(f0.size());
    for (const double frequency : f0) {
        if (std::isfinite(frequency) && frequency >= 40.0 && frequency <= 1600.0) {
            voiced.push_back(frequency);
        }
    }
    if (voiced.empty()) {
        DioOption dio {};
        InitializeDioOption(&dio);
        dio.frame_period = 5.0;
        dio.f0_floor = 40.0;
        dio.f0_ceil = 1600.0;
        const int dioCount = GetSamplesForDIO(
            sampleRate, static_cast<int>(source.size()), dio.frame_period);
        if (dioCount > 0) {
            std::vector<double> dioPositions(static_cast<size_t>(dioCount));
            std::vector<double> dioF0(static_cast<size_t>(dioCount));
            std::vector<double> refined(static_cast<size_t>(dioCount));
            Dio(source.data(), static_cast<int>(source.size()), sampleRate, &dio,
                dioPositions.data(), dioF0.data());
            StoneMask(source.data(), static_cast<int>(source.size()), sampleRate,
                dioPositions.data(), dioF0.data(), dioCount, refined.data());
            f0 = std::move(refined);
            for (const double frequency : f0) {
                if (std::isfinite(frequency) && frequency >= 40.0 && frequency <= 1600.0) {
                    voiced.push_back(frequency);
                }
            }
        }
    }
    if (voiced.empty()) return result;
    const auto middle = voiced.begin() + static_cast<std::ptrdiff_t>(voiced.size() / 2u);
    std::nth_element(voiced.begin(), middle, voiced.end());
    result.baseMidi = std::clamp(static_cast<int>(std::lround(
        69.0 + 12.0 * std::log2(*middle / 440.0))), 0, 127);
    result.voicedRatio = static_cast<float>(voiced.size())
        / static_cast<float>(std::max<size_t>(1u, f0.size()));
    return result;
}

static void appendU16(std::vector<uint8_t>& bytes, uint16_t value)
{
    bytes.push_back(static_cast<uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
}

static void appendU32(std::vector<uint8_t>& bytes, uint32_t value)
{
    bytes.push_back(static_cast<uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
    bytes.push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
    bytes.push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
}

static NSData* makeWavData(const std::vector<float>& samples,
                           uint64_t start,
                           uint64_t end,
                           int sampleRate)
{
    if (sampleRate <= 0 || start >= samples.size()) return nil;
    end = std::min<uint64_t>(end, samples.size());
    if (end <= start) return nil;
    const uint64_t sampleCount64 = end - start;
    if (sampleCount64 > (std::numeric_limits<uint32_t>::max() - 44u) / 2u) return nil;
    const uint32_t sampleCount = static_cast<uint32_t>(sampleCount64);
    const uint32_t dataSize = sampleCount * 2u;
    std::vector<uint8_t> bytes;
    bytes.reserve(static_cast<size_t>(dataSize) + 44u);
    bytes.insert(bytes.end(), { 'R', 'I', 'F', 'F' });
    appendU32(bytes, 36u + dataSize);
    bytes.insert(bytes.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
    appendU32(bytes, 16u);
    appendU16(bytes, 1u);
    appendU16(bytes, 1u);
    appendU32(bytes, static_cast<uint32_t>(sampleRate));
    appendU32(bytes, static_cast<uint32_t>(sampleRate) * 2u);
    appendU16(bytes, 2u);
    appendU16(bytes, 16u);
    bytes.insert(bytes.end(), { 'd', 'a', 't', 'a' });
    appendU32(bytes, dataSize);
    const uint32_t fadeSamples = std::min<uint32_t>(sampleCount / 2u,
        std::max<uint32_t>(1u, static_cast<uint32_t>(sampleRate) / 250u));
    for (uint32_t i = 0u; i < sampleCount; ++i) {
        float gain = 1.0f;
        if (i < fadeSamples) gain = static_cast<float>(i) / static_cast<float>(fadeSamples);
        if (sampleCount - i - 1u < fadeSamples) {
            gain = std::min(gain, static_cast<float>(sampleCount - i - 1u)
                / static_cast<float>(fadeSamples));
        }
        const float sample = std::clamp(samples[static_cast<size_t>(start) + i] * gain, -1.0f, 1.0f);
        const int16_t value = static_cast<int16_t>(std::lround(sample * 32767.0f));
        appendU16(bytes, static_cast<uint16_t>(value));
    }
    return [NSData dataWithBytes:bytes.data() length:bytes.size()];
}

static NSString* cleanOtoAlias(NSString* alias)
{
    NSString* result = alias ?: @"voice";
    for (NSString* character in @[ @",", @"=", @"\n", @"\r" ]) {
        result = [result stringByReplacingOccurrencesOfString:character withString:@"_"];
    }
    return result;
}

enum class ActionButtonTone : uint8_t {
    Neutral,
    Required,
    Ready,
};

static void drawActionButton(NSRect rect, NSString* label, NSDictionary* attrs,
                             bool enabled = true,
                             ActionButtonTone tone = ActionButtonTone::Neutral)
{
    uint32_t fill = enabled ? 0x202020 : 0x151515;
    uint32_t outer = enabled ? 0x9a9a9a : 0x444444;
    uint32_t inner = enabled ? 0x303030 : 0x1b1b1b;
    if (tone == ActionButtonTone::Required) {
        fill = 0x3b2023;
        outer = 0xa65d63;
        inner = 0x602f34;
    } else if (tone == ActionButtonTone::Ready) {
        fill = 0x203329;
        outer = 0x69a17b;
        inner = 0x345542;
    }
    [color(fill) setFill];
    NSRectFill(rect);
    [color(outer) setStroke];
    NSFrameRect(rect);
    [color(inner) setStroke];
    NSFrameRect(NSInsetRect(rect, 1.0, 1.0));
    const NSSize size = [label sizeWithAttributes:attrs];
    [label drawAtPoint:NSMakePoint(
        rect.origin.x + (rect.size.width - size.width) * 0.5,
        rect.origin.y + (rect.size.height - size.height) * 0.5 - 0.5)
        withAttributes:attrs];
}

static bool writeViewSnapshot(NSView* view, NSString* path)
{
    if (!view || [path length] == 0u) return false;
    [view layoutSubtreeIfNeeded];
    [view displayIfNeeded];
    NSBitmapImageRep* bitmap = [view bitmapImageRepForCachingDisplayInRect:[view bounds]];
    if (!bitmap) return false;
    [view cacheDisplayInRect:[view bounds] toBitmapImageRep:bitmap];
    NSData* png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    return png && [png writeToFile:path atomically:YES];
}

} // namespace

@class S3GVoxGeneratorView;

@interface S3GVoxBuilderView : NSView <NSTextViewDelegate, NSTextFieldDelegate,
    NSComboBoxDelegate, NSDraggingDestination>
- (BOOL)loadAudioURLs:(NSArray<NSURL*>*)selection;
- (BOOL)hasSeedSource;
- (NSString*)selectedSeedDescription;
- (float)selectedSeedFrequency;
- (BOOL)beginVoiceGeneration:(s3g::VoxSourceSynthParams)params
                  vocabulary:(s3g::VoxSourceVocabulary)vocabulary;
@end

@interface S3GVoxGeneratorView : NSView
- (instancetype)initWithFrame:(NSRect)frame builder:(S3GVoxBuilderView*)builder;
- (void)refreshSeedState;
@end

@interface S3GVoxBuilderView ()
- (void)showGenerator;
- (void)installGeneratedVoicebank:(std::shared_ptr<s3g::VoxGeneratedVoicebank>)bank
                       description:(NSString*)description
                       synthParams:(s3g::VoxSourceSynthParams)params
                        vocabulary:(s3g::VoxSourceVocabulary)vocabulary;
@end

@implementation S3GVoxBuilderView {
    std::shared_ptr<const std::vector<float>> _samples;
    std::vector<s3g::VoxBuilderSegment> _segments;
    std::vector<float> _waveMinimum;
    std::vector<float> _waveMaximum;
    int _sampleRate;
    size_t _selectedSegment;
    NSInteger _dragBoundary;
    NSInteger _dragSlider;
    s3g::VoxBuilderSettings _settings;
    s3g::VoxBuilderLevelReport _singleSourceLevel;
    std::atomic<uint64_t> _analysisGeneration;
    std::atomic<uint32_t> _analysisProgress;
    std::atomic<bool> _analyzing;
    std::atomic<bool> _generating;

    NSArray<NSURL*>* _sourceURLs;
    NSString* _generatedSourceName;
    NSDictionary* _generationMetadata;
    BOOL _multiFileSource;
    AVAudioPlayer* _previewPlayer;
    NSTimer* _timer;
    NSString* _status;
    BOOL _updatingFields;

    NSTextField* _nameField;
    NSComboBox* _aliasField;
    NSTextField* _rootField;
    NSTextField* _fixedField;
    NSTextField* _preutterField;
    NSTextField* _overlapField;
    NSScrollView* _phonemeScroll;
    NSTextView* _phonemeEditor;
    NSPanel* _aliasGuideWindow;
    std::vector<s3g::VoxBuilderSegment> _segmentsBeforeGuess;
    BOOL _canUndoGuess;
    BOOL _aliasesDirty;
    NSPanel* _generatorWindow;
    S3GVoxGeneratorView* _generatorView;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (!self) return nil;
    _sampleRate = 0;
    _selectedSegment = 0u;
    _dragBoundary = -1;
    _dragSlider = 0;
    _multiFileSource = NO;
    _canUndoGuess = NO;
    _aliasesDirty = NO;
    _generating.store(false, std::memory_order_relaxed);
    _status = @"LOAD A VOICE RECORDING";
    _samples = std::make_shared<const std::vector<float>>();
    [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];

    _nameField = [self makeTextField:NSMakeRect(912, 76, 152, 20) alignment:NSTextAlignmentLeft];
    [_nameField setStringValue:@"s3g_voicebank"];
    _aliasField = [[NSComboBox alloc] initWithFrame:NSMakeRect(912, 438, 152, 20)];
    [_aliasField setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]];
    s3g::clap_gui::styleNumberTextField(_aliasField, 10.5, NSTextAlignmentLeft);
    [_aliasField setDelegate:self];
    [_aliasField setCompletes:YES];
    [_aliasField setNumberOfVisibleItems:14];
    [_aliasField setToolTip:@"Automatic Ambi Vox phrase aliases; custom exact aliases are also accepted."];
    for (const auto& alias : s3g::voxBuilderAutoPhraseAliases()) {
        [_aliasField addItemWithObjectValue:[NSString stringWithUTF8String:alias.c_str()]];
    }
    [self addSubview:_aliasField];
    _rootField = [self makeTextField:NSMakeRect(1008, 468, 56, 20) alignment:NSTextAlignmentRight];
    _fixedField = [self makeTextField:NSMakeRect(1008, 498, 56, 20) alignment:NSTextAlignmentRight];
    _preutterField = [self makeTextField:NSMakeRect(1008, 528, 56, 20) alignment:NSTextAlignmentRight];
    _overlapField = [self makeTextField:NSMakeRect(1008, 558, 56, 20) alignment:NSTextAlignmentRight];

    _phonemeScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(30, 516, 746, 232)];
    [_phonemeScroll setHasVerticalScroller:YES];
    [_phonemeScroll setHasHorizontalScroller:NO];
    [_phonemeScroll setBorderType:NSNoBorder];
    [_phonemeScroll setDrawsBackground:YES];
    [_phonemeScroll setBackgroundColor:color(0x101010)];
    _phonemeEditor = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 728, 232)];
    [_phonemeEditor setDelegate:self];
    [_phonemeEditor setRichText:NO];
    [_phonemeEditor setImportsGraphics:NO];
    [_phonemeEditor setAllowsUndo:YES];
    [_phonemeEditor setHorizontallyResizable:NO];
    [_phonemeEditor setVerticallyResizable:YES];
    [_phonemeEditor setAutoresizingMask:NSViewWidthSizable];
    [[_phonemeEditor textContainer] setWidthTracksTextView:YES];
    [[_phonemeEditor textContainer] setContainerSize:NSMakeSize(728, CGFLOAT_MAX)];
    [_phonemeEditor setFont:s3g::clap_gui::uiFont(11.0)];
    [_phonemeEditor setTextColor:color(0xb0b0b0)];
    [_phonemeEditor setInsertionPointColor:color(0xd0d0d0)];
    [_phonemeEditor setBackgroundColor:color(0x101010)];
    [_phonemeEditor setSelectedTextAttributes:@{
        NSBackgroundColorAttributeName:color(0x4a4a4a),
        NSForegroundColorAttributeName:color(0xf0f0f0)
    }];
    [_phonemeEditor setString:[NSString stringWithUTF8String:kDefaultAliases]];
    [_phonemeScroll setDocumentView:_phonemeEditor];
    [self addSubview:_phonemeScroll];

    _timer = [NSTimer scheduledTimerWithTimeInterval:0.05
        target:self selector:@selector(animationTick:) userInfo:nil repeats:YES];
    return self;
}

- (void)dealloc
{
    [_timer invalidate];
    _analysisGeneration.fetch_add(1u, std::memory_order_acq_rel);
    [_generatorWindow orderOut:nil];
}

- (NSTextField*)makeTextField:(NSRect)frame alignment:(NSTextAlignment)alignment
{
    NSTextField* field = [[NSTextField alloc] initWithFrame:frame];
    s3g::clap_gui::styleNumberTextField(field, 10.5, alignment);
    [field setDelegate:self];
    [self addSubview:field];
    return field;
}

- (NSRect)waveformRect { return NSMakeRect(30, 78, 746, 390); }
- (NSRect)loadButtonRect { return NSMakeRect(820, 112, 116, 24); }
- (NSRect)generateButtonRect { return NSMakeRect(948, 112, 116, 24); }
- (NSRect)analyzeButtonRect { return NSMakeRect(820, 336, 116, 24); }
- (NSRect)autoGuessButtonRect { return NSMakeRect(948, 336, 116, 24); }
- (NSRect)aliasGuideButtonRect { return NSMakeRect(664, 486, 112, 22); }
- (NSRect)auditionButtonRect { return NSMakeRect(820, 590, 116, 24); }
- (NSRect)exportButtonRect { return NSMakeRect(820, 686, 244, 26); }
- (NSRect)removeSegmentButtonRect { return NSMakeRect(1036, 408, 18, 13); }
- (NSRect)addSegmentButtonRect { return NSMakeRect(1058, 408, 18, 13); }

- (void)animationTick:(NSTimer*)timer
{
    (void)timer;
    if (_analyzing.load(std::memory_order_relaxed)
        || _generating.load(std::memory_order_relaxed)
        || (_previewPlayer && [_previewPlayer isPlaying])) {
        [self setNeedsDisplay:YES];
    }
}

- (void)rebuildWaveOverview
{
    constexpr size_t bins = 1492u;
    _waveMinimum.assign(bins, 0.0f);
    _waveMaximum.assign(bins, 0.0f);
    if (!_samples || _samples->empty()) return;
    for (size_t bin = 0u; bin < bins; ++bin) {
        const size_t start = _samples->size() * bin / bins;
        const size_t end = std::max(start + 1u, _samples->size() * (bin + 1u) / bins);
        float minimum = 0.0f;
        float maximum = 0.0f;
        for (size_t i = start; i < std::min(end, _samples->size()); ++i) {
            minimum = std::min(minimum, (*_samples)[i]);
            maximum = std::max(maximum, (*_samples)[i]);
        }
        _waveMinimum[bin] = minimum;
        _waveMaximum[bin] = maximum;
    }
}

- (void)setStatus:(NSString*)status
{
    _status = status ?: @"";
    [self setNeedsDisplay:YES];
}

- (BOOL)canExportVoicebank
{
    return _samples && !_samples->empty() && !_segments.empty() && _sampleRate > 0
        && !_aliasesDirty
        && !_analyzing.load(std::memory_order_acquire)
        && !_generating.load(std::memory_order_acquire);
}

- (NSString*)aliasGuideText
{
    NSMutableString* text = [NSMutableString stringWithString:
        @"WHAT AMBI VOX UNDERSTANDS\n\n"
         "Any clean alias is valid. A custom alias can be entered directly in a phrase, or mapped "
         "from a word with s3g-pronunciations.txt. The finite list below is the vocabulary emitted "
         "by Ambi Vox's built-in English-to-CV mapper.\n\n"
         "AUTOMATIC PHRASE VOCABULARY\n"
         "a  e  i  o  u\n"];
    const auto& aliases = s3g::voxBuilderAutoPhraseAliases();
    for (size_t first = 5u; first + 4u < aliases.size() - 2u; first += 5u) {
        [text appendFormat:@"%-4s %-4s %-4s %-4s %-4s\n",
            aliases[first].c_str(), aliases[first + 1u].c_str(), aliases[first + 2u].c_str(),
            aliases[first + 3u].c_str(), aliases[first + 4u].c_str()];
    }
    [text appendString:@"n    ya\n\n"
        "FALLBACK PAIRS\n"
        "si / shi    zi / ji    ti / chi    tu / tsu    hu / fu\n"
        "du / zu     di / ji    ly / ry     l / r       wu / u       ye / e       wi / i\n\n"
        "ACOUSTIC AUTO GUESS\n"
        "AUTO GUESS works against the core 35-alias matrix below. Isolated audio can support a "
        "useful vowel and broad-onset estimate, but it cannot make every consonant distinction "
        "reliably without a trained speaker-specific model. Filename matches and manually edited "
        "aliases are retained, as is a high-confidence recording order. Review low M values by "
        "auditioning them.\n\n"
        "a e i o u\n"
        "ka ke ki ko ku\n"
        "sa se si so su\n"
        "ta te ti to tu\n"
        "na ne ni no nu\n"
        "ma me mi mo mu\n"
        "ra re ri ro ru\n"];
    return text;
}

- (void)showAliasGuide
{
    if (!_aliasGuideWindow) {
        const NSRect frame = NSMakeRect(0, 0, 660, 600);
        _aliasGuideWindow = [[NSPanel alloc] initWithContentRect:frame
            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
            backing:NSBackingStoreBuffered defer:NO];
        [_aliasGuideWindow setTitle:@"s3g Vox Builder Alias Guide"];
        [_aliasGuideWindow setReleasedWhenClosed:NO];
        NSView* content = [[NSView alloc] initWithFrame:frame];
        [content setWantsLayer:YES];
        [[content layer] setBackgroundColor:[color(0x121212) CGColor]];
        NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(18, 18, 624, 564)];
        [scroll setHasVerticalScroller:YES];
        [scroll setBorderType:NSNoBorder];
        [scroll setDrawsBackground:YES];
        [scroll setBackgroundColor:color(0x181818)];
        NSTextView* guide = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 606, 564)];
        [guide setEditable:NO];
        [guide setSelectable:YES];
        [guide setRichText:NO];
        [guide setHorizontallyResizable:NO];
        [guide setVerticallyResizable:YES];
        [guide setAutoresizingMask:NSViewWidthSizable];
        [[guide textContainer] setWidthTracksTextView:YES];
        [[guide textContainer] setContainerSize:NSMakeSize(606, CGFLOAT_MAX)];
        [guide setTextContainerInset:NSMakeSize(14, 14)];
        [guide setFont:[NSFont monospacedSystemFontOfSize:11.0 weight:NSFontWeightRegular]];
        [guide setTextColor:color(0xb0b0b0)];
        [guide setInsertionPointColor:color(0xd0d0d0)];
        [guide setBackgroundColor:color(0x181818)];
        [guide setString:[self aliasGuideText]];
        [scroll setDocumentView:guide];
        [content addSubview:scroll];
        [_aliasGuideWindow setContentView:content];
        [_aliasGuideWindow center];
    }
    [_aliasGuideWindow makeKeyAndOrderFront:nil];
}

- (BOOL)loadAudioURL:(NSURL*)url
{
    if (!url) return NO;
    DecodedWav decoded;
    if (!decodeMonoWav(url, decoded)) {
        [self setStatus:@"COULD NOT READ WAV"];
        NSBeep();
        return NO;
    }
    if (decoded.samples.size() < static_cast<size_t>(decoded.sampleRate) / 20u) {
        [self setStatus:@"WAV IS TOO SHORT"];
        NSBeep();
        return NO;
    }
    decoded.level = s3g::voxBuilderConditionAudio(decoded.samples, decoded.sampleRate);
    if (!decoded.level.usable) {
        [self setStatus:@"WAV HAS NO USABLE AUDIO"];
        NSBeep();
        return NO;
    }
    _analysisGeneration.fetch_add(1u, std::memory_order_acq_rel);
    _analyzing.store(false, std::memory_order_release);
    _generating.store(false, std::memory_order_release);
    _samples = std::make_shared<const std::vector<float>>(std::move(decoded.samples));
    _sampleRate = decoded.sampleRate;
    _singleSourceLevel = decoded.level;
    _sourceURLs = @[ url ];
    _generatedSourceName = nil;
    _generationMetadata = nil;
    _multiFileSource = NO;
    _segments.clear();
    _segmentsBeforeGuess.clear();
    _canUndoGuess = NO;
    _selectedSegment = 0u;
    [self rebuildWaveOverview];
    NSString* stem = [[[url lastPathComponent] stringByDeletingPathExtension]
        stringByReplacingOccurrencesOfString:@" " withString:@"_"];
    if ([stem length] > 0u) [_nameField setStringValue:stem];
    [self setStatus:@"SOURCE LOADED - ANALYZING"];
    [self analyzeSource];
    return YES;
}

- (NSArray<NSURL*>*)expandedWavURLs:(NSArray<NSURL*>*)selection
{
    NSMutableArray<NSURL*>* result = [NSMutableArray array];
    NSMutableSet<NSString*>* seen = [NSMutableSet set];
    NSFileManager* fileManager = [NSFileManager defaultManager];
    const auto addURL = ^(NSURL* candidate) {
        if (!isWavURL(candidate)) return;
        NSString* key = [[candidate URLByStandardizingPath] path];
        if ([seen containsObject:key]) return;
        [seen addObject:key];
        [result addObject:candidate];
    };
    for (NSURL* url in selection) {
        NSNumber* isDirectory = nil;
        [url getResourceValue:&isDirectory forKey:NSURLIsDirectoryKey error:nil];
        if (![isDirectory boolValue]) {
            addURL(url);
            continue;
        }
        NSDirectoryEnumerator<NSURL*>* enumerator = [fileManager
            enumeratorAtURL:url
            includingPropertiesForKeys:@[ NSURLIsRegularFileKey ]
            options:NSDirectoryEnumerationSkipsHiddenFiles
            errorHandler:^BOOL(NSURL* failedURL, NSError* error) {
                (void)failedURL;
                (void)error;
                return YES;
            }];
        for (NSURL* candidate in enumerator) addURL(candidate);
    }
    [result sortUsingComparator:^NSComparisonResult(NSURL* first, NSURL* second) {
        return [[first path] localizedStandardCompare:[second path]];
    }];
    return result;
}

- (BOOL)loadAudioURLs:(NSArray<NSURL*>*)selection
{
    NSArray<NSURL*>* urls = [self expandedWavURLs:selection];
    if ([urls count] == 0u) {
        [self setStatus:@"NO WAV FILES FOUND"];
        NSBeep();
        return NO;
    }
    if ([urls count] == 1u) return [self loadAudioURL:[urls firstObject]];

    std::vector<DecodedWav> decoded;
    decoded.reserve([urls count]);
    uint64_t totalSamples = 0u;
    int sampleRate = 0;
    for (NSURL* url in urls) {
        DecodedWav wav;
        if (!decodeMonoWav(url, wav)) {
            [self setStatus:@"COULD NOT READ ONE OR MORE WAVS"];
            NSBeep();
            return NO;
        }
        if (sampleRate == 0) sampleRate = wav.sampleRate;
        if (wav.sampleRate != sampleRate) {
            [self setStatus:@"WAV SAMPLE RATES MUST MATCH"];
            NSBeep();
            return NO;
        }
        if (wav.samples.size() < static_cast<size_t>(sampleRate) / 20u) {
            [self setStatus:[NSString stringWithFormat:@"TOO SHORT: %@", [url lastPathComponent]]];
            NSBeep();
            return NO;
        }
        wav.level = s3g::voxBuilderConditionAudio(wav.samples, sampleRate);
        if (!wav.level.usable) {
            [self setStatus:[NSString stringWithFormat:@"NO USABLE AUDIO: %@", [url lastPathComponent]]];
            NSBeep();
            return NO;
        }
        totalSamples += wav.samples.size();
        if (totalSamples > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
            [self setStatus:@"WAV SET IS TOO LARGE"];
            NSBeep();
            return NO;
        }
        decoded.push_back(std::move(wav));
    }

    auto samples = std::make_shared<std::vector<float>>();
    samples->reserve(static_cast<size_t>(totalSamples));
    std::vector<s3g::VoxBuilderSegment> segments;
    segments.reserve(decoded.size());
    NSMutableArray<NSString*>* aliases = [NSMutableArray arrayWithCapacity:[urls count]];
    const char* inventoryText = [[_phonemeEditor string] UTF8String];
    const auto inventory = s3g::voxBuilderParseAliases(inventoryText ? inventoryText : "");
    std::vector<bool> inventoryUsed(inventory.size(), false);
    size_t fallbackIndex = 0u;
    for (size_t i = 0u; i < decoded.size(); ++i) {
        const uint64_t start = samples->size();
        samples->insert(samples->end(), decoded[i].samples.begin(), decoded[i].samples.end());
        NSString* stem = [[[urls objectAtIndex:i] lastPathComponent] stringByDeletingPathExtension];
        if ([stem length] == 0u) stem = [NSString stringWithFormat:@"voice_%zu", i + 1u];
        std::string alias = [stem UTF8String] ?: "voice";
        float aliasConfidence = 0.25f;
        s3g::VoxBuilderAliasAssignment aliasAssignment =
            s3g::VoxBuilderAliasAssignment::Unknown;
        const int matched = s3g::voxBuilderAliasMatchIndex(alias, inventory);
        if (matched >= 0) {
            alias = inventory[static_cast<size_t>(matched)];
            inventoryUsed[static_cast<size_t>(matched)] = true;
            aliasConfidence = 0.95f;
            aliasAssignment = s3g::VoxBuilderAliasAssignment::Filename;
        } else {
            while (fallbackIndex < inventory.size() && inventoryUsed[fallbackIndex]) ++fallbackIndex;
            if (fallbackIndex < inventory.size()) {
                alias = inventory[fallbackIndex];
                inventoryUsed[fallbackIndex] = true;
                ++fallbackIndex;
                aliasConfidence = 0.55f;
                aliasAssignment = s3g::VoxBuilderAliasAssignment::Ordered;
            }
        }
        NSString* displayAlias = [NSString stringWithUTF8String:alias.c_str()];
        [aliases addObject:displayAlias];
        s3g::VoxBuilderSegment segment {};
        segment.alias = std::move(alias);
        segment.startSample = start;
        segment.endSample = samples->size();
        segment.aliasConfidence = aliasConfidence;
        segment.aliasAssignment = aliasAssignment;
        segment.normalizationGainDb = decoded[i].level.normalizationGainDb;
        segment.sourcePeakDb = decoded[i].level.sourcePeakDb;
        segment.sourceClippedRatio = decoded[i].level.clippedRatio;
        segment.normalizationLimited = decoded[i].level.boostLimited;
        s3g::voxBuilderSetSuggestedTiming(segment, sampleRate);
        segments.push_back(std::move(segment));
    }

    _analysisGeneration.fetch_add(1u, std::memory_order_acq_rel);
    _analyzing.store(false, std::memory_order_release);
    _generating.store(false, std::memory_order_release);
    _samples = std::move(samples);
    _sampleRate = sampleRate;
    _sourceURLs = [urls copy];
    _generatedSourceName = nil;
    _generationMetadata = nil;
    _multiFileSource = YES;
    _segments = std::move(segments);
    _segmentsBeforeGuess.clear();
    _canUndoGuess = NO;
    _selectedSegment = 0u;
    [_phonemeEditor setString:[aliases componentsJoinedByString:@"\n"]];
    _aliasesDirty = NO;
    NSURL* parent = [[urls firstObject] URLByDeletingLastPathComponent];
    NSString* bankName = [parent lastPathComponent];
    if ([bankName length] > 0u) [_nameField setStringValue:bankName];
    [self rebuildWaveOverview];
    [self refreshSegmentFields];
    [self setStatus:@"WAV SET LOADED - ANALYZING"];
    [self startWorldAnalysis];
    [[self window] invalidateCursorRectsForView:self];
    return YES;
}

- (void)openSource
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setAllowsMultipleSelection:YES];
    [panel setCanChooseDirectories:YES];
    [panel setCanChooseFiles:YES];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [panel setAllowedFileTypes:@[ @"wav", @"wave" ]];
#pragma clang diagnostic pop
    if ([panel runModal] == NSModalResponseOK) [self loadAudioURLs:[panel URLs]];
}

- (BOOL)hasSeedSource
{
    return _samples && !_samples->empty() && !_segments.empty();
}

- (NSString*)selectedSeedDescription
{
    if (![self hasSeedSource]) return @"LOAD AND SELECT A VOCAL SEGMENT";
    const auto& segment = _segments[std::min(_selectedSegment, _segments.size() - 1u)];
    return [NSString stringWithFormat:@"%@   MIDI %d",
        [NSString stringWithUTF8String:segment.alias.c_str()], segment.baseMidi];
}

- (float)selectedSeedFrequency
{
    if (![self hasSeedSource]) return 150.0f;
    const int midi = _segments[std::min(_selectedSegment, _segments.size() - 1u)].baseMidi;
    return std::clamp(440.0f * std::pow(2.0f,
        (static_cast<float>(midi) - 69.0f) / 12.0f), 55.0f, 520.0f);
}

- (void)showGenerator
{
    constexpr CGFloat width = 520.0;
    constexpr CGFloat height = 580.0;
    if (!_generatorWindow) {
        const NSRect content = NSMakeRect(0, 0, width, height);
        _generatorWindow = [[NSPanel alloc] initWithContentRect:content
            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
            backing:NSBackingStoreBuffered defer:NO];
        [_generatorWindow setTitle:@"s3g Vox Source Generator"];
        [_generatorWindow setReleasedWhenClosed:NO];
        [_generatorWindow setFloatingPanel:YES];
        [_generatorWindow setCollectionBehavior:NSWindowCollectionBehaviorMoveToActiveSpace];
        _generatorView = [[S3GVoxGeneratorView alloc] initWithFrame:content builder:self];
        [_generatorWindow setContentView:_generatorView];
    }
    [_generatorView refreshSeedState];
    NSWindow* parent = [self window];
    if (parent) {
        const NSRect parentFrame = [parent frame];
        const NSRect generatorFrame = [_generatorWindow frame];
        [_generatorWindow setFrameOrigin:NSMakePoint(
            NSMidX(parentFrame) - NSWidth(generatorFrame) * 0.5,
            NSMidY(parentFrame) - NSHeight(generatorFrame) * 0.5)];
    } else {
        [_generatorWindow center];
    }
    [_generatorWindow makeKeyAndOrderFront:nil];
}

- (BOOL)beginVoiceGeneration:(s3g::VoxSourceSynthParams)params
                  vocabulary:(s3g::VoxSourceVocabulary)vocabulary
{
    if (_generating.exchange(true, std::memory_order_acq_rel)) {
        [self setStatus:@"VOICEBANK GENERATION IS ALREADY RUNNING"];
        NSBeep();
        return NO;
    }

    std::shared_ptr<const std::vector<float>> seedSamples;
    uint64_t seedStart = 0u;
    uint64_t seedEnd = 0u;
    int seedSampleRate = 0;
    std::string seedAlias;
    float seedPitch = 0.0f;
    if (params.seeded) {
        if (![self hasSeedSource]) {
            _generating.store(false, std::memory_order_release);
            [self setStatus:@"SEEDED MODE NEEDS A SELECTED VOCAL SEGMENT"];
            NSBeep();
            return NO;
        }
        const auto& segment = _segments[std::min(_selectedSegment, _segments.size() - 1u)];
        seedSamples = _samples;
        seedStart = segment.startSample;
        seedEnd = segment.endSample;
        seedSampleRate = _sampleRate;
        seedAlias = segment.alias;
        seedPitch = [self selectedSeedFrequency];
    }

    params.sampleRate = 48000;
    __block s3g::VoxSourceSynthParams generationParams = params;
    const uint64_t generation = _analysisGeneration.fetch_add(
        1u, std::memory_order_acq_rel) + 1u;
    _analyzing.store(false, std::memory_order_release);
    _status = params.seeded ? @"ANALYZING SEED AND SYNTHESIZING"
                            : @"SYNTHESIZING PROCEDURAL VOICEBANK";
    [self setNeedsDisplay:YES];
    __strong S3GVoxBuilderView* strongSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        if (generationParams.seeded) {
            generationParams.seedProfile = s3g::voxSourceAnalyzeSeed(*seedSamples, seedSampleRate,
                seedStart, seedEnd, seedAlias, seedPitch);
            if (!generationParams.seedProfile.valid) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (strongSelf->_analysisGeneration.load(std::memory_order_acquire) != generation) return;
                    strongSelf->_generating.store(false, std::memory_order_release);
                    [strongSelf setStatus:@"SELECTED SEGMENT IS NOT A USABLE VOCAL SEED"];
                    NSBeep();
                });
                return;
            }
        }
        auto bank = std::make_shared<s3g::VoxGeneratedVoicebank>(
            s3g::voxSourceGenerateBank(generationParams, vocabulary));
        dispatch_async(dispatch_get_main_queue(), ^{
            if (strongSelf->_analysisGeneration.load(std::memory_order_acquire) != generation) return;
            strongSelf->_generating.store(false, std::memory_order_release);
            NSString* mode = generationParams.seeded ? @"SEEDED" : @"PROCEDURAL";
            NSString* description = [NSString stringWithFormat:@"%@ / %s",
                mode, s3g::voxSourceVocabularyName(vocabulary)];
            [strongSelf installGeneratedVoicebank:std::move(bank)
                description:description synthParams:generationParams vocabulary:vocabulary];
        });
    });
    return YES;
}

- (void)installGeneratedVoicebank:(std::shared_ptr<s3g::VoxGeneratedVoicebank>)bank
                       description:(NSString*)description
                       synthParams:(s3g::VoxSourceSynthParams)params
                        vocabulary:(s3g::VoxSourceVocabulary)vocabulary
{
    if (!bank || bank->entries.empty() || bank->sampleRate <= 0) {
        [self setStatus:@"VOICEBANK GENERATION FAILED"];
        NSBeep();
        return;
    }
    uint64_t totalSamples = 0u;
    for (const auto& entry : bank->entries) {
        totalSamples += entry.samples.size();
        if (totalSamples > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
            [self setStatus:@"GENERATED VOICEBANK IS TOO LARGE"];
            NSBeep();
            return;
        }
    }

    auto samples = std::make_shared<std::vector<float>>();
    samples->reserve(static_cast<size_t>(totalSamples));
    std::vector<s3g::VoxBuilderSegment> segments;
    segments.reserve(bank->entries.size());
    const int baseMidi = std::clamp(static_cast<int>(std::lround(
        69.0f + 12.0f * std::log2(std::max(1.0f, params.baseFrequencyHz) / 440.0f))), 0, 127);
    for (const auto& entry : bank->entries) {
        std::vector<float> conditioned = entry.samples;
        const auto level = s3g::voxBuilderConditionAudio(conditioned, bank->sampleRate);
        const uint64_t start = samples->size();
        samples->insert(samples->end(), conditioned.begin(), conditioned.end());
        s3g::VoxBuilderSegment segment {};
        segment.alias = entry.alias;
        segment.startSample = start;
        segment.endSample = samples->size();
        segment.baseMidi = baseMidi;
        segment.aliasConfidence = 1.0f;
        segment.aliasAssignment = s3g::VoxBuilderAliasAssignment::Manual;
        segment.normalizationGainDb = level.normalizationGainDb;
        segment.sourcePeakDb = level.sourcePeakDb;
        segment.sourceClippedRatio = level.clippedRatio;
        segment.normalizationLimited = level.boostLimited;
        s3g::voxBuilderSetSuggestedTiming(segment, bank->sampleRate);
        segments.push_back(std::move(segment));
    }

    _samples = std::move(samples);
    _sampleRate = bank->sampleRate;
    _sourceURLs = @[];
    _generatedSourceName = [description copy];
    NSMutableDictionary* generation = [@{
        @"mode":params.seeded ? @"seeded" : @"procedural",
        @"vocabulary":[NSString stringWithUTF8String:s3g::voxSourceVocabularyName(vocabulary)],
        @"sampleRate":@(params.sampleRate),
        @"baseFrequencyHz":@(params.baseFrequencyHz),
        @"tractScale":@(params.tractScale),
        @"breath":@(params.breath),
        @"roughness":@(params.roughness),
        @"articulation":@(params.articulation),
        @"consonantStrength":@(params.consonantStrength),
        @"durationMs":@(params.durationMs),
        @"variation":@(params.variation),
        @"randomSeed":@(params.randomSeed)
    } mutableCopy];
    if (params.seeded && params.seedProfile.valid) {
        generation[@"seedProfile"] = @{
            @"baseFrequencyHz":@(params.seedProfile.baseFrequencyHz),
            @"formant1Scale":@(params.seedProfile.formant1Scale),
            @"formant2Scale":@(params.seedProfile.formant2Scale),
            @"formant3Scale":@(params.seedProfile.formant3Scale),
            @"breath":@(params.seedProfile.breath),
            @"roughness":@(params.seedProfile.roughness),
            @"brightness":@(params.seedProfile.brightness),
            @"periodicity":@(params.seedProfile.periodicity)
        };
    }
    _generationMetadata = [generation copy];
    _multiFileSource = YES;
    _singleSourceLevel = {};
    _segments = std::move(segments);
    _segmentsBeforeGuess.clear();
    _canUndoGuess = NO;
    _selectedSegment = 0u;
    [_nameField setStringValue:[description hasPrefix:@"SEEDED"]
        ? @"s3g_seeded_voice" : @"s3g_procedural_voice"];
    [self syncAliasEditorFromSegments];
    [self rebuildWaveOverview];
    [self refreshSegmentFields];
    [self setStatus:[NSString stringWithFormat:@"GENERATED %zu ALIASES - WORLD ANALYSIS",
        _segments.size()]];
    [self startWorldAnalysis];
    [[self window] invalidateCursorRectsForView:self];
}

- (void)analyzeSource
{
    if (!_samples || _samples->empty() || _sampleRate <= 0) {
        [self setStatus:@"LOAD A VOICE RECORDING FIRST"];
        NSBeep();
        return;
    }
    const char* utf8 = [[_phonemeEditor string] UTF8String];
    const auto aliases = s3g::voxBuilderParseAliases(utf8 ? utf8 : "");
    if (aliases.empty()) {
        [self setStatus:@"ENTER AT LEAST ONE ALIAS"];
        NSBeep();
        return;
    }
    _segmentsBeforeGuess.clear();
    _canUndoGuess = NO;
    if (_multiFileSource) {
        if (aliases.size() != _segments.size()) {
            [self setStatus:@"ONE ALIAS IS REQUIRED PER SEGMENT"];
            NSBeep();
            return;
        }
        _aliasesDirty = NO;
        for (size_t i = 0u; i < aliases.size(); ++i) {
            if (_segments[i].alias != aliases[i]) {
                _segments[i].aliasConfidence = 1.0f;
                _segments[i].aliasAssignment = s3g::VoxBuilderAliasAssignment::Manual;
            }
            _segments[i].alias = aliases[i];
        }
        [self refreshSegmentFields];
        [self startWorldAnalysis];
        return;
    }
    _aliasesDirty = NO;
    _segments = s3g::voxBuilderDetectSegments(*_samples, _sampleRate, aliases, _settings);
    for (auto& segment : _segments) {
        segment.normalizationGainDb = _singleSourceLevel.normalizationGainDb;
        segment.sourcePeakDb = _singleSourceLevel.sourcePeakDb;
        segment.sourceClippedRatio = _singleSourceLevel.clippedRatio;
        segment.normalizationLimited = _singleSourceLevel.boostLimited;
    }
    _selectedSegment = 0u;
    [self refreshSegmentFields];
    [self startWorldAnalysis];
    [[self window] invalidateCursorRectsForView:self];
}

- (void)autoGuessAliases
{
    if (_canUndoGuess) {
        if (_segmentsBeforeGuess.size() != _segments.size()) {
            _segmentsBeforeGuess.clear();
            _canUndoGuess = NO;
            [self setStatus:@"GUESS HISTORY IS NO LONGER AVAILABLE"];
            NSBeep();
            return;
        }
        _segments = std::move(_segmentsBeforeGuess);
        _segmentsBeforeGuess.clear();
        _canUndoGuess = NO;
        [self syncAliasEditorFromSegments];
        [self refreshSegmentFields];
        [self setStatus:@"ACOUSTIC ALIAS GUESSES UNDONE"];
        return;
    }
    if (_analyzing.load(std::memory_order_relaxed)) {
        [self setStatus:@"WAIT FOR ANALYSIS TO FINISH"];
        NSBeep();
        return;
    }
    if (_aliasesDirty) {
        [self setStatus:@"RUN ANALYZE TO APPLY EDITED ALIASES"];
        NSBeep();
        return;
    }
    if (_segments.empty()) {
        [self setStatus:@"ANALYZE A SOURCE FIRST"];
        NSBeep();
        return;
    }
    _segmentsBeforeGuess = _segments;
    size_t guessed = 0u;
    size_t lowConfidence = 0u;
    size_t unmatched = 0u;
    std::vector<std::string> reservedAliases;
    std::vector<size_t> pendingIndices;
    std::vector<std::vector<s3g::VoxBuilderAliasGuess>> rankedCandidates;
    pendingIndices.reserve(_segments.size());
    rankedCandidates.reserve(_segments.size());
    for (size_t i = 0u; i < _segments.size(); ++i) {
        const auto& segment = _segments[i];
        const bool keep = segment.aliasAssignment == s3g::VoxBuilderAliasAssignment::Manual
            || segment.aliasAssignment == s3g::VoxBuilderAliasAssignment::Filename
            || segment.aliasConfidence >= 0.90f;
        if (keep) {
            reservedAliases.push_back(segment.alias);
            continue;
        }
        auto candidates = s3g::voxBuilderRankCoreAliases(segment.acoustic);
        if (candidates.empty()) continue;
        pendingIndices.push_back(i);
        rankedCandidates.push_back(std::move(candidates));
    }
    const auto matches = s3g::voxBuilderChooseUniqueAliases(rankedCandidates, reservedAliases);
    for (size_t i = 0u; i < matches.size(); ++i) {
        const auto& guess = matches[i];
        if (guess.alias.empty()) {
            ++unmatched;
            continue;
        }
        auto& segment = _segments[pendingIndices[i]];
        segment.alias = guess.alias;
        segment.aliasConfidence = guess.confidence;
        segment.aliasAssignment = s3g::VoxBuilderAliasAssignment::Acoustic;
        s3g::voxBuilderSetSuggestedTiming(segment, _sampleRate);
        ++guessed;
        if (guess.confidence < 0.70f) ++lowConfidence;
    }
    if (guessed == 0u) {
        _segmentsBeforeGuess.clear();
        [self setStatus:@"NO UNCONFIRMED ALIASES TO GUESS"];
        NSBeep();
        return;
    }
    _canUndoGuess = YES;
    [self syncAliasEditorFromSegments];
    [self refreshSegmentFields];
    _status = unmatched > 0u
        ? [NSString stringWithFormat:@"GUESSED %zu UNIQUE   %zu UNMATCHED", guessed, unmatched]
        : [NSString stringWithFormat:@"GUESSED %zu UNIQUE   REVIEW %zu", guessed, lowConfidence];
    [self setNeedsDisplay:YES];
}

- (void)startWorldAnalysis
{
    if (!_samples || _samples->empty() || _segments.empty()) return;
    const uint64_t generation = _analysisGeneration.fetch_add(1u, std::memory_order_acq_rel) + 1u;
    _analysisProgress.store(0u, std::memory_order_release);
    _analyzing.store(true, std::memory_order_release);
    _status = @"WORLD ANALYSIS";
    const auto source = _samples;
    const int sampleRate = _sampleRate;
    auto analyzed = std::make_shared<std::vector<s3g::VoxBuilderSegment>>(_segments);
    __strong S3GVoxBuilderView* strongSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        for (size_t i = 0u; i < analyzed->size(); ++i) {
            if (strongSelf->_analysisGeneration.load(std::memory_order_acquire) != generation) return;
            const WorldMetrics metrics = analyzeWorldPitch(*source, sampleRate,
                (*analyzed)[i].startSample, (*analyzed)[i].endSample);
            (*analyzed)[i].baseMidi = metrics.baseMidi;
            (*analyzed)[i].voicedRatio = metrics.voicedRatio;
            (*analyzed)[i].acoustic = s3g::voxBuilderAnalyzeAcoustics(*source, sampleRate,
                (*analyzed)[i].startSample, (*analyzed)[i].endSample);
            strongSelf->_analysisProgress.store(static_cast<uint32_t>(i + 1u), std::memory_order_release);
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            if (strongSelf->_analysisGeneration.load(std::memory_order_acquire) != generation) return;
            strongSelf->_segments = std::move(*analyzed);
            strongSelf->_analyzing.store(false, std::memory_order_release);
            bool clipped = false;
            bool limited = false;
            size_t uncertain = 0u;
            size_t unvoiced = 0u;
            for (const auto& segment : strongSelf->_segments) {
                clipped = clipped || segment.sourceClippedRatio > 0.001f;
                limited = limited || segment.normalizationLimited;
                if (segment.aliasConfidence < 0.70f) ++uncertain;
                if (s3g::voxBuilderVowelAlias(segment.alias) && segment.voicedRatio < 0.10f) {
                    ++unvoiced;
                }
            }
            if (clipped) strongSelf->_status = @"READY - SOURCE CLIPPING";
            else if (limited) strongSelf->_status = @"READY - VERY LOW INPUT";
            else if (uncertain > 0u) strongSelf->_status = [NSString stringWithFormat:
                @"READY - REVIEW %zu ALIAS GUESSES", uncertain];
            else if (unvoiced > 0u) strongSelf->_status = [NSString stringWithFormat:
                @"READY - REVIEW %zu UNVOICED VOWELS", unvoiced];
            else strongSelf->_status = @"READY TO AUDITION OR EXPORT";
            [strongSelf refreshSegmentFields];
            [strongSelf setNeedsDisplay:YES];
        });
    });
    [self setNeedsDisplay:YES];
}

- (void)refreshSegmentFields
{
    _updatingFields = YES;
    if (_segments.empty()) {
        for (NSTextField* field in @[ _aliasField, _rootField, _fixedField, _preutterField, _overlapField ]) {
            [field setStringValue:@""];
        }
    } else {
        _selectedSegment = std::min(_selectedSegment, _segments.size() - 1u);
        const auto& segment = _segments[_selectedSegment];
        [_aliasField setStringValue:[NSString stringWithUTF8String:segment.alias.c_str()]];
        [_rootField setIntegerValue:segment.baseMidi];
        [_fixedField setStringValue:[NSString stringWithFormat:@"%.1f", segment.fixedMs]];
        [_preutterField setStringValue:[NSString stringWithFormat:@"%.1f", segment.preutterMs]];
        [_overlapField setStringValue:[NSString stringWithFormat:@"%.1f", segment.overlapMs]];
    }
    _updatingFields = NO;
}

- (void)syncAliasEditorFromSegments
{
    NSMutableArray<NSString*>* aliases = [NSMutableArray arrayWithCapacity:_segments.size()];
    for (const auto& segment : _segments) {
        [aliases addObject:[NSString stringWithUTF8String:segment.alias.c_str()]];
    }
    [_phonemeEditor setString:[aliases componentsJoinedByString:@"\n"]];
    _aliasesDirty = NO;
}

- (void)addSegment
{
    if (_segments.empty()) {
        NSBeep();
        return;
    }
    _selectedSegment = std::min(_selectedSegment, _segments.size() - 1u);
    auto& selected = _segments[_selectedSegment];
    const uint64_t minimumSpan = std::max<uint64_t>(1u,
        static_cast<uint64_t>(std::max(1, _sampleRate)) / 50u);
    if (selected.endSample <= selected.startSample + minimumSpan * 2u) {
        [self setStatus:@"SEGMENT IS TOO SHORT TO SPLIT"];
        NSBeep();
        return;
    }
    const uint64_t splitTarget = selected.startSample
        + (selected.endSample - selected.startSample) / 2u;
    const uint64_t split = _samples ? s3g::voxBuilderSnapToZeroCrossing(
        *_samples, splitTarget, std::max<uint64_t>(1u, static_cast<uint64_t>(_sampleRate) / 100u),
        selected.startSample + minimumSpan, selected.endSample - minimumSpan) : splitTarget;
    s3g::VoxBuilderSegment right = selected;
    selected.endSample = split;
    right.startSample = split;
    right.alias = "voice_" + std::to_string(_segments.size() + 1u);
    right.aliasConfidence = 0.0f;
    right.aliasAssignment = s3g::VoxBuilderAliasAssignment::Unknown;
    s3g::voxBuilderSetSuggestedTiming(selected, _sampleRate);
    s3g::voxBuilderSetSuggestedTiming(right, _sampleRate);
    _segments.insert(_segments.begin() + static_cast<std::ptrdiff_t>(_selectedSegment + 1u),
        std::move(right));
    _segmentsBeforeGuess.clear();
    _canUndoGuess = NO;
    ++_selectedSegment;
    [self syncAliasEditorFromSegments];
    [self refreshSegmentFields];
    [self setStatus:@"SEGMENT ADDED - EDIT ITS ALIAS"];
    [self startWorldAnalysis];
    [[self window] invalidateCursorRectsForView:self];
}

- (void)removeSegment
{
    if (_segments.size() <= 1u) {
        [self setStatus:@"AT LEAST ONE SEGMENT IS REQUIRED"];
        NSBeep();
        return;
    }
    _segmentsBeforeGuess.clear();
    _canUndoGuess = NO;
    _selectedSegment = std::min(_selectedSegment, _segments.size() - 1u);
    if (_selectedSegment == 0u) {
        _segments[1u].startSample = _segments[0u].startSample;
        s3g::voxBuilderSetSuggestedTiming(_segments[1u], _sampleRate);
        _segments.erase(_segments.begin());
        _selectedSegment = 0u;
    } else {
        const size_t previous = _selectedSegment - 1u;
        _segments[previous].endSample = _segments[_selectedSegment].endSample;
        s3g::voxBuilderSetSuggestedTiming(_segments[previous], _sampleRate);
        _segments.erase(_segments.begin() + static_cast<std::ptrdiff_t>(_selectedSegment));
        _selectedSegment = previous;
    }
    [self syncAliasEditorFromSegments];
    [self refreshSegmentFields];
    [self setStatus:@"SEGMENT REMOVED"];
    [self startWorldAnalysis];
    [[self window] invalidateCursorRectsForView:self];
}

- (void)controlTextDidBeginEditing:(NSNotification*)notification
{
    id object = [notification object];
    if ([object isKindOfClass:[NSTextField class]]) {
        s3g::clap_gui::styleNumberTextEditor(static_cast<NSTextField*>(object));
    }
}

- (void)controlTextDidEndEditing:(NSNotification*)notification
{
    if (_updatingFields) return;
    NSTextField* field = [notification object];
    if (field == _nameField) {
        if ([[_nameField stringValue] length] == 0u) [_nameField setStringValue:@"s3g_voicebank"];
        [self setStatus:@"BANK NAME UPDATED"];
        return;
    }
    if (_segments.empty()) return;
    _segmentsBeforeGuess.clear();
    _canUndoGuess = NO;
    auto& segment = _segments[_selectedSegment];
    if (field == _aliasField) {
        NSString* alias = [[field stringValue] stringByTrimmingCharactersInSet:
            [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if ([alias length] == 0u) alias = @"voice";
        segment.alias = [alias UTF8String] ?: "voice";
        segment.aliasConfidence = 1.0f;
        segment.aliasAssignment = s3g::VoxBuilderAliasAssignment::Manual;
        [self syncAliasEditorFromSegments];
    } else if (field == _rootField) {
        segment.baseMidi = std::clamp(static_cast<int>([field integerValue]), 0, 127);
    } else {
        const float durationMs = static_cast<float>(segment.endSample - segment.startSample)
            * 1000.0f / static_cast<float>(std::max(1, _sampleRate));
        const float value = std::clamp([field floatValue], 0.0f, durationMs);
        if (field == _fixedField) segment.fixedMs = value;
        else if (field == _preutterField) segment.preutterMs = value;
        else if (field == _overlapField) segment.overlapMs = value;
    }
    [self refreshSegmentFields];
    [self setStatus:@"SEGMENT EDITED"];
}

- (void)textDidChange:(NSNotification*)notification
{
    if ([notification object] != _phonemeEditor) return;
    _aliasesDirty = YES;
    _segmentsBeforeGuess.clear();
    _canUndoGuess = NO;
    [self setStatus:@"ALIASES CHANGED - RUN ANALYZE"];
}

- (void)auditionSelected
{
    if (!_samples || _segments.empty()) {
        NSBeep();
        return;
    }
    const auto& segment = _segments[_selectedSegment];
    NSData* data = makeWavData(*_samples, segment.startSample, segment.endSample, _sampleRate);
    if (!data) {
        NSBeep();
        return;
    }
    NSError* error = nil;
    _previewPlayer = [[AVAudioPlayer alloc] initWithData:data error:&error];
    if (!_previewPlayer || error) {
        [self setStatus:@"AUDITION FAILED"];
        NSBeep();
        return;
    }
    [_previewPlayer prepareToPlay];
    [_previewPlayer play];
    [self setStatus:[NSString stringWithFormat:@"AUDITION  %02zu  %@",
        _selectedSegment + 1u, [NSString stringWithUTF8String:segment.alias.c_str()]]];
}

- (void)exportVoicebank
{
    if (_generating.load(std::memory_order_acquire)) {
        [self setStatus:@"WAIT FOR VOICEBANK GENERATION TO FINISH"];
        NSBeep();
        return;
    }
    if (_analyzing.load(std::memory_order_acquire)) {
        [self setStatus:@"WAIT FOR WORLD ANALYSIS TO FINISH"];
        NSBeep();
        return;
    }
    if (_aliasesDirty) {
        [self setStatus:@"RUN ANALYZE TO APPLY EDITED ALIASES"];
        NSBeep();
        return;
    }
    if (!_samples || _segments.empty() || _sampleRate <= 0) {
        [self setStatus:@"ANALYZE A SOURCE BEFORE EXPORT"];
        NSBeep();
        return;
    }
    NSString* bankName = [[_nameField stringValue] stringByTrimmingCharactersInSet:
        [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if ([bankName length] == 0u) bankName = @"s3g_voicebank";
    NSSavePanel* panel = [NSSavePanel savePanel];
    [panel setCanCreateDirectories:YES];
    [panel setNameFieldStringValue:bankName];
    [panel setPrompt:@"EXPORT"];
    if ([panel runModal] != NSModalResponseOK) return;
    NSURL* folder = [panel URL];
    NSError* error = nil;
    if (![[NSFileManager defaultManager] createDirectoryAtURL:folder
        withIntermediateDirectories:YES attributes:nil error:&error]) {
        [self setStatus:@"COULD NOT CREATE VOICEBANK FOLDER"];
        NSBeep();
        return;
    }

    NSMutableString* oto = [NSMutableString string];
    NSMutableString* markers = [NSMutableString stringWithString:@"alias,start_ms,end_ms\n"];
    NSMutableArray* entries = [NSMutableArray arrayWithCapacity:_segments.size()];
    std::unordered_map<std::string, uint32_t> stems;
    bool ok = true;
    for (size_t i = 0u; i < _segments.size(); ++i) {
        const auto& segment = _segments[i];
        std::string stem = s3g::voxBuilderSafeStem(segment.alias);
        const uint32_t duplicate = ++stems[stem];
        if (duplicate > 1u) stem += "_" + std::to_string(duplicate);
        NSString* fileName = [NSString stringWithFormat:@"%s.wav", stem.c_str()];
        NSData* wav = makeWavData(*_samples, segment.startSample, segment.endSample, _sampleRate);
        if (!wav || ![wav writeToURL:[folder URLByAppendingPathComponent:fileName]
            options:NSDataWritingAtomic error:&error]) {
            ok = false;
            break;
        }
        NSString* alias = cleanOtoAlias([NSString stringWithUTF8String:segment.alias.c_str()]);
        [oto appendFormat:@"%@=%@,0.000,%.3f,0.000,%.3f,%.3f\n",
            fileName, alias, segment.fixedMs, segment.preutterMs, segment.overlapMs];
        const double startMs = static_cast<double>(segment.startSample) * 1000.0
            / static_cast<double>(_sampleRate);
        const double endMs = static_cast<double>(segment.endSample) * 1000.0
            / static_cast<double>(_sampleRate);
        [markers appendFormat:@"%@,%.3f,%.3f\n", alias, startMs, endMs];
        [entries addObject:@{
            @"alias":alias,
            @"file":fileName,
            @"baseMidi":@(segment.baseMidi),
            @"voicedRatio":@(segment.voicedRatio),
            @"aliasConfidence":@(segment.aliasConfidence),
            @"aliasAssignment":[NSString stringWithUTF8String:
                s3g::voxBuilderAliasAssignmentName(segment.aliasAssignment)],
            @"normalizationGainDb":@(segment.normalizationGainDb),
            @"sourcePeakDb":@(segment.sourcePeakDb),
            @"sourceClippedRatio":@(segment.sourceClippedRatio),
            @"fixedMs":@(segment.fixedMs),
            @"preutterMs":@(segment.preutterMs),
            @"overlapMs":@(segment.overlapMs)
        }];
    }
    if (ok) {
        NSString* character = [NSString stringWithFormat:@"name=%@\n", bankName];
        ok = [oto writeToURL:[folder URLByAppendingPathComponent:@"oto.ini"]
            atomically:YES encoding:NSUTF8StringEncoding error:&error]
            && [character writeToURL:[folder URLByAppendingPathComponent:@"character.txt"]
                atomically:YES encoding:NSUTF8StringEncoding error:&error]
            && [[_phonemeEditor string] writeToURL:[folder URLByAppendingPathComponent:@"phonemes.txt"]
                atomically:YES encoding:NSUTF8StringEncoding error:&error]
            && [markers writeToURL:[folder URLByAppendingPathComponent:@"markers.csv"]
                atomically:YES encoding:NSUTF8StringEncoding error:&error];
    }
    if (ok) {
        NSMutableArray<NSString*>* sources = [NSMutableArray arrayWithCapacity:[_sourceURLs count]];
        for (NSURL* sourceURL in _sourceURLs) [sources addObject:[sourceURL path]];
        NSDictionary* metadata = @{
            @"format":@"s3g-voicebank-v2",
            @"builder":@"s3g Vox Builder",
            @"name":bankName,
            @"sampleRate":@(_sampleRate),
            @"source":[sources count] > 0u ? [sources firstObject] : @"",
            @"sources":sources,
            @"entries":entries,
            @"generation":_generationMetadata ?: @{}
        };
        NSData* json = [NSJSONSerialization dataWithJSONObject:metadata
            options:NSJSONWritingPrettyPrinted error:&error];
        ok = json && [json writeToURL:[folder URLByAppendingPathComponent:@"voicebank.json"]
            options:NSDataWritingAtomic error:&error];
    }
    if (!ok) {
        [self setStatus:@"EXPORT FAILED"];
        NSBeep();
        return;
    }
    [self setStatus:[NSString stringWithFormat:@"EXPORTED  %zu SEGMENTS", _segments.size()]];
}

- (CGFloat)xForSample:(uint64_t)sample
{
    const NSRect rect = [self waveformRect];
    const uint64_t length = _samples ? _samples->size() : 0u;
    if (length == 0u) return rect.origin.x;
    return rect.origin.x + rect.size.width * static_cast<CGFloat>(sample)
        / static_cast<CGFloat>(length);
}

- (uint64_t)sampleForX:(CGFloat)x
{
    const NSRect rect = [self waveformRect];
    if (!_samples || _samples->empty()) return 0u;
    const CGFloat norm = std::clamp((x - rect.origin.x) / rect.size.width, 0.0, 1.0);
    return static_cast<uint64_t>(std::llround(norm * static_cast<CGFloat>(_samples->size())));
}

- (NSInteger)boundaryNearPoint:(NSPoint)point
{
    if (!NSPointInRect(point, [self waveformRect])) return -1;
    for (size_t i = 1u; i < _segments.size(); ++i) {
        if (std::fabs(point.x - [self xForSample:_segments[i].startSample]) <= 7.0) {
            return static_cast<NSInteger>(i);
        }
    }
    return -1;
}

- (void)updateSlider:(NSPoint)point
{
    const CGFloat norm = std::clamp(
        (point.x - kControlTrackX) / kControlTrackWidth, 0.0, 1.0);
    if (_dragSlider == 1) _settings.thresholdDb = -60.0f + static_cast<float>(norm) * 48.0f;
    else if (_dragSlider == 2) _settings.minimumGapMs = 20.0f + static_cast<float>(norm) * 230.0f;
    else if (_dragSlider == 3) _settings.edgePaddingMs = static_cast<float>(norm) * 80.0f;
    _segmentsBeforeGuess.clear();
    _canUndoGuess = NO;
    _status = @"ANALYSIS SETTINGS CHANGED";
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (NSPointInRect(point, [self loadButtonRect])) { [self openSource]; return; }
    if (NSPointInRect(point, [self generateButtonRect])) { [self showGenerator]; return; }
    if (NSPointInRect(point, [self analyzeButtonRect])) { [self analyzeSource]; return; }
    if (NSPointInRect(point, [self autoGuessButtonRect])) { [self autoGuessAliases]; return; }
    if (NSPointInRect(point, [self aliasGuideButtonRect])) { [self showAliasGuide]; return; }
    if (NSPointInRect(point, [self removeSegmentButtonRect])) { [self removeSegment]; return; }
    if (NSPointInRect(point, [self addSegmentButtonRect])) { [self addSegment]; return; }
    if (NSPointInRect(point, [self auditionButtonRect])) { [self auditionSelected]; return; }
    if (NSPointInRect(point, [self exportButtonRect])) { [self exportVoicebank]; return; }

    if (!_multiFileSource) {
        const CGFloat sliderRows[] = { 246.0, 276.0, 306.0 };
        for (NSInteger i = 0; i < 3; ++i) {
            if (NSPointInRect(point, NSMakeRect(kControlTrackX - 8.0,
                sliderRows[i] - 8.0, kControlTrackWidth + 16.0, 25.0))) {
                _dragSlider = i + 1;
                [self updateSlider:point];
                return;
            }
        }
    }
    if (!NSPointInRect(point, [self waveformRect]) || _segments.empty()) return;
    _dragBoundary = [self boundaryNearPoint:point];
    if (_dragBoundary >= 0) return;
    const uint64_t sample = [self sampleForX:point.x];
    for (size_t i = 0u; i < _segments.size(); ++i) {
        if (sample >= _segments[i].startSample && sample < _segments[i].endSample) {
            _selectedSegment = i;
            [self refreshSegmentFields];
            [self setNeedsDisplay:YES];
            if ([event clickCount] >= 2) [self auditionSelected];
            return;
        }
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragSlider > 0) {
        [self updateSlider:point];
        return;
    }
    if (_dragBoundary <= 0 || static_cast<size_t>(_dragBoundary) >= _segments.size()) return;
    const uint64_t minimumSpan = std::max<uint64_t>(1u,
        static_cast<uint64_t>(std::max(1, _sampleRate)) / 50u);
    const size_t boundary = static_cast<size_t>(_dragBoundary);
    const uint64_t target = [self sampleForX:point.x];
    const uint64_t upper = _segments[boundary].endSample > minimumSpan
        ? _segments[boundary].endSample - minimumSpan
        : _segments[boundary - 1u].startSample + minimumSpan;
    const uint64_t snapped = _samples ? s3g::voxBuilderSnapToZeroCrossing(
        *_samples, target, std::max<uint64_t>(1u, static_cast<uint64_t>(_sampleRate) / 100u),
        _segments[boundary - 1u].startSample + minimumSpan,
        upper) : target;
    if (s3g::voxBuilderMoveBoundary(_segments, boundary, snapped, minimumSpan)) {
        _segmentsBeforeGuess.clear();
        _canUndoGuess = NO;
        s3g::voxBuilderSetSuggestedTiming(_segments[static_cast<size_t>(_dragBoundary) - 1u], _sampleRate);
        s3g::voxBuilderSetSuggestedTiming(_segments[static_cast<size_t>(_dragBoundary)], _sampleRate);
        [self refreshSegmentFields];
        _status = @"BOUNDARY EDITED";
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    const bool movedBoundary = _dragBoundary > 0;
    _dragBoundary = -1;
    _dragSlider = 0;
    if (movedBoundary) [self startWorldAnalysis];
}

- (void)resetCursorRects
{
    [super resetCursorRects];
    for (size_t i = 1u; i < _segments.size(); ++i) {
        const CGFloat x = [self xForSample:_segments[i].startSample];
        [self addCursorRect:NSMakeRect(x - 5.0, [self waveformRect].origin.y,
            10.0, [self waveformRect].size.height) cursor:[NSCursor resizeLeftRightCursor]];
    }
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
    return [sender draggingSourceOperationMask] & NSDragOperationCopy
        ? NSDragOperationCopy : NSDragOperationNone;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
    NSArray<NSURL*>* urls = [[sender draggingPasteboard]
        readObjectsForClasses:@[ [NSURL class] ]
        options:@{ NSPasteboardURLReadingFileURLsOnlyKey:@YES }];
    return [self loadAudioURLs:urls];
}

- (void)drawWaveform:(NSDictionary*)labelAttrs valueAttrs:(NSDictionary*)valueAttrs
{
    const NSRect rect = [self waveformRect];
    [color(0x0b0b0b) setFill];
    NSRectFill(rect);
    [color(0x575757) setStroke];
    NSFrameRect(rect);
    [@"WAVEFORM / SEGMENTS" drawAtPoint:NSMakePoint(rect.origin.x + 8, rect.origin.y + 7)
        withAttributes:labelAttrs];
    if (!_samples || _samples->empty()) {
        [@"DROP WAV FILES OR A FOLDER" drawAtPoint:NSMakePoint(rect.origin.x + 218, rect.origin.y + 188)
            withAttributes:valueAttrs];
        return;
    }
    const NSRect graph = NSMakeRect(rect.origin.x + 8, rect.origin.y + 28,
        rect.size.width - 16, rect.size.height - 38);
    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRect:graph] addClip];
    for (size_t i = 0u; i < _segments.size(); ++i) {
        NSRect area = NSMakeRect([self xForSample:_segments[i].startSample], graph.origin.y,
            [self xForSample:_segments[i].endSample] - [self xForSample:_segments[i].startSample],
            graph.size.height);
        [color(i == _selectedSegment ? 0x292929 : ((i & 1u) ? 0x151515 : 0x111111)) setFill];
        NSRectFill(area);
    }
    [color(0x303030) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(graph.origin.x, NSMidY(graph))
        toPoint:NSMakePoint(NSMaxX(graph), NSMidY(graph))];
    if (!_waveMinimum.empty() && _waveMinimum.size() == _waveMaximum.size()) {
        NSBezierPath* path = [NSBezierPath bezierPath];
        [path setLineWidth:1.0];
        for (size_t bin = 0u; bin < _waveMinimum.size(); ++bin) {
            const CGFloat x = graph.origin.x + graph.size.width * static_cast<CGFloat>(bin)
                / static_cast<CGFloat>(_waveMinimum.size() - 1u);
            const CGFloat y0 = NSMidY(graph) - _waveMaximum[bin] * graph.size.height * 0.43;
            const CGFloat y1 = NSMidY(graph) - _waveMinimum[bin] * graph.size.height * 0.43;
            [path moveToPoint:NSMakePoint(x, y0)];
            [path lineToPoint:NSMakePoint(x, y1)];
        }
        [color(0xa0a0a0) setStroke];
        [path stroke];
    }
    for (size_t i = 0u; i < _segments.size(); ++i) {
        const CGFloat x = [self xForSample:_segments[i].startSample];
        [color(i == _selectedSegment ? 0xd0d0d0 : 0x686868) setStroke];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(x, graph.origin.y)
            toPoint:NSMakePoint(x, NSMaxY(graph))];
        const CGFloat width = [self xForSample:_segments[i].endSample] - x;
        NSString* alias = [NSString stringWithUTF8String:_segments[i].alias.c_str()];
        NSString* label = width >= 42.0 ? alias
            : [NSString stringWithFormat:@"%zu", i + 1u];
        [label drawInRect:NSMakeRect(x + 4, graph.origin.y + 4,
            std::max<CGFloat>(10.0, width - 8.0), 15.0) withAttributes:valueAttrs];
    }
    if (!_segments.empty()) {
        const CGFloat endX = [self xForSample:_segments.back().endSample];
        [color(0x686868) setStroke];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(endX, graph.origin.y)
            toPoint:NSMakePoint(endX, NSMaxY(graph))];
    }
    [NSGraphicsContext restoreGraphicsState];
    NSString* source = _generatedSourceName ?: ([_sourceURLs count] > 1u
        ? [NSString stringWithFormat:@"%lu WAV FILES", static_cast<unsigned long>([_sourceURLs count])]
        : [[_sourceURLs firstObject] lastPathComponent] ?: @"");
    NSString* detail = [NSString stringWithFormat:@"%@   %d HZ   %.2F S",
        source, _sampleRate, static_cast<double>(_samples->size()) / std::max(1, _sampleRate)];
    [detail drawAtPoint:NSMakePoint(NSMaxX(rect) - [detail sizeWithAttributes:valueAttrs].width - 8,
        rect.origin.y + 7) withAttributes:valueAttrs];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    const auto style = s3g::clap_gui::softTextStyle();
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    NSDictionary* labelAttrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    [style.bg setFill];
    NSRectFill([self bounds]);
    [@"s3g VOX BUILDER" drawAtPoint:NSMakePoint(18, 14) withAttributes:titleAttrs];
    NSString* topStatus = _generating.load(std::memory_order_relaxed)
        ? @"SYNTHESIZING VOICEBANK"
        : (_analyzing.load(std::memory_order_relaxed)
            ? [NSString stringWithFormat:@"WORLD  %u / %zu",
            _analysisProgress.load(std::memory_order_relaxed), _segments.size()]
            : @"WORLD VOICEBANK AUTHORING");
    s3g::clap_gui::drawRightStatus(topStatus, kGuiWidth, 14, valueAttrs);

    s3g::clap_gui::drawPanelFrame(18, 42, 770, 720, style);
    s3g::clap_gui::drawPanelHeader(@"VOICE SOURCE", true, 18, 42, 770, 21, labelAttrs, style);
    [self drawWaveform:labelAttrs valueAttrs:valueAttrs];
    [@"PHONEME / ALIAS ORDER" drawAtPoint:NSMakePoint(30, 492) withAttributes:labelAttrs];
    drawActionButton([self aliasGuideButtonRect], @"ALIAS GUIDE", labelAttrs);

    s3g::clap_gui::drawPanelFrame(kRightX, 42, kRightWidth, 150, style);
    s3g::clap_gui::drawPanelHeader(@"SOURCE", true, kRightX, 42, kRightWidth, 21, labelAttrs, style);
    [@"BANK" drawAtPoint:NSMakePoint(820, 80) withAttributes:labelAttrs];
    drawActionButton([self loadButtonRect], @"LOAD", labelAttrs,
        !_generating.load(std::memory_order_relaxed));
    drawActionButton([self generateButtonRect], @"GENERATE", labelAttrs,
        !_generating.load(std::memory_order_relaxed));
    NSString* sourceName = _generatedSourceName ?: ([_sourceURLs count] > 1u
        ? [NSString stringWithFormat:@"%lu WAV FILES", static_cast<unsigned long>([_sourceURLs count])]
        : [[_sourceURLs firstObject] lastPathComponent] ?: @"NO SOURCE");
    NSString* sourceStatus = sourceName;
    if (!_segments.empty()) {
        float minimumGain = std::numeric_limits<float>::max();
        float maximumGain = std::numeric_limits<float>::lowest();
        bool clipped = false;
        for (const auto& segment : _segments) {
            minimumGain = std::min(minimumGain, segment.normalizationGainDb);
            maximumGain = std::max(maximumGain, segment.normalizationGainDb);
            clipped = clipped || segment.sourceClippedRatio > 0.001f;
        }
        NSString* level = std::fabs(maximumGain - minimumGain) < 0.1f
            ? [NSString stringWithFormat:@"NORM %+.1f DB", maximumGain]
            : [NSString stringWithFormat:@"NORM %+.1f TO %+.1f DB", minimumGain, maximumGain];
        if (clipped) level = [level stringByAppendingString:@"  CLIP"];
        sourceStatus = [NSString stringWithFormat:@"%@\n%@", sourceName, level];
    }
    [sourceStatus drawInRect:NSMakeRect(820, 146, 244, 34) withAttributes:valueAttrs];

    s3g::clap_gui::drawPanelFrame(kRightX, 204, kRightWidth, 188, style);
    s3g::clap_gui::drawPanelHeader(@"ANALYSIS", true, kRightX, 204, kRightWidth, 21, labelAttrs, style);
    if (_multiFileSource) {
        NSString* fileMode = _generatedSourceName ? @"GENERATED / ALIAS"
            : (_segments.size() == [_sourceURLs count]
                ? @"FILE / ALIAS" : @"FILES + EDITS");
        [@"MODE" drawAtPoint:NSMakePoint(820, 246) withAttributes:labelAttrs];
        [fileMode drawAtPoint:NSMakePoint(914, 246) withAttributes:valueAttrs];
        [@"ORDER" drawAtPoint:NSMakePoint(820, 276) withAttributes:labelAttrs];
        [(_generatedSourceName ? @"SYNTH" : @"FILENAME")
            drawAtPoint:NSMakePoint(914, 276) withAttributes:valueAttrs];
        [@"RATE" drawAtPoint:NSMakePoint(820, 306) withAttributes:labelAttrs];
        [[NSString stringWithFormat:@"%d HZ", _sampleRate]
            drawAtPoint:NSMakePoint(914, 306) withAttributes:valueAttrs];
    } else {
        s3g::clap_gui::drawSlider(@"THRESH",
            [NSString stringWithFormat:@"%.0f DB", _settings.thresholdDb],
            (_settings.thresholdDb + 60.0f) / 48.0f, 246, labelAttrs, valueAttrs, style,
            820, kControlTrackX, 1028, kControlTrackWidth);
        s3g::clap_gui::drawSlider(@"MIN GAP",
            [NSString stringWithFormat:@"%.0f MS", _settings.minimumGapMs],
            (_settings.minimumGapMs - 20.0f) / 230.0f, 276, labelAttrs, valueAttrs, style,
            820, kControlTrackX, 1028, kControlTrackWidth);
        s3g::clap_gui::drawSlider(@"PAD",
            [NSString stringWithFormat:@"%.0f MS", _settings.edgePaddingMs],
            _settings.edgePaddingMs / 80.0f, 306, labelAttrs, valueAttrs, style,
            820, kControlTrackX, 1028, kControlTrackWidth);
    }
    drawActionButton([self analyzeButtonRect], @"ANALYZE", labelAttrs,
        _samples && !_samples->empty(),
        _aliasesDirty ? ActionButtonTone::Required : ActionButtonTone::Neutral);
    drawActionButton([self autoGuessButtonRect], _canUndoGuess ? @"UNDO GUESS" : @"AUTO GUESS",
        labelAttrs, !_segments.empty() && !_analyzing.load(std::memory_order_relaxed));
    size_t uncertain = 0u;
    for (const auto& segment : _segments) {
        if (segment.aliasConfidence < 0.70f) ++uncertain;
    }
    NSString* analysisStatus = _analyzing.load(std::memory_order_relaxed)
        ? [NSString stringWithFormat:@"WORLD PITCH  %u / %zu",
            _analysisProgress.load(std::memory_order_relaxed), _segments.size()]
        : (uncertain > 0u
            ? [NSString stringWithFormat:@"%zu SEGMENTS   %zu GUESSED", _segments.size(), uncertain]
            : [NSString stringWithFormat:@"%zu SEGMENTS", _segments.size()]);
    [analysisStatus drawAtPoint:NSMakePoint(820, 370) withAttributes:valueAttrs];

    s3g::clap_gui::drawPanelFrame(kRightX, 404, kRightWidth, 238, style);
    s3g::clap_gui::drawPanelHeader(@"SEGMENT", true, kRightX, 404, kRightWidth, 21, labelAttrs, style);
    const NSRect segmentHeader = NSMakeRect(kRightX, 404, kRightWidth, 21);
    s3g::clap_gui::drawHeaderActionButton([self removeSegmentButtonRect], segmentHeader,
        @"-", labelAttrs, style);
    s3g::clap_gui::drawHeaderActionButton([self addSegmentButtonRect], segmentHeader,
        @"+", labelAttrs, style);
    [@"ALIAS" drawAtPoint:NSMakePoint(820, 442) withAttributes:labelAttrs];
    [@"ROOT MIDI" drawAtPoint:NSMakePoint(820, 472) withAttributes:labelAttrs];
    [@"FIXED" drawAtPoint:NSMakePoint(820, 502) withAttributes:labelAttrs];
    [@"PREUTTER" drawAtPoint:NSMakePoint(820, 532) withAttributes:labelAttrs];
    [@"OVERLAP" drawAtPoint:NSMakePoint(820, 562) withAttributes:labelAttrs];
    drawActionButton([self auditionButtonRect], @"AUDITION", labelAttrs, !_segments.empty());
    if (!_segments.empty()) {
        const auto& segment = _segments[_selectedSegment];
        NSString* metrics = [NSString stringWithFormat:@"%02zu/%02zu  V %.0f%%  M %.0f%%",
            _selectedSegment + 1u, _segments.size(), segment.voicedRatio * 100.0f,
            segment.aliasConfidence * 100.0f];
        [metrics drawAtPoint:NSMakePoint(948, 596) withAttributes:valueAttrs];
        const double durationMs = static_cast<double>(segment.endSample - segment.startSample)
            * 1000.0 / static_cast<double>(std::max(1, _sampleRate));
        [[NSString stringWithFormat:@"DURATION %.1f MS   NORM %+.1f DB",
            durationMs, segment.normalizationGainDb]
            drawAtPoint:NSMakePoint(820, 622) withAttributes:valueAttrs];
    }

    s3g::clap_gui::drawPanelFrame(kRightX, 654, kRightWidth, 108, style);
    s3g::clap_gui::drawPanelHeader(@"EXPORT", true, kRightX, 654, kRightWidth, 21, labelAttrs, style);
    const bool canExport = [self canExportVoicebank];
    drawActionButton([self exportButtonRect], @"EXPORT VOICEBANK", labelAttrs, canExport,
        canExport ? ActionButtonTone::Ready : ActionButtonTone::Required);
    [_status drawInRect:NSMakeRect(820, 724, 244, 28) withAttributes:valueAttrs];
}

@end

@implementation S3GVoxGeneratorView {
    __weak S3GVoxBuilderView* _builder;
    s3g::VoxSourceSynthParams _params;
    s3g::VoxSourceVocabulary _vocabulary;
    NSInteger _mode;
    NSInteger _openMenu;
    NSInteger _hoverMenuItem;
    NSInteger _dragControl;
    NSTrackingArea* _trackingArea;
    NSString* _seedDescription;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (instancetype)initWithFrame:(NSRect)frame builder:(S3GVoxBuilderView*)builder
{
    self = [super initWithFrame:frame];
    if (!self) return nil;
    _builder = builder;
    _vocabulary = s3g::VoxSourceVocabulary::Core35;
    _mode = 0;
    _openMenu = 0;
    _hoverMenuItem = -1;
    _dragControl = 0;
    _seedDescription = @"LOAD AND SELECT A VOCAL SEGMENT";
    return self;
}

- (void)updateTrackingAreas
{
    if (_trackingArea) [self removeTrackingArea:_trackingArea];
    _trackingArea = [[NSTrackingArea alloc] initWithRect:[self bounds]
        options:NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
        owner:self userInfo:nil];
    [self addTrackingArea:_trackingArea];
    [super updateTrackingAreas];
}

- (NSRect)modeMenuRect { return NSMakeRect(172, 79, 164, 17); }
- (NSRect)vocabularyMenuRect { return NSMakeRect(172, 107, 164, 17); }
- (NSRect)cancelButtonRect { return NSMakeRect(278, 500, 104, 26); }
- (NSRect)generateButtonRect { return NSMakeRect(394, 500, 108, 26); }

- (void)refreshSeedState
{
    S3GVoxBuilderView* builder = _builder;
    _seedDescription = builder ? [builder selectedSeedDescription]
                               : @"LOAD AND SELECT A VOCAL SEGMENT";
    [self setNeedsDisplay:YES];
}

- (CGFloat)normalizedValueForControl:(NSInteger)control
{
    switch (control) {
    case 1: return (_params.baseFrequencyHz - 55.0f) / 385.0f;
    case 2: return (_params.tractScale - 0.70f) / 0.65f;
    case 3: return _params.breath;
    case 4: return _params.roughness;
    case 5: return _params.variation;
    case 6: return (static_cast<float>(_params.randomSeed) - 1.0f) / 9998.0f;
    case 7: return _params.articulation;
    case 8: return _params.consonantStrength;
    case 9: return (_params.durationMs - 300.0f) / 1100.0f;
    default: return 0.0;
    }
}

- (void)setControl:(NSInteger)control normalized:(CGFloat)normalized
{
    const float value = static_cast<float>(std::clamp(normalized, 0.0, 1.0));
    switch (control) {
    case 1: _params.baseFrequencyHz = 55.0f + value * 385.0f; break;
    case 2: _params.tractScale = 0.70f + value * 0.65f; break;
    case 3: _params.breath = value; break;
    case 4: _params.roughness = value; break;
    case 5: _params.variation = value; break;
    case 6: _params.randomSeed = static_cast<uint32_t>(std::lround(1.0f + value * 9998.0f)); break;
    case 7: _params.articulation = value; break;
    case 8: _params.consonantStrength = value; break;
    case 9: _params.durationMs = 300.0f + value * 1100.0f; break;
    default: return;
    }
    [self setNeedsDisplay:YES];
}

- (CGFloat)rowForControl:(NSInteger)control
{
    if (control >= 1 && control <= 6) return 204.0 + static_cast<CGFloat>(control - 1) * 28.0;
    if (control >= 7 && control <= 9) return 406.0 + static_cast<CGFloat>(control - 7) * 28.0;
    return 0.0;
}

- (void)updateDraggedControl:(NSPoint)point
{
    if (_dragControl <= 0) return;
    [self setControl:_dragControl normalized:(point.x - 172.0) / 190.0];
}

- (NSString*)valueTextForControl:(NSInteger)control
{
    switch (control) {
    case 1: return [NSString stringWithFormat:@"%.1f HZ", _params.baseFrequencyHz];
    case 2: return [NSString stringWithFormat:@"%.2f", _params.tractScale];
    case 3: return [NSString stringWithFormat:@"%.0f%%", _params.breath * 100.0f];
    case 4: return [NSString stringWithFormat:@"%.0f%%", _params.roughness * 100.0f];
    case 5: return [NSString stringWithFormat:@"%.0f%%", _params.variation * 100.0f];
    case 6: return [NSString stringWithFormat:@"%u", _params.randomSeed];
    case 7: return [NSString stringWithFormat:@"%.0f%%", _params.articulation * 100.0f];
    case 8: return [NSString stringWithFormat:@"%.0f%%", _params.consonantStrength * 100.0f];
    case 9: return [NSString stringWithFormat:@"%.0f MS", _params.durationMs];
    default: return @"";
    }
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    const auto style = s3g::clap_gui::softTextStyle();
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    NSDictionary* labelAttrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    [style.bg setFill];
    NSRectFill([self bounds]);
    [@"s3g VOICE SOURCE GENERATOR" drawAtPoint:NSMakePoint(18, 14) withAttributes:titleAttrs];
    s3g::clap_gui::drawRightStatus(@"DRY MONO   48 KHZ", NSWidth([self bounds]), 14, valueAttrs);

    s3g::clap_gui::drawPanelFrame(18, 42, 484, 118, style);
    s3g::clap_gui::drawPanelHeader(@"SOURCE MODEL", true, 18, 42, 484, 21, labelAttrs, style);
    s3g::clap_gui::drawMenu(@"MODE", _mode == 1 ? @"SEEDED" : @"PROCEDURAL", 80,
        labelAttrs, valueAttrs, style, 34, 172, 164);
    s3g::clap_gui::drawMenu(@"VOCAB", _vocabulary == s3g::VoxSourceVocabulary::Full92
        ? @"FULL 92" : @"CORE 35", 108, labelAttrs, valueAttrs, style, 34, 172, 164);
    [@"SEED" drawAtPoint:NSMakePoint(34, 136) withAttributes:labelAttrs];
    NSString* seedText = _mode == 1 ? _seedDescription : @"DETERMINISTIC PARAMETER MODEL";
    [seedText drawInRect:NSMakeRect(172, 134, 312, 18) withAttributes:valueAttrs];

    s3g::clap_gui::drawPanelFrame(18, 172, 484, 184, style);
    s3g::clap_gui::drawPanelHeader(@"VOICE", true, 18, 172, 484, 21, labelAttrs, style);
    static NSString* voiceLabels[] = {
        @"PITCH", @"TRACT", @"BREATH", @"ROUGH", @"VARIATION", @"SEED"
    };
    for (NSInteger control = 1; control <= 6; ++control) {
        s3g::clap_gui::drawSlider(voiceLabels[control - 1],
            [self valueTextForControl:control], [self normalizedValueForControl:control],
            [self rowForControl:control], labelAttrs, valueAttrs, style,
            34, 172, 382, 190);
    }

    s3g::clap_gui::drawPanelFrame(18, 368, 484, 112, style);
    s3g::clap_gui::drawPanelHeader(@"ARTICULATION", true, 18, 368, 484, 21, labelAttrs, style);
    static NSString* articulationLabels[] = { @"CLARITY", @"CONSONANT", @"DURATION" };
    for (NSInteger control = 7; control <= 9; ++control) {
        s3g::clap_gui::drawSlider(articulationLabels[control - 7],
            [self valueTextForControl:control], [self normalizedValueForControl:control],
            [self rowForControl:control], labelAttrs, valueAttrs, style,
            34, 172, 382, 190);
    }

    const bool canGenerate = _mode == 0 || (_builder && [_builder hasSeedSource]);
    drawActionButton([self cancelButtonRect], @"CANCEL", labelAttrs);
    drawActionButton([self generateButtonRect], @"GENERATE", labelAttrs, canGenerate);
    NSString* footer = _mode == 1 && !canGenerate
        ? @"SEEDED MODE NEEDS A SELECTED VOCAL SEGMENT"
        : [NSString stringWithFormat:@"%s ALIASES   REPRODUCIBLE SEED %u",
            s3g::voxSourceVocabularyName(_vocabulary), _params.randomSeed];
    [footer drawAtPoint:NSMakePoint(18, 544) withAttributes:valueAttrs];

    if (_openMenu > 0) {
        static NSString* modeItems[] = { @"PROCEDURAL", @"SEEDED" };
        static NSString* vocabularyItems[] = { @"CORE 35", @"FULL 92" };
        NSString* const* items = _openMenu == 1 ? modeItems : vocabularyItems;
        const NSRect origin = _openMenu == 1 ? [self modeMenuRect] : [self vocabularyMenuRect];
        const NSRect menu = NSMakeRect(origin.origin.x, NSMaxY(origin), origin.size.width, 36.0);
        const int selected = _openMenu == 1 ? static_cast<int>(_mode)
            : (_vocabulary == s3g::VoxSourceVocabulary::Full92 ? 1 : 0);
        s3g::clap_gui::drawDropdownMenu(menu, 18.0, items, 2u,
            selected, static_cast<int>(_hoverMenuItem), valueAttrs, style);
    }
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu > 0) {
        const NSRect origin = _openMenu == 1 ? [self modeMenuRect] : [self vocabularyMenuRect];
        const NSRect menu = NSMakeRect(origin.origin.x, NSMaxY(origin), origin.size.width, 36.0);
        const int hit = s3g::clap_gui::dropdownHitIndex(point, menu, 18.0, 2u);
        if (hit >= 0) {
            if (_openMenu == 1) {
                _mode = hit;
                if (_mode == 1 && _builder && [_builder hasSeedSource]) {
                    _params.baseFrequencyHz = [_builder selectedSeedFrequency];
                }
            } else {
                _vocabulary = hit == 1 ? s3g::VoxSourceVocabulary::Full92
                                       : s3g::VoxSourceVocabulary::Core35;
            }
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        [self refreshSeedState];
        return;
    }
    if (NSPointInRect(point, [self modeMenuRect])) {
        _openMenu = 1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, [self vocabularyMenuRect])) {
        _openMenu = 2;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, [self cancelButtonRect])) {
        [[self window] orderOut:nil];
        return;
    }
    if (NSPointInRect(point, [self generateButtonRect])) {
        if (_mode == 1 && (!_builder || ![_builder hasSeedSource])) {
            NSBeep();
            return;
        }
        _params.seeded = _mode == 1;
        if (_builder && [_builder beginVoiceGeneration:_params vocabulary:_vocabulary]) {
            [[self window] orderOut:nil];
        }
        return;
    }
    for (NSInteger control = 1; control <= 9; ++control) {
        const CGFloat row = [self rowForControl:control];
        if (!NSPointInRect(point, NSMakeRect(164, row - 8, 210, 25))) continue;
        _dragControl = control;
        [self updateDraggedControl:point];
        return;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    [self updateDraggedControl:[self convertPoint:[event locationInWindow] fromView:nil]];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragControl = 0;
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu <= 0) return;
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    const NSRect origin = _openMenu == 1 ? [self modeMenuRect] : [self vocabularyMenuRect];
    const NSRect menu = NSMakeRect(origin.origin.x, NSMaxY(origin), origin.size.width, 36.0);
    const NSInteger hover = s3g::clap_gui::dropdownHitIndex(point, menu, 18.0, 2u);
    if (hover != _hoverMenuItem) {
        _hoverMenuItem = hover;
        [self setNeedsDisplay:YES];
    }
}

@end

@interface S3GVoxBuilderAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation S3GVoxBuilderAppDelegate {
    NSWindow* _window;
    S3GVoxBuilderView* _view;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    (void)notification;
    const NSRect content = NSMakeRect(0, 0, kGuiWidth, kGuiHeight);
    _window = [[NSWindow alloc] initWithContentRect:content
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
        backing:NSBackingStoreBuffered defer:NO];
    [_window setTitle:@"s3g Vox Builder"];
    [_window setReleasedWhenClosed:NO];
    [_window setSharingType:NSWindowSharingReadOnly];
    [_window setCollectionBehavior:NSWindowCollectionBehaviorMoveToActiveSpace];
    _view = [[S3GVoxBuilderView alloc] initWithFrame:content];
    [_window setContentView:_view];
    NSScreen* screen = [NSScreen mainScreen] ?: [[NSScreen screens] firstObject];
    if (screen) {
        const NSRect visible = [screen visibleFrame];
        [_window setFrameOrigin:NSMakePoint(
            NSMidX(visible) - NSWidth([_window frame]) * 0.5,
            NSMidY(visible) - NSHeight([_window frame]) * 0.5)];
    } else {
        [_window center];
    }
    [NSApp activateIgnoringOtherApps:YES];
    [_window makeFirstResponder:_view];
    [_window makeKeyAndOrderFront:nil];
    NSArray<NSString*>* arguments = [[NSProcessInfo processInfo] arguments];
    if ([arguments count] > 1u) {
        NSMutableArray<NSURL*>* urls = [NSMutableArray array];
        for (NSUInteger i = 1u; i < [arguments count]; ++i) {
            NSString* path = [arguments objectAtIndex:i];
            if (![path hasPrefix:@"-"]) [urls addObject:[NSURL fileURLWithPath:path]];
        }
        if ([urls count] > 0u) [_view loadAudioURLs:urls];
    }
    NSString* snapshotDirectory = [[[NSProcessInfo processInfo] environment]
        objectForKey:@"S3G_VOX_BUILDER_SNAPSHOT_DIR"];
    if ([snapshotDirectory length] > 0u) {
        [[NSFileManager defaultManager] createDirectoryAtPath:snapshotDirectory
            withIntermediateDirectories:YES attributes:nil error:nil];
        writeViewSnapshot(_view,
            [snapshotDirectory stringByAppendingPathComponent:@"vox-builder-main.png"]);
        S3GVoxGeneratorView* generator = [[S3GVoxGeneratorView alloc]
            initWithFrame:NSMakeRect(0, 0, 520, 580) builder:_view];
        NSPanel* generatorWindow = [[NSPanel alloc]
            initWithContentRect:NSMakeRect(0, 0, 520, 580)
            styleMask:NSWindowStyleMaskBorderless
            backing:NSBackingStoreBuffered defer:NO];
        [generatorWindow setContentView:generator];
        [generatorWindow orderFront:nil];
        [generatorWindow displayIfNeeded];
        [generator refreshSeedState];
        writeViewSnapshot(generator,
            [snapshotDirectory stringByAppendingPathComponent:@"vox-builder-generator.png"]);
        [generatorWindow orderOut:nil];
        dispatch_async(dispatch_get_main_queue(), ^{ [NSApp terminate:nil]; });
    }
}

- (void)application:(NSApplication*)application openFiles:(NSArray<NSString*>*)filenames
{
    NSMutableArray<NSURL*>* urls = [NSMutableArray arrayWithCapacity:[filenames count]];
    for (NSString* filename in filenames) [urls addObject:[NSURL fileURLWithPath:filename]];
    const BOOL loaded = [_view loadAudioURLs:urls];
    [application replyToOpenOrPrint:loaded
        ? NSApplicationDelegateReplySuccess : NSApplicationDelegateReplyFailure];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender
{
    (void)sender;
    return YES;
}

@end

static S3GVoxBuilderAppDelegate* gAppDelegate = nil;

static void installApplicationMenu()
{
    NSMenu* menuBar = [[NSMenu alloc] init];
    NSMenuItem* appItem = [[NSMenuItem alloc] init];
    [menuBar addItem:appItem];
    [NSApp setMainMenu:menuBar];
    NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"s3g Vox Builder"];
    [appMenu addItemWithTitle:@"Quit s3g Vox Builder"
        action:@selector(terminate:) keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
}

int main(int argc, const char* argv[])
{
    (void)argc;
    (void)argv;
    @autoreleasepool {
        NSApplication* application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        installApplicationMenu();
        gAppDelegate = [[S3GVoxBuilderAppDelegate alloc] init];
        [application setDelegate:gAppDelegate];
        [application run];
    }
    return 0;
}
