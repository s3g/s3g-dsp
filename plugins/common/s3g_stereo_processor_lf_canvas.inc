// Literal Cocoa adaptation: preserve layout, drawing order and hit maps.
namespace stereo_processor_canvas {
namespace drawing = s3g::portable_gui::stereo_drawing;
namespace foundation = s3g::portable_gui::foundation;

constexpr clap_id kFactoryPresetMenuId = 0x7ffffff0u;

struct SynthUiRow {
    clap_id id;
    const char* label;
    double panelX;
    double panelWidth;
    double y;
};

namespace layout = s3g::gui_layout;

constexpr double kLeftPanelX = 16.0;
constexpr double kRightPanelX = 506.0;
constexpr double kPanelWidth = 458.0;
constexpr layout::Canvas kSynthCanvas {
    static_cast<double>(kGuiWidth), static_cast<double>(kGuiHeight)
};
constexpr layout::Column kLeftColumn {
    kLeftPanelX, kPanelWidth, layout::kStandardMetrics.contentTop
};
constexpr layout::Column kRightColumn {
    kRightPanelX, kPanelWidth, layout::kStandardMetrics.contentTop
};

constexpr auto kOutputPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Output,
    kLeftColumn, layout::kStandardMetrics.contentTop, 1u);
constexpr auto kModalPanel = layout::fittedStackPanel(
    layout::PanelRole::Engine, kOutputPanel, 6u);
constexpr auto kPitchPanel = layout::fittedStackPanel(
    layout::PanelRole::ToneShape, kModalPanel, 3u);
constexpr auto kEnvelopePanel = layout::fittedStackPanel(
    layout::PanelRole::Envelope, kPitchPanel, 4u);

constexpr auto kTonePanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Topology,
    kRightColumn, layout::kStandardMetrics.contentTop, 6u);
constexpr auto kShredPanel = layout::fittedStackPanel(
    layout::PanelRole::Projection, kTonePanel, 6u);
constexpr auto kMotionPanel = layout::fittedStackPanel(
    layout::PanelRole::Motion, kShredPanel, 5u);
constexpr auto kRoutingPanel = layout::fittedStackPanel(
    layout::PanelRole::Utility, kMotionPanel, 1u);

constexpr std::array kLeftPanels {
    kOutputPanel, kModalPanel, kPitchPanel, kEnvelopePanel,
};
constexpr std::array kRightPanels {
    kTonePanel, kShredPanel, kMotionPanel, kRoutingPanel,
};

static_assert(layout::validateColumn(kLeftPanels, kSynthCanvas));
static_assert(layout::validateColumn(kRightPanels, kSynthCanvas, false));
static_assert(layout::rolesFollowTemplate(
    kLeftPanels, layout::kProceduralEncoderTemplate, true));
static_assert(layout::rolesFollowTemplate(
    kRightPanels, layout::kProceduralEncoderTemplate, false));
static_assert(layout::rowY(kOutputPanel, 0u) == 78.0);
static_assert(kEnvelopePanel.frame.y + kEnvelopePanel.frame.height
    + 12.0 <= kGuiHeight);

constexpr std::array<SynthUiRow, 32u> kUiRows {{
    { kOutputParamId, "OUT", kLeftPanelX, kPanelWidth,
        layout::rowY(kOutputPanel, 0u) },

    { kFundamentalParamId, "SUB", kLeftPanelX, kPanelWidth,
        layout::rowY(kModalPanel, 0u) },
    { kBodyParamId, "MODAL BODY", kLeftPanelX, kPanelWidth,
        layout::rowY(kModalPanel, 1u) },
    { kLoadingParamId, "MASS LOAD", kLeftPanelX, kPanelWidth,
        layout::rowY(kModalPanel, 2u) },
    { kCouplingParamId, "COUPLING", kLeftPanelX, kPanelWidth,
        layout::rowY(kModalPanel, 3u) },
    { kDampingParamId, "DAMPING", kLeftPanelX, kPanelWidth,
        layout::rowY(kModalPanel, 4u) },
    { kExcitationParamId, "EXCITATION", kLeftPanelX, kPanelWidth,
        layout::rowY(kModalPanel, 5u) },

    { kTransposeParamId, "TRANSPOSE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPitchPanel, 0u) },
    { kGlideParamId, "GLIDE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPitchPanel, 1u) },
    { kPitchTransientParamId, "PITCH TRANSIENT", kLeftPanelX, kPanelWidth,
        layout::rowY(kPitchPanel, 2u) },

    { kAttackParamId, "ATTACK", kLeftPanelX, kPanelWidth,
        layout::rowY(kEnvelopePanel, 0u) },
    { kDecayParamId, "DECAY", kLeftPanelX, kPanelWidth,
        layout::rowY(kEnvelopePanel, 1u) },
    { kSustainParamId, "SUSTAIN", kLeftPanelX, kPanelWidth,
        layout::rowY(kEnvelopePanel, 2u) },
    { kReleaseParamId, "RELEASE", kLeftPanelX, kPanelWidth,
        layout::rowY(kEnvelopePanel, 3u) },

    { kHarmonicsParamId, "UPPER MODES", kRightPanelX, kPanelWidth,
        layout::rowY(kTonePanel, 0u) },
    { kFilterCutoffParamId, "CUTOFF", kRightPanelX, kPanelWidth,
        layout::rowY(kTonePanel, 1u) },
    { kFilterResonanceParamId, "RESONANCE", kRightPanelX, kPanelWidth,
        layout::rowY(kTonePanel, 2u) },
    { kDriveParamId, "MEMBRANE DRIVE", kRightPanelX, kPanelWidth,
        layout::rowY(kTonePanel, 3u) },
    { kProcessedMixParamId, "FILTER MIX", kRightPanelX, kPanelWidth,
        layout::rowY(kTonePanel, 4u) },
    { kValvePreampParamId, "TUBE", kRightPanelX, kPanelWidth,
        layout::rowY(kTonePanel, 5u) },

    { kShredCircuitParamId, "CIRCUIT", kRightPanelX, kPanelWidth,
        layout::rowY(kShredPanel, 0u) },
    { kShredParamId, "SHRED", kRightPanelX, kPanelWidth,
        layout::rowY(kShredPanel, 1u) },
    { kShredFeedbackParamId, "FEEDBACK", kRightPanelX, kPanelWidth,
        layout::rowY(kShredPanel, 2u) },
    { kShredFeedbackToneLevelParamId, "FB TONE LEVEL", kRightPanelX,
        kPanelWidth, layout::rowY(kShredPanel, 3u) },
    { kShredColorParamId, "COLOR", kRightPanelX, kPanelWidth,
        layout::rowY(kShredPanel, 4u) },
    { kShredMixParamId, "MIX", kRightPanelX, kPanelWidth,
        layout::rowY(kShredPanel, 5u) },

    { kFilterMotionClockParamId, "CLOCK", kRightPanelX, kPanelWidth,
        layout::rowY(kMotionPanel, 0u) },
    { kFilterMotionDivisionParamId, "DIVISION", kRightPanelX, kPanelWidth,
        layout::rowY(kMotionPanel, 1u) },
    { kFilterMotionRateParamId, "FREE RATE", kRightPanelX, kPanelWidth,
        layout::rowY(kMotionPanel, 2u) },
    { kFilterMotionDepthParamId, "AMOUNT", kRightPanelX, kPanelWidth,
        layout::rowY(kMotionPanel, 3u) },
    { kAmplitudeMotionPositionParamId, "POSITION", kRightPanelX,
        kPanelWidth, layout::rowY(kMotionPanel, 4u) },

    { kMidiReceiveParamId, "MIDI RECEIVE", kRightPanelX, kPanelWidth,
        layout::rowY(kRoutingPanel, 0u) },
}};

static_assert(kUiRows[0u].id == kOutputParamId);

bool isUiMenuParam(clap_id id)
{
    return id == kMidiReceiveParamId || id == kFilterMotionClockParamId
        || id == kFilterMotionDivisionParamId
        || id == kAmplitudeMotionPositionParamId
        || id == kShredCircuitParamId;
}

uint32_t uiMenuItemCount(clap_id id)
{
    if (id == kMidiReceiveParamId) return 17u;
    if (id == kFilterMotionClockParamId) {
        return s3g::kMotionClockCount;
    }
    if (id == kFilterMotionDivisionParamId) {
        return s3g::kMotionDivisionCount;
    }
    if (id == kAmplitudeMotionPositionParamId) {
        return s3g::kAmplitudeMotionPositionCount;
    }
    if (id == kShredCircuitParamId) {
        return s3g::kBassShredCircuitCount;
    }
    return 0u;
}

bool isLogUiParam(clap_id id)
{
    return id == kAttackParamId || id == kDecayParamId
        || id == kReleaseParamId || id == kFilterCutoffParamId
        || id == kFilterMotionRateParamId;
}

double uiNormalizedValue(clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def) return 0.0;
    if (isLogUiParam(id) && def->minimum > 0.0) {
        return std::clamp(std::log(value / def->minimum)
            / std::log(def->maximum / def->minimum), 0.0, 1.0);
    }
    return std::clamp((value - def->minimum)
        / std::max(1.0e-12, def->maximum - def->minimum), 0.0, 1.0);
}

double uiValueFromNormalized(clap_id id, double normalized)
{
    const auto* def = paramDef(id);
    if (!def) return 0.0;
    normalized = std::clamp(normalized, 0.0, 1.0);
    const double value = isLogUiParam(id)
        ? def->minimum * std::pow(def->maximum / def->minimum, normalized)
        : def->minimum + (def->maximum - def->minimum) * normalized;
    return clampValue(*def, value);
}

s3g::LowFrequencySynthParams publishedParamsSnapshot(const Plugin& p)
{
    s3g::LowFrequencySynthParams params;
    params.outputGainDb = static_cast<float>(paramValue(p, kOutputParamId));
    params.fundamental = static_cast<float>(paramValue(p, kFundamentalParamId));
    params.body = static_cast<float>(paramValue(p, kBodyParamId));
    params.loading = static_cast<float>(paramValue(p, kLoadingParamId));
    params.coupling = static_cast<float>(paramValue(p, kCouplingParamId));
    params.damping = static_cast<float>(paramValue(p, kDampingParamId));
    params.excitationPosition = static_cast<float>(paramValue(p, kExcitationParamId));
    params.upperModeLevel = static_cast<float>(paramValue(p, kHarmonicsParamId));
    params.filterCutoffHz = static_cast<float>(paramValue(p, kFilterCutoffParamId));
    params.filterResonance = static_cast<float>(paramValue(p, kFilterResonanceParamId));
    params.membraneDrive = static_cast<float>(paramValue(p, kDriveParamId));
    params.processedMix = static_cast<float>(paramValue(p, kProcessedMixParamId));
    params.valvePreamp = static_cast<float>(paramValue(p, kValvePreampParamId));
    params.attackSeconds = static_cast<float>(paramValue(p, kAttackParamId));
    params.decaySeconds = static_cast<float>(paramValue(p, kDecayParamId));
    params.sustain = static_cast<float>(paramValue(p, kSustainParamId));
    params.releaseSeconds = static_cast<float>(paramValue(p, kReleaseParamId));
    params.transposeSemitones = static_cast<float>(paramValue(p, kTransposeParamId));
    params.glideMs = static_cast<float>(paramValue(p, kGlideParamId));
    params.pitchTransientSemitones = static_cast<float>(paramValue(p, kPitchTransientParamId));
    params.amplitudeMotionClock = static_cast<float>(paramValue(p, kFilterMotionClockParamId));
    params.amplitudeMotionDivision = static_cast<float>(paramValue(p, kFilterMotionDivisionParamId));
    params.amplitudeMotionRateHz = static_cast<float>(paramValue(p, kFilterMotionRateParamId));
    params.amplitudeMotionDepth = static_cast<float>(paramValue(p, kFilterMotionDepthParamId));
    params.amplitudeMotionPosition = static_cast<float>(
        paramValue(p, kAmplitudeMotionPositionParamId));
    params.shred = static_cast<float>(paramValue(p, kShredParamId));
    params.shredFeedback = static_cast<float>(paramValue(p, kShredFeedbackParamId));
    params.shredFeedbackToneLevel = static_cast<float>(
        paramValue(p, kShredFeedbackToneLevelParamId));
    params.shredMix = static_cast<float>(paramValue(p, kShredMixParamId));
    params.shredCircuit = static_cast<float>(paramValue(p, kShredCircuitParamId));
    params.shredColor = static_cast<float>(paramValue(p, kShredColorParamId));
    return params;
}

float randomUnit(uint32_t& state)
{
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return static_cast<float>(state & 0x00ffffffu) / 16777215.0f;
}

s3g::LowFrequencySynthParams safeRandomParams(const Plugin& p, uint32_t seed)
{
    auto result = publishedParamsSnapshot(p);
    result.fundamental = 0.82f + randomUnit(seed) * 0.18f;
    result.body = 0.48f + randomUnit(seed) * 0.48f;
    result.loading = 0.58f + randomUnit(seed) * 0.42f;
    result.coupling = 0.18f + randomUnit(seed) * 0.64f;
    result.excitationPosition = randomUnit(seed) * 0.92f;
    result.damping = 0.12f + randomUnit(seed) * 0.58f;
    result.attackSeconds = std::pow(10.0f,
        -3.1f + randomUnit(seed) * 2.0f);
    result.decaySeconds = 0.05f + randomUnit(seed) * 0.85f;
    result.sustain = 0.55f + randomUnit(seed) * 0.45f;
    result.releaseSeconds = 0.06f + randomUnit(seed) * 1.40f;
    result.glideMs = randomUnit(seed) * 320.0f;
    result.pitchTransientSemitones = randomUnit(seed) < 0.72f
        ? 0.0f : -2.0f + randomUnit(seed) * 8.0f;
    result.upperModeLevel = 0.05f + randomUnit(seed) * 0.31f;
    result.filterCutoffHz = std::pow(2.0f,
        std::log2(180.0f) + randomUnit(seed)
            * (std::log2(6200.0f) - std::log2(180.0f)));
    result.filterResonance = 0.04f + randomUnit(seed) * 0.32f;
    result.membraneDrive = 0.06f + randomUnit(seed) * 0.54f;
    result.processedMix = 0.16f + randomUnit(seed) * 0.48f;
    result.amplitudeMotionRateHz = std::pow(2.0f,
        std::log2(0.35f) + randomUnit(seed)
            * (std::log2(12.0f) - std::log2(0.35f)));
    result.amplitudeMotionDepth = randomUnit(seed) < 0.28f
        ? 0.0f : 0.18f + randomUnit(seed) * 0.62f;
    result.amplitudeMotionClock = randomUnit(seed) < 0.25f
        ? static_cast<float>(s3g::MotionClock::Free)
        : static_cast<float>(s3g::MotionClock::Transport);
    result.amplitudeMotionDivision = std::floor(
        6.0f + randomUnit(seed) * 10.0f);
    result.amplitudeMotionPosition = randomUnit(seed) < 0.5f
        ? static_cast<float>(s3g::AmplitudeMotionPosition::PreShred)
        : static_cast<float>(s3g::AmplitudeMotionPosition::PostShred);
    result.valvePreamp = 0.08f + randomUnit(seed) * 0.76f;
    result.shred = randomUnit(seed) < 0.42f
        ? 0.0f : 0.12f + randomUnit(seed) * 0.58f;
    result.shredFeedback = result.shred > 0.0f
        ? randomUnit(seed) * 0.52f : 0.0f;
    result.shredFeedbackToneLevel = result.shredFeedback > 0.0f
        ? 0.35f + randomUnit(seed) * 0.65f : 1.0f;
    result.shredMix = result.shred > 0.0f
        ? 0.12f + randomUnit(seed) * 0.50f : 0.0f;
    result.shredCircuit = result.shred > 0.0f
        ? std::floor(randomUnit(seed)
            * static_cast<float>(s3g::kBassShredCircuitCount))
        : static_cast<float>(s3g::BassShredCircuit::Shred);
    result.shredColor = 0.28f + randomUnit(seed) * 0.50f;
    return result;
}

class Editor final : public s3g::portable_gui::routing::Canvas {
public:
using Canvas = s3g::portable_gui::routing::Canvas;

    Plugin* _plugin;
    int _dragParam;
    int _factoryPresetIndex;
    clap_id _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    uint64_t _observedParamRevision;
    char _presetName[64];
#include "s3g_stereo_processor_editor.inc"
explicit Editor(Plugin& plugin) : Canvas(kGuiWidth,kGuiHeight) {_plugin = &plugin;
    _dragParam = -1;
    _openMenu = CLAP_INVALID_ID;
    _hoverMenuItem = -1;
    _menuItemCount = 0u;
    auto* p = static_cast<Plugin*>(_plugin);
    _observedParamRevision = p
        ? p->parameterRevision.load(std::memory_order_acquire) : 0u;
    _factoryPresetIndex = p ? s3g::lowFrequencySynthFactoryPresetIndex(
        publishedParamsSnapshot(*p)) : 0;
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        _factoryPresetIndex >= 0
            ? s3g::lowFrequencySynthFactoryPresetInfo(
                static_cast<uint32_t>(_factoryPresetIndex)).name
            : "CUSTOM");
}

void applyFactoryPreset(int index)  {
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    index = std::clamp(index, 0,
        static_cast<int>(s3g::kLowFrequencySynthFactoryPresetCount - 1u));
    if (!queueGuiParams(*p, s3g::lowFrequencySynthFactoryPreset(
            static_cast<uint32_t>(index)))) {
        beep();
        return;
    }
    _factoryPresetIndex = index;
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        s3g::lowFrequencySynthFactoryPresetInfo(
            static_cast<uint32_t>(index)).name);
    setNeedsDisplay(true);

}

void markCustomPreset()  {
    _factoryPresetIndex = -1;
    std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");

}

drawing::Rect openMenuRect()  {
    drawing::Rect anchor = drawing::Rect{};
    if (_openMenu == kFactoryPresetMenuId) {
        const auto band = drawing::encoderTitleBand(
            kGuiWidth, kGuiHeight);
        anchor = drawing::cocoaRect(band.presetMenu);
    } else {
        const auto row = std::find_if(kUiRows.begin(), kUiRows.end(),
            [=](const SynthUiRow& candidate) {
                return candidate.id == _openMenu;
            });
        if (row == kUiRows.end() || !isUiMenuParam(row->id)) {
            return drawing::Rect{};
        }
        anchor = drawing::makeRect(
            s3g::gui_layout::processorControlX(row->panelX), row->y - 1.0,
            s3g::gui_layout::processorMenuWidth(row->panelWidth), 15.0);
    }
    const double menuHeight = 18.0 * _menuItemCount;
    double menuY = drawing::maxY(anchor) + 2.0;
    if (menuY + menuHeight > kGuiHeight) {
        menuY = anchor.origin.y - 2.0 - menuHeight;
    }
    return drawing::makeRect(anchor.origin.x, menuY,
        anchor.size.width, menuHeight);

}

void drawOpenMenu_style(drawing::Attrs  attrs, const drawing::Style& style)  {
    if (_openMenu == CLAP_INVALID_ID || _menuItemCount == 0u) return;
    drawing::String  items[32] {};
    const uint32_t count = std::min<uint32_t>(_menuItemCount, 32u);
    if (_openMenu == kFactoryPresetMenuId) {
        for (uint32_t index = 0u; index < count; ++index) {
            items[index] = drawing::stringFromUtf8(s3g::lowFrequencySynthFactoryPresetInfo(index).name);
        }
    } else {
        for (uint32_t index = 0u; index < count; ++index) {
            char text[64] {};
            if (paramsValueToText(
                    &static_cast<Plugin*>(_plugin)->plugin, _openMenu,
                    static_cast<double>(index), text, sizeof(text))) {
                items[index] = drawing::stringFromUtf8(text);
            } else {
                items[index] = "—";
            }
        }
    }
    const int selected = _openMenu == kFactoryPresetMenuId
        ? _factoryPresetIndex
        : static_cast<int>(std::lround(paramValue(
            *static_cast<Plugin*>(_plugin), _openMenu)));
    drawing::drawDropdownMenu(openMenuRect(), 18.0,
        items, count, selected, _hoverMenuItem, attrs, style);

}

void service() override {
    
    auto* p = static_cast<Plugin*>(_plugin);
    if (p) {
        const uint64_t revision = p->parameterRevision.load(
            std::memory_order_acquire);
        if (revision != _observedParamRevision) {
            _observedParamRevision = revision;
            _factoryPresetIndex = s3g::lowFrequencySynthFactoryPresetIndex(
                publishedParamsSnapshot(*p));
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                _factoryPresetIndex >= 0
                    ? s3g::lowFrequencySynthFactoryPresetInfo(
                        static_cast<uint32_t>(_factoryPresetIndex)).name
                    : "CUSTOM");
        }
    }
    if (true && p && true) {
        setNeedsDisplay(true);
    }

}

void drawOriginal(drawing::Rect dirty)  {
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    drawing::Style style;
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs  titleAttrs = drawing::softTitleAttrs();
    drawing::Attrs  labelAttrs = drawing::softLabelAttrs();
    drawing::Attrs  valueAttrs = drawing::softValueAttrs();
    const auto titleBand = drawing::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    drawing::drawEncoderTitleBand(
        "s3g PROCESSOR LF SYNTH",
        drawing::stringFromUtf8(_presetName),
        drawing::peakDbText(
            p->outputPeak.load(std::memory_order_relaxed)),
        titleBand, titleAttrs, labelAttrs, valueAttrs, style);

    const auto drawPanel = [&](drawing::String  name, const layout::Panel& panel) {
        const drawing::Rect rect = drawing::cocoaRect(panel.frame);
        drawing::drawPanelFrame(rect.origin.x, rect.origin.y,
            rect.size.width, rect.size.height, style);
        drawing::drawPanelHeader(name, true, rect.origin.x,
            rect.origin.y, rect.size.width,
            s3g::gui_layout::kStandardMetrics.headerHeight,
            labelAttrs, style);
    };
    drawPanel("OUTPUT", kOutputPanel);
    drawPanel("MODAL CORE", kModalPanel);
    drawPanel("PITCH", kPitchPanel);
    drawPanel("ENVELOPE", kEnvelopePanel);
    drawPanel("TONE", kTonePanel);
    drawPanel("STEREO SHRED", kShredPanel);
    drawPanel("AMPLITUDE LFO", kMotionPanel);
    drawPanel("ROUTING", kRoutingPanel);

    for (const auto& row : kUiRows) {
        const double value = paramValue(*p, row.id);
        char text[64] {};
        paramsValueToText(&p->plugin, row.id, value, text, sizeof(text));
        drawing::String  label = drawing::stringFromUtf8(row.label);
        drawing::String  display = drawing::stringFromUtf8(text);
        if (isUiMenuParam(row.id)) {
            drawing::drawProcessorMenu(label, display, row.y,
                row.panelX, row.panelWidth,
                labelAttrs, valueAttrs, style);
        } else {
            drawing::drawProcessorSlider(label, display,
                static_cast<double>(uiNormalizedValue(row.id, value)),
                row.y, row.panelX, row.panelWidth,
                labelAttrs, valueAttrs, style);
        }
    }
    drawOpenMenu_style(valueAttrs, style);

}

void updateDraggedParam(drawing::Point point)  {
    if (_dragParam <= 0) return;
    const clap_id id = static_cast<clap_id>(_dragParam);
    const auto row = std::find_if(kUiRows.begin(), kUiRows.end(),
        [=](const SynthUiRow& candidate) { return candidate.id == id; });
    if (row == kUiRows.end()) return;
    const double controlX = s3g::gui_layout::processorControlX(row->panelX);
    const double trackWidth =
        s3g::gui_layout::processorTrackWidth(row->panelWidth);
    const double normalized = std::clamp(
        (point.x - controlX) / trackWidth, 0.0, 1.0);
    auto* p = static_cast<Plugin*>(_plugin);
    queueGuiParamValue(*p, id, uiValueFromNormalized(id, normalized));
    setNeedsDisplay(true);

}

void mouseDown(drawing::Event*  event)  {
    const drawing::Point point = convertPoint_fromView(event->locationInWindow(), nullptr);
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    const auto titleBand = drawing::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    if (_openMenu != CLAP_INVALID_ID) {
        const int hit = drawing::dropdownHitIndex(point,
            openMenuRect(), 18.0, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == kFactoryPresetMenuId) {
                applyFactoryPreset(hit);
            } else {
                const clap_id menuParam = _openMenu;
                queueGuiParamGesture(*p, menuParam,
                    static_cast<double>(hit));
                if (menuParam != kMidiReceiveParamId) {
                    markCustomPreset();
                }
            }
        }
        _openMenu = CLAP_INVALID_ID;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point,
            drawing::cocoaRect(titleBand.presetMenu))) {
        _openMenu = kFactoryPresetMenuId;
        _hoverMenuItem = -1;
        _menuItemCount = s3g::kLowFrequencySynthFactoryPresetCount;
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point,
            drawing::cocoaRect(titleBand.loadButton))) {
        drawing::String name;
        if (presetDialog(false, &name)) {
            markCustomPreset();
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                !name.empty() ? name.c_str() : "CUSTOM");
        } else {
            beep();
        }
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point,
            drawing::cocoaRect(titleBand.saveButton))) {
        drawing::String name;
        if (presetDialog(true, &name)) {
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                !name.empty() ? name.c_str() : "CUSTOM");
        } else {
            beep();
        }
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point,
            drawing::cocoaRect(titleBand.randomButton))) {
        if (queueGuiParams(*p, safeRandomParams(*p, drawing::randomBits()))) {
            markCustomPreset();
        } else {
            beep();
        }
        setNeedsDisplay(true);
        return;
    }

    for (const auto& row : kUiRows) {
        const drawing::Rect hit = drawing::makeRect(
            row.panelX + s3g::gui_layout::kStandardMetrics.hitInset,
            row.y - 9.0,
            row.panelWidth
                - s3g::gui_layout::kStandardMetrics.hitInset * 2.0,
            s3g::gui_layout::kStandardMetrics.hitHeight);
        if (!drawing::contains(point, hit)) continue;
        if (isUiMenuParam(row.id)) {
            _openMenu = row.id;
            _hoverMenuItem = -1;
            _menuItemCount = uiMenuItemCount(row.id);
            setNeedsDisplay(true);
            return;
        }
        double defaultValue = 0.0;
        if (drawing::sliderDoubleClickDefault(
                event, &p->plugin, row.id, &defaultValue)) {
            queueGuiParamGesture(*p, row.id, defaultValue);
            _dragParam = -1;
        } else {
            _dragParam = static_cast<int>(row.id);
            queueGuiParamGestureBegin(*p, row.id);
            updateDraggedParam(point);
        }
        if (row.id != kOutputParamId && row.id != kMidiReceiveParamId) {
            markCustomPreset();
        }
        return;
    }

}

void mouseDragged(drawing::Event*  event)  {
    if (_dragParam > 0) {
        updateDraggedParam(convertPoint_fromView(event->locationInWindow(), nullptr));
    }

}

void mouseUp(drawing::Event*  event)  {
    (void)event;
    if (_dragParam > 0) {
        queueGuiParamGestureEnd(*static_cast<Plugin*>(_plugin),
            static_cast<clap_id>(_dragParam));
    }
    _dragParam = -1;

}

void mouseMoved(drawing::Event*  event)  {
    if (_openMenu == CLAP_INVALID_ID) return;
    const drawing::Point point = convertPoint_fromView(event->locationInWindow(), nullptr);
    const int hover = drawing::dropdownHitIndex(
        point, openMenuRect(), 18.0, _menuItemCount);
    if (hover != _hoverMenuItem) {
        _hoverMenuItem = hover;
        setNeedsDisplay(true);
    }

}

};
}
s3g::portable_gui::foundation::ContentView* makePortableCanvas(Plugin& p) { return new stereo_processor_canvas::Editor(p); }
#include "s3g_clap_canvas_gui.inc"
