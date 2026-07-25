#include "s3g_realtime.h"
#include "s3g_spectral_spray.h"

#include <clap/clap.h>
#include <clap/ext/latency.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_clap_macos.h"
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace {

constexpr uint32_t kChannelCount = 8;
constexpr uint32_t kStateVersion = 1;
constexpr uint32_t kGuiWidth = 760;
constexpr uint32_t kGuiHeight = 376;

constexpr clap_id kSprayBinsParamId = 1;
constexpr clap_id kDriftParamId = 2;
constexpr clap_id kHoldParamId = 3;
constexpr clap_id kFreezeParamId = 4;
constexpr clap_id kFeedbackParamId = 5;
constexpr clap_id kSmearParamId = 6;
constexpr clap_id kHolesParamId = 7;
constexpr clap_id kPhaseBlurParamId = 8;
constexpr clap_id kLoFreqParamId = 9;
constexpr clap_id kHiFreqParamId = 10;
constexpr clap_id kTiltParamId = 11;
constexpr clap_id kMixParamId = 12;
constexpr clap_id kGainParamId = 13;
constexpr clap_id kSafetyParamId = 14;

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::SpectralSprayParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    s3g::SpectralSprayParams params {};
    s3g::SpectralSpray spray;
    std::vector<std::vector<float>> input32;
    std::vector<std::vector<float>> output32;
    std::vector<const float*> inputPtrs;
    std::vector<float*> outputPtrs;
    std::atomic<float> outputPeak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

void applyParam(Plugin& p, clap_id id, double value)
{
    switch (id) {
    case kSprayBinsParamId: p.params.sprayBins = static_cast<float>(std::clamp(value, 0.0, 256.0)); break;
    case kDriftParamId: p.params.drift = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kHoldParamId: p.params.hold = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kFreezeParamId: p.params.freeze = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kFeedbackParamId: p.params.feedback = static_cast<float>(std::clamp(value, 0.0, 0.85)); break;
    case kSmearParamId: p.params.smear = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kHolesParamId: p.params.holes = static_cast<float>(std::clamp(value, 0.0, 0.95)); break;
    case kPhaseBlurParamId: p.params.phaseBlur = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kLoFreqParamId: p.params.loFreq = static_cast<float>(std::clamp(value, 0.0, 24000.0)); break;
    case kHiFreqParamId: p.params.hiFreq = static_cast<float>(std::clamp(value, 20.0, 24000.0)); break;
    case kTiltParamId: p.params.tilt = static_cast<float>(std::clamp(value, -1.0, 1.0)); break;
    case kMixParamId: p.params.mix = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kGainParamId: p.params.gainDb = static_cast<float>(std::clamp(value, -60.0, 18.0)); break;
    case kSafetyParamId: p.params.safety = static_cast<float>(std::clamp(value, 0.05, 1.0)); break;
    default: break;
    }
    p.spray.setParams(p.params);
}

bool init(const clap_plugin_t*) { return true; }
#if defined(__APPLE__)
void guiDestroy(const clap_plugin_t* plugin);
#endif
void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    guiDestroy(plugin);
#endif
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t maxFrames)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->maxFrames = std::max<uint32_t>(1u, maxFrames);
    p->input32.assign(kChannelCount, std::vector<float>(p->maxFrames, 0.0f));
    p->output32.assign(kChannelCount, std::vector<float>(p->maxFrames, 0.0f));
    p->inputPtrs.assign(kChannelCount, nullptr);
    p->outputPtrs.assign(kChannelCount, nullptr);
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        p->inputPtrs[ch] = p->input32[ch].data();
        p->outputPtrs[ch] = p->output32[ch].data();
    }
    if (!p->spray.prepare(sampleRate, kChannelCount, 4096u, 8u, p->maxFrames)) return false;
    p->spray.setParams(p->params);
    return true;
}
void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin) { self(plugin)->spray.reset(); }

void readParamEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) return;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            applyParam(p, param->param_id, param->value);
        }
    }
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    readParamEvents(*p, proc->in_events);
    if (proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;
    const auto& input = proc->audio_inputs[0];
    const auto& output = proc->audio_outputs[0];
    const uint32_t frames = std::min(proc->frames_count, p->maxFrames);
    if (frames == 0u || output.channel_count < kChannelCount) return CLAP_PROCESS_CONTINUE;

    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        for (uint32_t i = 0; i < frames; ++i) {
            if (ch < input.channel_count && input.data32 && input.data32[ch]) p->input32[ch][i] = input.data32[ch][i];
            else if (ch < input.channel_count && input.data64 && input.data64[ch]) p->input32[ch][i] = static_cast<float>(input.data64[ch][i]);
            else p->input32[ch][i] = 0.0f;
        }
    }
    p->spray.setParams(p->params);
    p->spray.process(p->inputPtrs.data(), p->outputPtrs.data(), frames);

    float blockPeak = 0.0f;
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        for (uint32_t i = 0; i < frames; ++i) {
            const float v = p->output32[ch][i];
            if (output.data32 && output.data32[ch]) output.data32[ch][i] = v;
            if (output.data64 && output.data64[ch]) output.data64[ch][i] = static_cast<double>(v);
            blockPeak = std::max(blockPeak, std::abs(v));
        }
    }
    for (uint32_t ch = kChannelCount; ch < output.channel_count; ++ch) {
        if (output.data32 && output.data32[ch]) std::fill(output.data32[ch], output.data32[ch] + frames, 0.0f);
        if (output.data64 && output.data64[ch]) std::fill(output.data64[ch], output.data64[ch] + frames, 0.0);
    }
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.90f, blockPeak), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }
bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0 || !info) return false;
    info->id = isInput ? 10 : 20;
    std::snprintf(info->name, sizeof(info->name), "8ch %s", isInput ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannelCount;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = isInput ? 20 : 10;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; };
constexpr ParamDef kParamDefs[] {
    { kSprayBinsParamId, "Spray Bins", 0.0, 256.0, 18.0 },
    { kDriftParamId, "Drift", 0.0, 1.0, 0.18 },
    { kHoldParamId, "Hold", 0.0, 1.0, 0.72 },
    { kFreezeParamId, "Freeze", 0.0, 1.0, 0.0 },
    { kFeedbackParamId, "Feedback", 0.0, 0.85, 0.18 },
    { kSmearParamId, "Smear", 0.0, 1.0, 0.25 },
    { kHolesParamId, "Holes", 0.0, 0.95, 0.05 },
    { kPhaseBlurParamId, "Phase Blur", 0.0, 1.0, 0.28 },
    { kLoFreqParamId, "Lo Freq", 0.0, 24000.0, 0.0 },
    { kHiFreqParamId, "Hi Freq", 20.0, 24000.0, 20000.0 },
    { kTiltParamId, "Tilt", -1.0, 1.0, 0.0 },
    { kMixParamId, "Mix", 0.0, 1.0, 1.0 },
    { kGainParamId, "Gain", -60.0, 18.0, -2.5 },
    { kSafetyParamId, "Safety", 0.05, 1.0, 0.82 },
};
uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(sizeof(kParamDefs) / sizeof(kParamDefs[0])); }
bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParamDefs[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, "Spectral Spray", sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}
bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const auto& p = self(plugin)->params;
    switch (id) {
    case kSprayBinsParamId: *value = p.sprayBins; return true;
    case kDriftParamId: *value = p.drift; return true;
    case kHoldParamId: *value = p.hold; return true;
    case kFreezeParamId: *value = p.freeze; return true;
    case kFeedbackParamId: *value = p.feedback; return true;
    case kSmearParamId: *value = p.smear; return true;
    case kHolesParamId: *value = p.holes; return true;
    case kPhaseBlurParamId: *value = p.phaseBlur; return true;
    case kLoFreqParamId: *value = p.loFreq; return true;
    case kHiFreqParamId: *value = p.hiFreq; return true;
    case kTiltParamId: *value = p.tilt; return true;
    case kMixParamId: *value = p.mix; return true;
    case kGainParamId: *value = p.gainDb; return true;
    case kSafetyParamId: *value = p.safety; return true;
    default: return false;
    }
}
bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (id == kSprayBinsParamId) std::snprintf(display, size, "%.0f bins", value);
    else if (id == kLoFreqParamId || id == kHiFreqParamId) std::snprintf(display, size, "%.0f Hz", value);
    else if (id == kGainParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kTiltParamId) std::snprintf(display, size, "%+.2f", value);
    else std::snprintf(display, size, "%.0f%%", value * 100.0);
    return true;
}
bool paramsTextToValue(const clap_plugin_t*, clap_id, const char* display, double* value)
{
    if (!display || !value) return false;
    *value = std::atof(display);
    return true;
}
void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readParamEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    SavedState s {};
    s.params = self(plugin)->params;
    return stream->write(stream, &s, sizeof(s)) == static_cast<int64_t>(sizeof(s));
}
bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState s {};
    if (stream->read(stream, &s, sizeof(s)) != static_cast<int64_t>(sizeof(s)) || s.version != kStateVersion) return false;
    auto* p = self(plugin);
    p->params = s.params;
    p->spray.setParams(p->params);
    return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };
uint32_t latencyGet(const clap_plugin_t* plugin) { return self(plugin)->spray.latencyFrames(); }
const clap_plugin_latency_t latencyExt { latencyGet };

} // namespace

#if defined(__APPLE__)
constexpr auto kOutputPanel = s3g::gui_layout::compactEffectOutputPanel(3u);
constexpr auto kRangePanel =
    s3g::gui_layout::compactEffectLeftPanel(
        kOutputPanel, s3g::gui_layout::PanelRole::ToneShape, 4u);
constexpr auto kMotionPanel =
    s3g::gui_layout::compactEffectRightPanel(
        s3g::gui_layout::PanelRole::Motion, 7u);
constexpr std::array kFirstColumnPanels { kOutputPanel, kRangePanel };
constexpr std::array kSecondColumnPanels { kMotionPanel };
static_assert(s3g::gui_layout::validateColumn(
    kFirstColumnPanels, s3g::gui_layout::kCompactEffectFamilyLayout.canvas));
static_assert(s3g::gui_layout::validateColumn(
    kSecondColumnPanels, s3g::gui_layout::kCompactEffectFamilyLayout.canvas,
    false));

@interface S3G8chSpectralSprayView : NSView {
    void* _plugin;
    int _dragSlider;
    NSTimer* _timer;
    char _titlePresetName[64];
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)drawRow:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y panel:(const s3g::gui_layout::Panel&)panel attrs:(NSDictionary*)attrs;
- (void)updateSlider:(NSPoint)point;
@end

@implementation S3G8chSpectralSprayView
- (id)initWithPlugin:(void*)plugin { self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)]; if (self) { _plugin = plugin; _dragSlider = -1; _timer = nil; std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "INIT"); } return self; }
- (BOOL)isFlipped { return YES; }
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (void)startRefreshTimer { if (_timer) return; _timer = [NSTimer timerWithTimeInterval:1.0/20.0 target:self selector:@selector(refresh:) userInfo:nil repeats:YES]; [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes]; }
- (void)stopRefreshTimer { if (_timer) { [_timer invalidate]; _timer = nil; } }
- (void)refresh:(NSTimer*)timer { (void)timer; if (![self isHidden] && _plugin && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES]; }
- (void)drawRow:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y panel:(const s3g::gui_layout::Panel&)panel attrs:(NSDictionary*)attrs
{
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawProcessorSlider(name, value, norm, y,
        panel.frame.x, panel.frame.width, attrs,
        s3g::clap_gui::softValueAttrs(), style);
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSDictionary* lab = s3g::clap_gui::softLabelAttrs();
    NSDictionary* small = s3g::clap_gui::softValueAttrs();
    const float pk = p->outputPeak.load(std::memory_order_relaxed);
    const auto titleBand = s3g::gui_layout::compactEffectTitleBand(
        s3g::gui_layout::kCompactEffectFamilyLayout.canvas);
    s3g::clap_gui::drawCompactEffectTitleBand(
        @"s3g EFFECT SPECTRAL SPRAY 8CH",
        [NSString stringWithUTF8String:_titlePresetName],
        s3g::clap_gui::peakDbText(pk), titleBand, style);
    const auto drawPanel = [&](NSString* name,
                               const s3g::gui_layout::Panel& panel) {
        s3g::clap_gui::drawPanelFrame(
            panel.frame.x, panel.frame.y, panel.frame.width, panel.frame.height,
            style);
        s3g::clap_gui::drawPanelHeader(
            name, true, panel.frame.x, panel.frame.y, panel.frame.width,
            s3g::gui_layout::kStandardMetrics.headerHeight, lab, style);
    };
    drawPanel(@"OUTPUT", kOutputPanel);
    drawPanel(@"RANGE / PHASE", kRangePanel);
    drawPanel(@"SPECTRAL MOTION", kMotionPanel);
    const auto& prm = p->params;
    [self drawRow:@"OUT" value:[NSString stringWithFormat:@"%+.1f dB", prm.gainDb] norm:(prm.gainDb + 60.0f) / 78.0f y:s3g::gui_layout::rowY(kOutputPanel, 0u) panel:kOutputPanel attrs:lab];
    [self drawRow:@"MIX" value:[NSString stringWithFormat:@"%.0f%%", prm.mix * 100.0f] norm:prm.mix y:s3g::gui_layout::rowY(kOutputPanel, 1u) panel:kOutputPanel attrs:lab];
    [self drawRow:@"SAFE" value:[NSString stringWithFormat:@"%.0f%%", prm.safety * 100.0f] norm:(prm.safety - 0.05f) / 0.95f y:s3g::gui_layout::rowY(kOutputPanel, 2u) panel:kOutputPanel attrs:lab];
    [self drawRow:@"LO" value:[NSString stringWithFormat:@"%.0f Hz", prm.loFreq] norm:prm.loFreq / 24000.0f y:s3g::gui_layout::rowY(kRangePanel, 0u) panel:kRangePanel attrs:lab];
    [self drawRow:@"HI" value:[NSString stringWithFormat:@"%.0f Hz", prm.hiFreq] norm:prm.hiFreq / 24000.0f y:s3g::gui_layout::rowY(kRangePanel, 1u) panel:kRangePanel attrs:lab];
    [self drawRow:@"TILT" value:[NSString stringWithFormat:@"%+.2f", prm.tilt] norm:(prm.tilt + 1.0f) * 0.5f y:s3g::gui_layout::rowY(kRangePanel, 2u) panel:kRangePanel attrs:lab];
    [self drawRow:@"PHAS" value:[NSString stringWithFormat:@"%.0f%%", prm.phaseBlur * 100.0f] norm:prm.phaseBlur y:s3g::gui_layout::rowY(kRangePanel, 3u) panel:kRangePanel attrs:lab];
    [self drawRow:@"BINS" value:[NSString stringWithFormat:@"%.0f", prm.sprayBins] norm:prm.sprayBins / 256.0f y:s3g::gui_layout::rowY(kMotionPanel, 0u) panel:kMotionPanel attrs:lab];
    [self drawRow:@"DRFT" value:[NSString stringWithFormat:@"%.0f%%", prm.drift * 100.0f] norm:prm.drift y:s3g::gui_layout::rowY(kMotionPanel, 1u) panel:kMotionPanel attrs:lab];
    [self drawRow:@"HOLD" value:[NSString stringWithFormat:@"%.0f%%", prm.hold * 100.0f] norm:prm.hold y:s3g::gui_layout::rowY(kMotionPanel, 2u) panel:kMotionPanel attrs:lab];
    [self drawRow:@"FRZ" value:[NSString stringWithFormat:@"%.0f%%", prm.freeze * 100.0f] norm:prm.freeze y:s3g::gui_layout::rowY(kMotionPanel, 3u) panel:kMotionPanel attrs:lab];
    [self drawRow:@"FDBK" value:[NSString stringWithFormat:@"%.0f%%", prm.feedback * 100.0f] norm:prm.feedback / 0.85f y:s3g::gui_layout::rowY(kMotionPanel, 4u) panel:kMotionPanel attrs:lab];
    [self drawRow:@"SMR" value:[NSString stringWithFormat:@"%.0f%%", prm.smear * 100.0f] norm:prm.smear y:s3g::gui_layout::rowY(kMotionPanel, 5u) panel:kMotionPanel attrs:lab];
    [self drawRow:@"HOLE" value:[NSString stringWithFormat:@"%.0f%%", prm.holes * 100.0f] norm:prm.holes / 0.95f y:s3g::gui_layout::rowY(kMotionPanel, 6u) panel:kMotionPanel attrs:lab];
    [@"FFT 4096 / 8x OLA" drawAtPoint:NSMakePoint(
        kMotionPanel.frame.x + 16.0,
        kMotionPanel.frame.y + kMotionPanel.frame.height + 12.0)
        withAttributes:small];
}
- (void)updateSlider:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    const bool output = _dragSlider == 12 || _dragSlider == 13 || _dragSlider == 14;
    const bool range = _dragSlider >= 8 && _dragSlider <= 11;
    const auto& panel = output ? kOutputPanel : (range ? kRangePanel : kMotionPanel);
    const double x0 = s3g::gui_layout::processorControlX(panel.frame.x);
    const double trackWidth = s3g::gui_layout::processorTrackWidth(panel.frame.width);
    const double n = std::clamp((point.x - x0) / trackWidth, 0.0, 1.0);
    switch (_dragSlider) {
    case 1: applyParam(*p, kSprayBinsParamId, n * 256.0); break;
    case 2: applyParam(*p, kDriftParamId, n); break;
    case 3: applyParam(*p, kHoldParamId, n); break;
    case 4: applyParam(*p, kFreezeParamId, n); break;
    case 5: applyParam(*p, kFeedbackParamId, n * 0.85); break;
    case 6: applyParam(*p, kSmearParamId, n); break;
    case 7: applyParam(*p, kHolesParamId, n * 0.95); break;
    case 8: applyParam(*p, kPhaseBlurParamId, n); break;
    case 9: applyParam(*p, kLoFreqParamId, n * 24000.0); break;
    case 10: applyParam(*p, kHiFreqParamId, 20.0 + n * 23980.0); break;
    case 11: applyParam(*p, kTiltParamId, -1.0 + n * 2.0); break;
    case 12: applyParam(*p, kMixParamId, n); break;
    case 13: applyParam(*p, kGainParamId, -60.0 + n * 78.0); break;
    case 14: applyParam(*p, kSafetyParamId, 0.05 + n * 0.95); break;
    default: break;
    }
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    const auto titleBand = s3g::gui_layout::compactEffectTitleBand(
        s3g::gui_layout::kCompactEffectFamilyLayout.canvas);
    if (s3g::clap_gui::handleProcessorTitleClick(
            pt, &p->plugin, @"Effect Spectral Spray", titleBand,
            _titlePresetName, sizeof(_titlePresetName))) {
        [self setNeedsDisplay:YES];
        return;
    }
    const auto beginSlider = [&](clap_id paramId) {
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &p->plugin, paramId, &defaultValue)) {
            applyParam(*p, paramId, defaultValue);
            _dragSlider = -1;
        } else {
            _dragSlider = static_cast<int>(paramId);
            [self updateSlider:pt];
        }
        [self setNeedsDisplay:YES];
    };
    const clap_id outputIds[] {
        kGainParamId, kMixParamId, kSafetyParamId
    };
    const clap_id rangeIds[] {
        kLoFreqParamId, kHiFreqParamId, kTiltParamId, kPhaseBlurParamId
    };
    const clap_id motionIds[] {
        kSprayBinsParamId, kDriftParamId, kHoldParamId, kFreezeParamId,
        kFeedbackParamId, kSmearParamId, kHolesParamId
    };
    const auto hitPanel = [&](const s3g::gui_layout::Panel& panel,
                              const clap_id* ids, uint32_t count) {
        for (uint32_t row = 0u; row < count; ++row) {
            if (NSPointInRect(pt, s3g::clap_gui::cocoaRect(
                    s3g::gui_layout::sliderHitRect(panel, row)))) {
                beginSlider(ids[row]);
                return true;
            }
        }
        return false;
    };
    if (hitPanel(kOutputPanel, outputIds, 3u)
        || hitPanel(kRangePanel, rangeIds, 4u)
        || hitPanel(kMotionPanel, motionIds, 7u)) {
        return;
    }
}
- (void)mouseDragged:(NSEvent*)event { if (_dragSlider > 0) [self updateSlider:[self convertPoint:[event locationInWindow] fromView:nil]]; }
- (void)mouseUp:(NSEvent*)event { (void)event; _dragSlider = -1; }
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3G8chSpectralSprayView alloc] initWithPlugin:p]; if (!p->guiView) return false; if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport, static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight)) { [static_cast<NSView*>(p->guiView) release]; p->guiView = nullptr; return false; } return true; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible = false; [static_cast<S3G8chSpectralSprayView*>(p->guiView) stopRefreshTimer]; s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView); } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, w, h); }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport, static_cast<NSView*>(win->cocoa), p->host); }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false; p->guiVisible = true; [static_cast<S3G8chSpectralSprayView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3G8chSpectralSprayView*>(p->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true); }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
#endif

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
    if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) return &latencyExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, nullptr };
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.8ch-spectral-spray",
    "s3g Effect Spectral Spray 8ch",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "8-channel C++ FFT spectral spray effect with bin displacement, spectral memory, freeze, and dry/wet alignment.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->plugin.desc = &descriptor;
    p->plugin.plugin_data = p;
    p->plugin.init = init;
    p->plugin.destroy = destroy;
    p->plugin.activate = activate;
    p->plugin.deactivate = deactivate;
    p->plugin.start_processing = startProcessing;
    p->plugin.stop_processing = stopProcessing;
    p->plugin.reset = reset;
    p->plugin.process = process;
    p->plugin.get_extension = pluginGetExtension;
    p->plugin.on_main_thread = onMainThread;
    return &p->plugin;
}
uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index) { return index == 0 ? &descriptor : nullptr; }
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin };
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId) { return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }

} // namespace

extern "C" const clap_plugin_entry_t clap_entry { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
