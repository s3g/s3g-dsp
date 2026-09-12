// Cocoa geometry, drawing order and interaction maps retained.
namespace midi_tool_canvas {
namespace drawing = s3g::portable_gui::midi_drawing;
namespace foundation = s3g::portable_gui::foundation;
constexpr double kPanelHeaderHeight = 22.0;
constexpr int kFactoryPresetMenuId =
    static_cast<int>(kParamCount);

struct GuiParamBatch {
    static_assert(kParamCount * 3u < kGuiParamQueueCapacity,
        "Relay preset transaction must fit the GUI parameter queue");
    std::array<s3g::clap_gui::ParamEvent, kParamCount * 3u> events {};
    uint32_t count = 0u;

    bool add(uint32_t index, double value)
    {
        if (index >= kParamCount || count + 3u > events.size()) return false;
        const clap_id id = paramIdForIndex(index);
        events[count++] = { s3g::clap_gui::ParamEventKind::GestureBegin,
            id, 0.0 };
        events[count++] = { s3g::clap_gui::ParamEventKind::Value,
            id, value };
        events[count++] = { s3g::clap_gui::ParamEventKind::GestureEnd,
            id, 0.0 };
        return true;
    }

    bool submit(Plugin& plugin)
    {
        if (count == 0u || !plugin.guiParamEvents.pushBatch(
                events.data(), count)) return false;
        requestGuiParamService(plugin);
        return true;
    }
};

bool queueFactoryPreset(Plugin& plugin, const Config& config)
{
    GuiParamBatch batch;
    for (uint32_t index = 0u; index < kGlobalParamCount; ++index) {
        if (!batch.add(index, configValue(config, index))) return false;
    }
    for (uint32_t relay = 0u; relay < s3g::relay::kRelayCount; ++relay) {
        for (uint32_t local = 0u; local < kRelayParamCount; ++local) {
            if (local == kRelayChannel || local == kRelayNote
                || local == kRelayCcA || local == kRelayCcB) continue;
            const uint32_t index = kGlobalParamCount
                + relay * kRelayParamCount + local;
            if (!batch.add(index, configValue(config, index))) return false;
        }
    }
    return batch.submit(plugin);
}

int detectedFactoryPreset(const Plugin& plugin)
{
    for (uint32_t preset = 0u; preset < kFactoryPresetCount; ++preset) {
        const Config candidate = factoryPresetConfig(preset);
        bool matches = true;
        for (uint32_t index = 0u; index < kGlobalParamCount && matches;
             ++index) {
            matches = std::abs(publishedValue(plugin, index)
                - configValue(candidate, index)) < 1.0e-9;
        }
        for (uint32_t relay = 0u;
             relay < s3g::relay::kRelayCount && matches; ++relay) {
            for (uint32_t local = 0u; local < kRelayParamCount; ++local) {
                if (local == kRelayChannel || local == kRelayNote
                    || local == kRelayCcA || local == kRelayCcB) continue;
                const uint32_t index = kGlobalParamCount
                    + relay * kRelayParamCount + local;
                if (std::abs(publishedValue(plugin, index)
                        - configValue(candidate, index)) >= 1.0e-9) {
                    matches = false;
                    break;
                }
            }
        }
        if (matches) return static_cast<int>(preset);
    }
    return -1;
}

drawing::Rect graphPanelRect() { return drawing::makeRect(18.0, 42.0, 760.0, 500.0); }
drawing::Rect conductPanelRect() { return drawing::makeRect(790.0, 42.0, 432.0, 274.0); }
drawing::Rect pitchPanelRect() { return drawing::makeRect(790.0, 328.0, 432.0, 214.0); }
drawing::Rect climatePanelRect() { return drawing::makeRect(790.0, 554.0, 432.0, 328.0); }
drawing::Rect relayPanelRect() { return drawing::makeRect(18.0, 554.0, 760.0, 328.0); }

drawing::Rect globalRowRect(uint32_t index)
{
    if (index < 8u)
        return drawing::makeRect(806.0, 72.0 + static_cast<double>(index) * 29.0,
            400.0, 22.0);
    if (index < 16u)
        return drawing::makeRect(806.0,
            584.0 + static_cast<double>(index - 8u) * 28.0, 400.0, 22.0);
    if (index <= kScaleRange)
        return drawing::makeRect(806.0,
            362.0 + static_cast<double>(index - 16u) * 34.0, 400.0, 22.0);
    if (index == kLatticeDepth)
        return drawing::makeRect(806.0, 808.0, 400.0, 22.0);
    if (index == kDealerLaw)
        return drawing::makeRect(806.0, 836.0, 400.0, 22.0);
    if (index >= kMidiInputMode && index <= kMidiInputDecay) {
        const uint32_t local = index - kMidiInputMode;
        const uint32_t column = local >= 4u ? 1u : 0u;
        const uint32_t row = local % 4u;
        return drawing::makeRect(34.0 + static_cast<double>(column) * 390.0,
            126.0 + static_cast<double>(row) * 48.0, 338.0, 24.0);
    }
    return drawing::Rect{};
}

drawing::Rect relayRowRect(uint32_t local)
{
    uint32_t column = 0u;
    uint32_t row = 0u;
    switch (local) {
    case kRelayEnabled: row = 0u; break;
    case kRelayChannel: row = 1u; break;
    case kRelayNote: row = 2u; break;
    case kRelayThreshold: row = 3u; break;
    case kRelayBias: column = 1u; row = 0u; break;
    case kRelayRefractory: column = 1u; row = 1u; break;
    case kRelayFeedback: column = 1u; row = 2u; break;
    case kRelayGate: column = 1u; row = 3u; break;
    case kRelayTopology: column = 1u; row = 4u; break;
    case kRelayPitchMode: column = 1u; row = 5u; break;
    case kRelayArticulation: column = 1u; row = 6u; break;
    case kRelayCcA: row = 0u; break;
    case kRelayCcASource: row = 1u; break;
    case kRelayCcAMin: row = 2u; break;
    case kRelayCcAMax: row = 3u; break;
    case kRelayCcACurve: row = 4u; break;
    case kRelayCcASlew: row = 5u; break;
    case kRelayCcB: column = 1u; row = 0u; break;
    case kRelayCcBSource: column = 1u; row = 1u; break;
    case kRelayCcBMin: column = 1u; row = 2u; break;
    case kRelayCcBMax: column = 1u; row = 3u; break;
    case kRelayCcBCurve: column = 1u; row = 4u; break;
    case kRelayCcBSlew: column = 1u; row = 5u; break;
    default: break;
    }
    return drawing::makeRect(34.0 + static_cast<double>(column) * 390.0,
        606.0 + static_cast<double>(row) * 36.0, 338.0, 24.0);
}

drawing::Rect relayTabRect(uint32_t relay)
{
    return drawing::makeRect(114.0 + static_cast<double>(relay) * 80.0,
        557.0, 72.0, 16.0);
}

drawing::Rect relayModeTabRect(uint32_t mode)
{
    return drawing::makeRect(34.0 + static_cast<double>(mode) * 104.0,
        580.0, 96.0, 16.0);
}

inline constexpr std::array<uint32_t, 11u> kRelayVoiceControls {{
    kRelayEnabled, kRelayChannel, kRelayNote, kRelayThreshold,
    kRelayBias, kRelayRefractory, kRelayFeedback, kRelayGate,
    kRelayTopology, kRelayPitchMode, kRelayArticulation,
}};

inline constexpr std::array<uint32_t, 12u> kRelayCcControls {{
    kRelayCcA, kRelayCcASource, kRelayCcAMin, kRelayCcAMax,
    kRelayCcACurve, kRelayCcASlew,
    kRelayCcB, kRelayCcBSource, kRelayCcBMin, kRelayCcBMax,
    kRelayCcBCurve, kRelayCcBSlew,
}};

drawing::Rect crystallizeRect() { return drawing::makeRect(1107.0, 45.0, 104.0, 16.0); }

drawing::Rect visualPageTabRect(uint32_t page)
{
    return drawing::makeRect(392.0 + static_cast<double>(page) * 75.0,
        45.0, 69.0, 16.0);
}

drawing::Rect formPlaneButtonRect(uint32_t item)
{
    return drawing::makeRect(34.0 + static_cast<double>(item) * 64.0,
        72.0, 58.0, 16.0);
}

constexpr double kLearningRowHeaderX = 34.0;
constexpr double kLearningRowHeaderWidth = 64.0;
constexpr double kLearningMatrixX = kLearningRowHeaderX
    + kLearningRowHeaderWidth;
constexpr double kLearningColumnHeaderY = 124.0;
constexpr double kLearningColumnHeaderHeight = 30.0;
constexpr double kLearningMatrixY = kLearningColumnHeaderY
    + kLearningColumnHeaderHeight;
constexpr double kLearningCellSize = 80.0;

drawing::Rect learningCellRect(uint32_t target, uint32_t source)
{
    return drawing::makeRect(
        kLearningMatrixX + static_cast<double>(source) * kLearningCellSize,
        kLearningMatrixY + static_cast<double>(target) * kLearningCellSize,
        kLearningCellSize, kLearningCellSize);
}

drawing::Rect learningModeRect(uint32_t mode)
{
    return drawing::makeRect(342.0 + static_cast<double>(mode) * 62.0,
        74.0, 56.0, 16.0);
}

drawing::Rect consoleFilterRect(uint32_t filter)
{
    return drawing::makeRect(34.0 + static_cast<double>(filter) * 57.0,
        72.0, 51.0, 16.0);
}

drawing::Rect consoleClearRect() { return drawing::makeRect(211.0, 72.0, 51.0, 16.0); }

bool parameterIsBinary(uint32_t index)
{
    return index == kEnabled || index == kFreeze
        || (index >= kGlobalParamCount
            && (index - kGlobalParamCount) % kRelayParamCount
                == kRelayEnabled);
}

bool parameterIsMenu(uint32_t index)
{
    if (index == kClockRate || index == kCcRate
        || index == kLatticeDepth || index == kDealerLaw
        || (index >= kMidiInputMode && index <= kMidiInputResponse)
        || index == kMidiInputPolarity
        || (index >= kScaleRoot && index <= kScaleRange)) return true;
    if (index < kGlobalParamCount) return false;
    const uint32_t local = (index - kGlobalParamCount) % kRelayParamCount;
    return local == kRelayChannel || local == kRelayTopology
        || local == kRelayPitchMode || local == kRelayArticulation
        || local == kRelayCcASource || local == kRelayCcBSource;
}

uint32_t parameterMenuCount(uint32_t index)
{
    if (index == kClockRate) return 7u;
    if (index == kCcRate) return 6u;
    if (index == kScaleRoot) return 12u;
    if (index == kScaleOctave) return 9u;
    if (index == kScale) return s3g::kMusicalScaleCount;
    if (index == kScaleRange) return 4u;
    if (index == kLatticeDepth) return 3u;
    if (index == kDealerLaw) return s3g::relay::kDealerLawCount;
    if (index == kMidiInputMode) return 4u;
    if (index == kMidiInputChannel) return 17u;
    if (index == kMidiInputMap) return 4u;
    if (index == kMidiInputResponse) return 2u;
    if (index == kMidiInputPolarity) return 2u;
    if (index < kGlobalParamCount) return 0u;
    const uint32_t local = (index - kGlobalParamCount) % kRelayParamCount;
    if (local == kRelayChannel) return 16u;
    if (local == kRelayTopology) return 4u;
    if (local == kRelayPitchMode) return 2u;
    if (local == kRelayArticulation) return 4u;
    if (local == kRelayCcASource || local == kRelayCcBSource)
        return s3g::relay::kCcSourceCount;
    return 0u;
}

uint32_t parameterMenuColumns(uint32_t index)
{
    if (index == kScale) return 4u;
    if (index == kScaleRoot) return 2u;
    if (index == kMidiInputChannel) return 2u;
    return index >= kGlobalParamCount
            && (index - kGlobalParamCount) % kRelayParamCount == kRelayChannel
        ? 2u : 1u;
}

double parameterMenuValue(uint32_t index, uint32_t item)
{
    if (index == kScale)
        return static_cast<double>(
            s3g::musicalScaleValueForMenuIndex(item));
    if (index == kScaleOctave)
        return static_cast<double>(static_cast<int32_t>(item) - 1);
    if (index == kScaleRange) return static_cast<double>(item + 1u);
    if (index >= kGlobalParamCount
        && (index - kGlobalParamCount) % kRelayParamCount == kRelayChannel)
        return static_cast<double>(item + 1u);
    return static_cast<double>(item);
}

int parameterMenuSelection(uint32_t index, const Plugin& plugin)
{
    int selected = static_cast<int>(std::lround(publishedValue(plugin, index)));
    if (index == kScale)
        return static_cast<int>(s3g::musicalScaleMenuIndexForValue(
            static_cast<uint32_t>(selected)));
    if (index == kScaleOctave) ++selected;
    if (index == kScaleRange) --selected;
    if (index >= kGlobalParamCount
        && (index - kGlobalParamCount) % kRelayParamCount == kRelayChannel)
        --selected;
    return selected;
}

drawing::String  parameterMenuItem(uint32_t index, uint32_t item)
{
    if (index == kClockRate) {
        static drawing::String  const items[7] = {
            "1 TICK / 4 BEATS", "1 TICK / 2 BEATS", "1 TICK / BEAT",
            "2 TICKS / BEAT", "4 TICKS / BEAT", "8 TICKS / BEAT",
            "16 TICKS / BEAT",
        };
        return items[std::min<uint32_t>(item, 6u)];
    }
    if (index == kCcRate) {
        return drawing::stringFormat("EVERY %u TICK%@",
            s3g::relay::ccEveryTicks(item),
            s3g::relay::ccEveryTicks(item) == 1u ? "" : "S");
    }
    if (index == kScaleRoot)
        return drawing::stringFromUtf8(scaleRootName(item));
    if (index == kScaleOctave)
        return drawing::stringFormat("OCTAVE %d",
            static_cast<int>(item) - 1);
    if (index == kScale)
        return drawing::stringFromUtf8(s3g::musicalScaleDefinition(
            s3g::musicalScaleValueForMenuIndex(item)).name);
    if (index == kScaleRange)
        return drawing::stringFormat("%u OCTAVE%@", item + 1u,
            item == 0u ? "" : "S");
    if (index == kLatticeDepth) {
        static drawing::String  const items[3] = {
            "SHEET", "2 PLANES", "4 PLANES",
        };
        return items[std::min<uint32_t>(item, 2u)];
    }
    if (index == kDealerLaw)
        return drawing::uppercaseString(drawing::stringFromUtf8(s3g::relay::dealerLawName(
            static_cast<DealerLaw>(std::min<uint32_t>(item,
                s3g::relay::kDealerLawCount - 1u)))));
    if (index == kMidiInputMode)
        return drawing::uppercaseString(drawing::stringFromUtf8(s3g::relay::midiInputModeName(
            static_cast<MidiInputMode>(std::min<uint32_t>(item, 3u)))));
    if (index == kMidiInputChannel)
        return item == 0u ? "OMNI"
            : drawing::stringFormat("CHANNEL %u", item);
    if (index == kMidiInputMap)
        return drawing::uppercaseString(drawing::stringFromUtf8(s3g::relay::midiInputMapName(
            static_cast<MidiInputMap>(std::min<uint32_t>(item, 3u)))));
    if (index == kMidiInputResponse)
        return item == 0u ? "IMPULSE" : "GATE";
    if (index == kMidiInputPolarity)
        return item == 0u ? "EXCITATORY" : "SIGNED";
    const uint32_t local = index >= kGlobalParamCount
        ? (index - kGlobalParamCount) % kRelayParamCount : kRelayParamCount;
    if (local == kRelayChannel)
        return drawing::stringFormat("CHANNEL %u", item + 1u);
    if (local == kRelayTopology) {
        static drawing::String  const items[4] = {
            "LOCAL", "CROSS", "DIFFUSE", "ROAMING",
        };
        return items[std::min<uint32_t>(item, 3u)];
    }
    if (local == kRelayPitchMode)
        return item == 0u ? "FIXED" : "SCALE LOGIC";
    if (local == kRelayArticulation) {
        static drawing::String  const items[4] = {
            "RESTART", "HOLD", "EXTEND", "STACK",
        };
        return items[std::min<uint32_t>(item, 3u)];
    }
    if (local == kRelayCcASource || local == kRelayCcBSource)
        return drawing::stringFromUtf8(s3g::relay::ccSourceName(
            static_cast<CcSource>(std::min<uint32_t>(
                item, s3g::relay::kCcSourceCount - 1u))));
    return "";
}

void parameterPanelGeometry(uint32_t index, double& panelX,
    double& panelWidth)
{
    if (index < 8u) {
        panelX = conductPanelRect().origin.x;
        panelWidth = conductPanelRect().size.width;
        return;
    }
    if (index < kGlobalParamCount) {
        if (index >= kMidiInputMode && index <= kMidiInputDecay) {
            const bool left = index < kMidiInputCc;
            panelX = left ? 18.0 : 408.0;
            panelWidth = 370.0;
            return;
        }
        const bool climate = index < 16u || index == kLatticeDepth
            || index == kDealerLaw;
        panelX = climate ? climatePanelRect().origin.x
                         : pitchPanelRect().origin.x;
        panelWidth = climate ? climatePanelRect().size.width
                             : pitchPanelRect().size.width;
        return;
    }
    const uint32_t local = (index - kGlobalParamCount) % kRelayParamCount;
    const bool left = local == kRelayEnabled || local == kRelayChannel
        || local == kRelayNote || local == kRelayThreshold
        || local == kRelayCcA
        || (local >= kRelayCcASource && local <= kRelayCcASlew);
    panelX = left ? 18.0 : 408.0;
    panelWidth = 370.0;
}

drawing::Rect parameterMenuBoxRect(uint32_t index)
{
    double panelX = 0.0;
    double panelWidth = 0.0;
    parameterPanelGeometry(index, panelX, panelWidth);
    const drawing::Rect row = index < kGlobalParamCount
        ? globalRowRect(index)
        : relayRowRect((index - kGlobalParamCount) % kRelayParamCount);
    return drawing::makeRect(
        static_cast<double>(s3g::gui_layout::processorControlX(panelX)),
        row.origin.y + 1.0,
        static_cast<double>(s3g::gui_layout::processorMenuWidth(panelWidth)),
        15.0);
}

drawing::Rect parameterSliderTrackRect(uint32_t index)
{
    double panelX = 0.0;
    double panelWidth = 0.0;
    parameterPanelGeometry(index, panelX, panelWidth);
    const drawing::Rect row = index < kGlobalParamCount
        ? globalRowRect(index)
        : relayRowRect((index - kGlobalParamCount) % kRelayParamCount);
    return drawing::makeRect(
        static_cast<double>(s3g::gui_layout::processorControlX(panelX)),
        row.origin.y + 3.0,
        static_cast<double>(s3g::gui_layout::processorTrackWidth(panelWidth)),
        9.0);
}

drawing::Rect parameterInteractionRect(uint32_t index)
{
    const drawing::Rect row = index < kGlobalParamCount
        ? globalRowRect(index)
        : relayRowRect((index - kGlobalParamCount) % kRelayParamCount);
    if (parameterIsBinary(index))
        return drawing::makeRect(drawing::maxX(row) - 68.0, row.origin.y,
            68.0, row.size.height);
    if (parameterIsMenu(index)) {
        const drawing::Rect box = parameterMenuBoxRect(index);
        return drawing::inset(box, -4.0, -4.0);
    }
    const drawing::Rect track = parameterSliderTrackRect(index);
    return drawing::makeRect(track.origin.x - 5.0, row.origin.y,
        track.size.width + 10.0, row.size.height);
}

drawing::Point clusterPoint(uint32_t cluster)
{
    static constexpr std::array<double, 4u> x {{ 270.0, 526.0, 270.0, 526.0 }};
    static constexpr std::array<double, 4u> y {{ 182.0, 182.0, 404.0, 404.0 }};
    return drawing::makePoint(
        static_cast<double>(x[cluster % s3g::relay::kClusterCount]),
        static_cast<double>(y[cluster % s3g::relay::kClusterCount]));
}

drawing::Point nodePoint(uint32_t node)
{
    constexpr double radius = 34.0;
    const uint32_t cluster = node / s3g::relay::kNodesPerCluster;
    const uint32_t local = node % s3g::relay::kNodesPerCluster;
    const double angle = -s3g::relay::kPi * 0.5
        + s3g::relay::kPi * 2.0 * static_cast<double>(local)
            / static_cast<double>(s3g::relay::kNodesPerCluster);
    const drawing::Point center = clusterPoint(cluster);
    return drawing::makePoint(
        center.x + static_cast<double>(std::cos(angle) * radius),
        center.y + static_cast<double>(std::sin(angle) * radius));
}

drawing::Point relayPoint(uint32_t relay)
{
    static constexpr std::array<double, 8u> x {{
        80.0, 716.0, 80.0, 716.0, 80.0, 716.0, 80.0, 716.0,
    }};
    static constexpr std::array<double, 8u> y {{
        145.0, 145.0, 368.0, 368.0, 212.0, 212.0, 435.0, 435.0,
    }};
    return drawing::makePoint(static_cast<double>(x[relay % 8u]),
        static_cast<double>(y[relay % 8u]));
}

drawing::Rect receptorStationRect(uint32_t cluster)
{
    const bool right = (cluster & 1u) != 0u;
    const bool lower = cluster >= 2u;
    return drawing::makeRect(right ? 630.0 : 38.0, lower ? 315.0 : 92.0,
        128.0, 154.0);
}

drawing::Point receptorPathOrigin(uint32_t relay)
{
    const drawing::Point receptor = relayPoint(relay);
    return drawing::makePoint(
        (relay % s3g::relay::kClusterCount & 1u) != 0u ? 630.0 : 166.0,
        receptor.y);
}

drawing::Rect relayGlyphRect(uint32_t relay)
{
    const drawing::Point point = relayPoint(relay);
    return drawing::makeRect(point.x - 14.0, point.y - 14.0, 28.0, 28.0);
}

void drawLine(drawing::Point a, drawing::Point b, VSTGUI::CColor  color, double width = 1.0)
{
    drawing::Path  path = drawing::Path::bezierPath();
    path.moveToPoint(a);
    path.lineToPoint(b);
    path.setLineWidth(width);
    drawing::setStroke(color);
    path.stroke();
}

void drawDirectedConnection(drawing::Point source, drawing::Point target, double weight,
    VSTGUI::CColor  positive, VSTGUI::CColor  negative)
{
    const double dx = target.x - source.x;
    const double dy = target.y - source.y;
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length <= 1.0) return;
    const double ux = dx / length;
    const double uy = dy / length;
    const double nx = -uy;
    const double ny = ux;
    constexpr double nodeClearance = 9.0;
    constexpr double laneOffset = 2.2;
    const drawing::Point start = drawing::makePoint(
        source.x + ux * nodeClearance + nx * laneOffset,
        source.y + uy * nodeClearance + ny * laneOffset);
    const drawing::Point end = drawing::makePoint(
        target.x - ux * nodeClearance + nx * laneOffset,
        target.y - uy * nodeClearance + ny * laneOffset);
    const double strength = static_cast<double>(std::min(
        std::abs(weight) / 1.3, 1.0));
    VSTGUI::CColor  ink = drawing::withAlpha((weight >= 0.0 ? positive : negative), 0.46 + strength * 0.42);
    drawLine(start, end, ink, 0.58 + strength * 0.72);

    constexpr double arrowLength = 4.2;
    constexpr double arrowWidth = 2.4;
    const drawing::Point base = drawing::makePoint(
        end.x - ux * arrowLength, end.y - uy * arrowLength);
    drawLine(end, drawing::makePoint(base.x + nx * arrowWidth,
        base.y + ny * arrowWidth), ink, 0.58 + strength * 0.72);
    drawLine(end, drawing::makePoint(base.x - nx * arrowWidth,
        base.y - ny * arrowWidth), ink, 0.58 + strength * 0.72);
}

void drawParameter(Plugin& plugin, uint32_t index, drawing::Rect rect,
    double panelX, double panelWidth)
{
    const ParamSpec spec = paramSpec(index);
    const double value = publishedValue(plugin, index);
    const double normalized = spec.maximum > spec.minimum
        ? std::clamp((value - spec.minimum) / (spec.maximum - spec.minimum),
            0.0, 1.0) : 0.0;
    char buffer[64] {};
    paramsValueToText(&plugin.plugin, spec.id, value, buffer, sizeof(buffer));
    drawing::String  label = drawing::uppercaseString(drawing::stringFromUtf8(spec.name));
    drawing::Attrs  labels = drawing::softLabelAttrs();
    drawing::Attrs  values = drawing::softValueAttrs();
    const auto style = drawing::softTextStyle();
    int32_t climateTrait = -1;
    if (index == kActivity) climateTrait = 0;
    else if (index == kCoupling) climateTrait = 1;
    else if (index == kHierarchy) climateTrait = 2;
    else if (index == kContrast) climateTrait = 3;
    drawing::String  displayValue = drawing::stringFromUtf8(buffer);
    double effective = value;
    if (climateTrait >= 0) {
        effective = plugin.visualEffectiveConduct[
            static_cast<std::size_t>(climateTrait)].load(
                std::memory_order_relaxed);
        displayValue = drawing::stringFormat("%.0f→%.0f%%",
            value * 100.0, effective * 100.0);
    }
    if (parameterIsBinary(index)) {
        drawing::drawToggle(label, value >= 0.5,
            rect.origin.y + 2.0, labels, values, style,
            rect.origin.x, drawing::maxX(rect) - 64.0, 64.0);
    } else if (parameterIsMenu(index)) {
        drawing::drawProcessorMenu(label,
            displayValue, rect.origin.y + 2.0,
            panelX, panelWidth, labels, values, style);
    } else {
        drawing::drawProcessorSlider(label,
            displayValue,
            static_cast<double>(normalized), rect.origin.y + 2.0,
            panelX, panelWidth, labels, values, style);
        if (climateTrait >= 0) {
            const drawing::Rect track = parameterSliderTrackRect(index);
            const double marker = track.origin.x
                + static_cast<double>(std::clamp(effective, 0.0, 1.0))
                    * track.size.width;
            VSTGUI::CColor  ink = effective >= value
                ? drawing::color(0x9a6b50)
                : drawing::color(0x587d81);
            drawLine(drawing::makePoint(marker, track.origin.y - 2.0),
                drawing::makePoint(marker, drawing::maxY(track) + 2.0), ink, 1.4);
        }
    }
}

class Editor final : public s3g::portable_gui::routing::Canvas {
public:
using Canvas = s3g::portable_gui::routing::Canvas;

    Plugin* _plugin;
    int _selectedRelay;
    int _relayPage;
    int _dragParam;
    int _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    int _visualPage;
    int _matrixMode;
    int _selectedMatrixCell;
    int _formPlane;
    int _consoleFilter;
    uint64_t _consoleFloor;
    int _factoryPresetIndex;
    char _presetName[64];
    void*  _timer;
#include "s3g_midi_tool_editor.inc"
explicit Editor(Plugin& plugin) : Canvas(kGuiWidth,kGuiHeight) {_plugin = &plugin;
        _selectedRelay = 0;
        _relayPage = 0;
        _dragParam = -1;
        _openMenu = -1;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        _visualPage = 0;
        _matrixMode = 0;
        _selectedMatrixCell = -1;
        _formPlane = -1;
        _consoleFilter = 0;
        _consoleFloor = 0u;
        _factoryPresetIndex = detectedFactoryPreset(
            *&plugin);
        std::snprintf(_presetName, sizeof(_presetName), "%s",
            _factoryPresetIndex >= 0
                ? factoryPresetName(static_cast<uint32_t>(_factoryPresetIndex))
                : "CUSTOM");
}

void service() override {
    
    if (!isHidden() && _plugin
        && true) setNeedsDisplay(true);

}

void drawNeuralGraph_style_labels_values(Plugin& plugin, const drawing::Style& style, drawing::Attrs  labels, drawing::Attrs  values)  {
    VSTGUI::CColor  positive = drawing::color(0x9a6b50);
    VSTGUI::CColor  negative = drawing::color(0x587d81);
    VSTGUI::CColor  dormant = drawing::color(0x343434);
    const drawing::Point fieldBus = drawing::makePoint(398.0, 326.0);

    for (uint32_t cluster = 0u; cluster < s3g::relay::kClusterCount;
         ++cluster) {
        const drawing::Rect station = receptorStationRect(cluster);
        drawing::setFill(style.cellBg);
        drawing::rectFill(station);
        drawing::setStroke(style.grid);
        drawing::frameRect(station);
        drawing::drawAtPoint_withAttributes(drawing::stringFormat("C%u / R%u + R%u", cluster + 1u,
            cluster + 1u, cluster + 5u), drawing::makePoint(station.origin.x + 9.0,
                station.origin.y + 8.0), labels);
        drawLine(drawing::makePoint(station.origin.x + 9.0, station.origin.y + 28.0),
            drawing::makePoint(drawing::maxX(station) - 9.0, station.origin.y + 28.0),
            style.grid, 0.7);
    }

    // Draw both directions of each recurrent pentad in separate lanes. Color
    // is the actual coupling sign, arrowheads identify its target, and line
    // weight follows the absolute coupling strength.
    for (uint32_t cluster = 0u; cluster < s3g::relay::kClusterCount;
         ++cluster) {
        const uint32_t base = cluster * s3g::relay::kNodesPerCluster;
        for (uint32_t target = 0u;
             target < s3g::relay::kNodesPerCluster; ++target) {
            const uint32_t previous = (target
                + s3g::relay::kNodesPerCluster - 1u)
                % s3g::relay::kNodesPerCluster;
            const uint32_t next = (target + 1u)
                % s3g::relay::kNodesPerCluster;
            drawDirectedConnection(nodePoint(base + previous),
                nodePoint(base + target),
                s3g::relay::kRingForwardWeights[target], positive, negative);
            drawDirectedConnection(nodePoint(base + next),
                nodePoint(base + target),
                s3g::relay::kRingReverseWeights[target], positive, negative);
        }
    }

    drawLine(clusterPoint(0u), clusterPoint(1u), dormant);
    drawLine(clusterPoint(0u), clusterPoint(2u), dormant);
    drawLine(clusterPoint(1u), clusterPoint(3u), dormant);
    drawLine(clusterPoint(2u), clusterPoint(3u), dormant);

    if (_selectedMatrixCell >= 0
        && _selectedMatrixCell < static_cast<int>(
            s3g::relay::kClusterCount * s3g::relay::kClusterCount)) {
        const uint32_t selected = static_cast<uint32_t>(_selectedMatrixCell);
        const uint32_t target = selected / s3g::relay::kClusterCount;
        const uint32_t source = selected % s3g::relay::kClusterCount;
        const double learned = plugin.visualPlasticity[selected].load(
            std::memory_order_relaxed);
        const double weight = s3g::relay::kInterClusterWeights[selected]
            + learned;
        if (source != target) {
            drawDirectedConnection(clusterPoint(source), clusterPoint(target),
                weight, positive, negative);
        }
        for (uint32_t cluster : { source, target }) {
            const drawing::Point center = clusterPoint(cluster);
            drawing::Path  ring = drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(center.x - 48.0, center.y - 48.0, 96.0, 96.0));
            drawing::setStroke((cluster == target ? style.accent : style.text));
            ring.setLineWidth(cluster == target ? 1.6 : 0.9);
            ring.stroke();
        }
        drawing::drawAtPoint_withAttributes(drawing::stringFormat("MATRIX  C%u SEND → C%u RECV  W%+.2f",
            source + 1u, target + 1u, weight), drawing::makePoint(286.0, 76.0), labels);
    }

    for (uint32_t relay = 0u; relay < s3g::relay::kRelayCount; ++relay) {
        const uint32_t local = kGlobalParamCount + relay * kRelayParamCount;
        const auto topology = static_cast<ReceptorTopology>(
            static_cast<uint32_t>(publishedValue(plugin,
                local + kRelayTopology)));
        uint32_t target = relay % s3g::relay::kClusterCount;
        if (topology == ReceptorTopology::Cross)
            target = (target + 2u) % s3g::relay::kClusterCount;
        else if (topology == ReceptorTopology::Roaming) {
            target = (target + plugin.visualCell.load(
                std::memory_order_relaxed)) % s3g::relay::kClusterCount;
        }
        const double receptor = plugin.visualReceptors[relay].load(
            std::memory_order_relaxed);
        const drawing::Point destination = topology == ReceptorTopology::Diffuse
            ? fieldBus
            : topology == ReceptorTopology::Local
                ? nodePoint(target * s3g::relay::kNodesPerCluster
                    + s3g::relay::receptorPentadTap(relay))
                : clusterPoint(target);
        drawLine(receptorPathOrigin(relay), destination,
            receptor >= 0.0 ? positive : negative,
            relay == static_cast<uint32_t>(_selectedRelay) ? 1.8 : 0.65);
    }

    for (uint32_t node = 0u; node < s3g::relay::kNodeCount; ++node) {
        const double state = plugin.visualNodes[node].load(
            std::memory_order_relaxed);
        const double external = plugin.visualExternalInputNodes[node].load(
            std::memory_order_relaxed);
        const double radius = 5.0 + static_cast<double>(std::abs(state)) * 3.0;
        if (std::abs(external) > 1.0e-6) {
            const double halo = radius + 3.0
                + static_cast<double>(std::min(1.0, std::abs(external)))
                    * 3.0;
            drawing::setStroke((external >= 0.0 ? positive : negative));
            drawing::Path  inputHalo = drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(nodePoint(node).x - halo,
                    nodePoint(node).y - halo, halo * 2.0, halo * 2.0));
            inputHalo.setLineWidth(1.0);
            inputHalo.stroke();
        }
        drawing::setFill((state >= 0.0 ? positive : negative));
        drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
            nodePoint(node).x - radius, nodePoint(node).y - radius,
            radius * 2.0, radius * 2.0)).fill();
    }

    for (uint32_t cluster = 0u; cluster < s3g::relay::kClusterCount;
         ++cluster) {
        drawing::String  name = drawing::stringFormat("C%u", cluster + 1u);
        const drawing::Size size = drawing::sizeWithAttributes(name, values);
        drawing::drawAtPoint_withAttributes(name, drawing::makePoint(clusterPoint(cluster).x - size.width * 0.5,
            clusterPoint(cluster).y - size.height * 0.5), values);
    }

    drawing::setFill(style.strip);
    drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
        fieldBus.x - 6.0, fieldBus.y - 6.0, 12.0, 12.0)).fill();
    drawing::setStroke(style.grid);
    drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
        fieldBus.x - 6.0, fieldBus.y - 6.0, 12.0, 12.0)).stroke();
    drawing::drawAtPoint_withAttributes("DIFFUSE BUS", drawing::makePoint(fieldBus.x - 34.0,
        fieldBus.y + 10.0), labels);

    drawing::drawAtPoint_withAttributes(drawing::stringFormat("PENTAD CLOCKWISE FROM TOP: %s / %s / %s / %s / %s",
        s3g::relay::kPentadRoleNames[0u],
        s3g::relay::kPentadRoleNames[1u],
        s3g::relay::kPentadRoleNames[2u],
        s3g::relay::kPentadRoleNames[3u],
        s3g::relay::kPentadRoleNames[4u]), drawing::makePoint(188.0, 500.0), labels);

    const uint32_t bits = plugin.visualRegister.load(std::memory_order_relaxed);
    for (uint32_t relay = 0u; relay < s3g::relay::kRelayCount; ++relay) {
        const drawing::Point receptor = relayPoint(relay);
        const bool right = (relay % s3g::relay::kClusterCount & 1u) != 0u;
        const double railStart = right ? 644.0 : 106.0;
        const double railEnd = right ? 692.0 : 154.0;
        const double receptorValue = std::clamp(
            plugin.visualReceptors[relay].load(std::memory_order_relaxed),
            -1.0, 1.0);
        drawLine(drawing::makePoint(railStart, receptor.y),
            drawing::makePoint(railEnd, receptor.y), style.grid, 1.0);
        const double midpoint = (railStart + railEnd) * 0.5;
        drawLine(drawing::makePoint(midpoint, receptor.y - 3.0),
            drawing::makePoint(midpoint, receptor.y + 3.0), style.dim, 0.8);
        const double marker = railStart + static_cast<double>(
            (receptorValue + 1.0) * 0.5) * (railEnd - railStart);
        drawing::setFill((receptorValue >= 0.0 ? positive : negative));
        drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
            marker - 2.5, receptor.y - 2.5, 5.0, 5.0)).fill();

        const drawing::Rect glyph = relayGlyphRect(relay);
        const bool active = (bits & (1u << relay)) != 0u;
        drawing::setFill((active ? style.accent : style.strip));
        drawing::rectFill(glyph);
        drawing::setStroke((relay == static_cast<uint32_t>(_selectedRelay)
            ? style.text : style.grid));
        drawing::frameRect(glyph);
        drawing::String  name = drawing::stringFormat("R%u", relay + 1u);
        auto glyphAttrs = labels;
        glyphAttrs.color = active
            ? style.bg : style.text;
        const drawing::Size textSize = drawing::sizeWithAttributes(name, glyphAttrs);
        drawing::drawAtPoint_withAttributes(name, drawing::makePoint(drawing::midX(glyph) - textSize.width * 0.5,
            drawing::midY(glyph) - textSize.height * 0.5), glyphAttrs);
    }

    const drawing::Rect registerRect = drawing::makeRect(335.0, 274.0, 126.0, 31.0);
    drawing::setFill(style.strip);
    drawing::rectFill(registerRect);
    drawing::setStroke(style.grid);
    drawing::frameRect(registerRect);
    drawing::drawAtPoint_withAttributes("RECIRCULATING REGISTER", drawing::makePoint(331.0, 254.0), values);
    for (uint32_t bit = 0u; bit < 8u; ++bit) {
        const drawing::Rect cell = drawing::makeRect(343.0 + static_cast<double>(bit) * 14.0,
            283.0, 10.0, 11.0);
        drawing::setFill(((bits & (1u << bit)) != 0u ? style.accent : style.cellBg));
        drawing::rectFill(cell);
        drawing::setStroke(style.grid);
        drawing::frameRect(cell);
    }

}

void drawInjectionView_style_labels_values(Plugin& plugin, const drawing::Style& style, drawing::Attrs  labels, drawing::Attrs  values)  {
    VSTGUI::CColor  positive = drawing::color(0x9a6b50);
    VSTGUI::CColor  negative = drawing::color(0x587d81);
    const double activity = plugin.visualExternalInputActivity.load(
        std::memory_order_relaxed);
    drawing::drawAtPoint_withAttributes("EXTERNAL ECOLOGICAL INJECTION", drawing::makePoint(34.0, 76.0), values);
    drawing::drawAtPoint_withAttributes("MIDI ENTERS THE FIELD / RELAY OUTPUT IS NOT MIDI THRU", drawing::makePoint(34.0, 96.0), labels);
    drawing::drawAtPoint_withAttributes("INPUT ACTIVITY", drawing::makePoint(518.0, 96.0), labels);
    drawing::setFill(style.grid);
    drawing::rectFill(drawing::makeRect(610.0, 101.0, 132.0, 4.0));
    drawing::setFill(style.accent);
    drawing::rectFill(drawing::makeRect(610.0, 101.0,
        132.0 * static_cast<double>(std::clamp(activity * 3.0, 0.0, 1.0)),
        4.0));

    static constexpr std::array<double, s3g::relay::kClusterCount> centers {{
        112.0, 290.0, 468.0, 646.0,
    }};
    const auto inputPoint = [&](uint32_t node) {
        const uint32_t cluster = node / s3g::relay::kNodesPerCluster;
        const uint32_t role = node % s3g::relay::kNodesPerCluster;
        const double angle = -s3g::relay::kPi * 0.5
            + s3g::relay::kPi * 2.0 * static_cast<double>(role)
                / static_cast<double>(s3g::relay::kNodesPerCluster);
        return drawing::makePoint(static_cast<double>(centers[cluster]
                + std::cos(angle) * 43.0),
            static_cast<double>(397.0 + std::sin(angle) * 43.0));
    };
    for (uint32_t cluster = 0u; cluster < s3g::relay::kClusterCount;
         ++cluster) {
        const uint32_t base = cluster * s3g::relay::kNodesPerCluster;
        for (uint32_t role = 0u; role < s3g::relay::kNodesPerCluster;
             ++role) {
            const uint32_t next = (role + 1u)
                % s3g::relay::kNodesPerCluster;
            drawLine(inputPoint(base + role), inputPoint(base + next),
                style.grid, 0.8);
        }
        drawing::drawAtPoint_withAttributes(drawing::stringFormat("C%u", cluster + 1u), drawing::makePoint(
                static_cast<double>(centers[cluster] - 8.0), 390.0), values);
    }
    for (uint32_t node = 0u; node < s3g::relay::kNodeCount; ++node) {
        const double drive = plugin.visualExternalInputNodes[node].load(
            std::memory_order_relaxed);
        const double radius = 4.0
            + static_cast<double>(std::min(1.0, std::abs(drive))) * 7.0;
        drawing::setFill((std::abs(drive) < 1.0e-7 ? style.dim
                                   : drive >= 0.0 ? positive : negative));
        const drawing::Point point = inputPoint(node);
        drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
            point.x - radius, point.y - radius, radius * 2.0, radius * 2.0)).fill();
    }
    const auto map = static_cast<MidiInputMap>(static_cast<uint32_t>(
        std::lround(publishedValue(plugin, kMidiInputMap))));
    static drawing::String  const mapNotes[4] = {
        "NOTE NUMBER MOD 20 ADDRESSES ONE PENTAD ROLE",
        "NOTE NUMBER MOD 4 ADDRESSES A WHOLE CLUSTER",
        "MATCH OUTPUT NOTE WHEN POSSIBLE / OTHERWISE NOTE MOD 8",
        "EVERY ACCEPTED EVENT EXCITES THE COMPLETE FIELD",
    };
    drawing::drawAtPoint_withAttributes(mapNotes[std::min<uint32_t>(static_cast<uint32_t>(map), 3u)], drawing::makePoint(34.0, 488.0), values);
    drawing::drawAtPoint_withAttributes("IMPULSE DECAYS IN BEATS / GATE PERSISTS UNTIL NOTE OFF / CC IS A CONTINUOUS FORCE", drawing::makePoint(34.0, 510.0), labels);

}

void drawLearningPlate_style_labels_values(Plugin& plugin, const drawing::Style& style, drawing::Attrs  labels, drawing::Attrs  values)  {
    VSTGUI::CColor  positive = drawing::color(0x9a6b50);
    VSTGUI::CColor  negative = drawing::color(0x587d81);
    constexpr double displayLimit = 0.55;
    const bool flowMode = _matrixMode == 1;

    drawing::drawAtPoint_withAttributes((flowMode ? "FLOW MATRIX / LIVE SIGNED CONTRIBUTION"
               : "PLASTICITY MATRIX / EFFECTIVE CLUSTER CONNECTIVITY"), drawing::makePoint(34.0, 76.0), values);
    drawing::drawAtPoint_withAttributes((flowMode
        ? "SQUARE AREA = |SOURCE ACTIVITY × WEIGHT| / ROWS RECEIVE"
        : "SQUARE AREA = |EFFECTIVE WEIGHT| / ROWS RECEIVE / COLUMNS SEND"), drawing::makePoint(34.0, 94.0), labels);
    static drawing::String  const matrixModes[2] = { "WEIGHT", "FLOW" };
    for (uint32_t mode = 0u; mode < 2u; ++mode) {
        drawing::drawToolboxHeaderButton(learningModeRect(mode),
            graphPanelRect(), matrixModes[mode],
            _matrixMode == static_cast<int>(mode), labels, style);
    }

    const drawing::Rect matrixCorner = drawing::makeRect(kLearningRowHeaderX,
        kLearningColumnHeaderY, kLearningRowHeaderWidth,
        kLearningColumnHeaderHeight);
    drawing::setFill(style.strip);
    drawing::rectFill(matrixCorner);
    drawing::setStroke(style.dim);
    drawing::frameRect(matrixCorner);
    drawing::drawAtPoint_withAttributes("TO / FROM", drawing::makePoint(kLearningRowHeaderX + 7.0,
        kLearningColumnHeaderY + 9.0), labels);
    for (uint32_t source = 0u; source < s3g::relay::kClusterCount;
         ++source) {
        const drawing::Rect header = drawing::makeRect(
            kLearningMatrixX + static_cast<double>(source)
                * kLearningCellSize,
            kLearningColumnHeaderY, kLearningCellSize,
            kLearningColumnHeaderHeight);
        drawing::setFill(style.strip);
        drawing::rectFill(header);
        drawing::setStroke(style.dim);
        drawing::frameRect(header);
        drawing::String  name = drawing::stringFormat("SEND C%u",
            source + 1u);
        const drawing::Size size = drawing::sizeWithAttributes(name, values);
        drawing::drawAtPoint_withAttributes(name, drawing::makePoint(
            drawing::midX(header) - size.width * 0.5,
            drawing::midY(header) - size.height * 0.5), values);
    }

    double totalDrift = 0.0;
    double maximumDrift = 0.0;
    uint32_t maximumIndex = 0u;
    uint32_t signFlips = 0u;
    for (uint32_t target = 0u; target < s3g::relay::kClusterCount;
         ++target) {
        const drawing::Rect rowHeader = drawing::makeRect(kLearningRowHeaderX,
            kLearningMatrixY + static_cast<double>(target)
                * kLearningCellSize,
            kLearningRowHeaderWidth, kLearningCellSize);
        drawing::setFill(style.strip);
        drawing::rectFill(rowHeader);
        drawing::setStroke(style.dim);
        drawing::frameRect(rowHeader);
        drawing::String  rowName = drawing::stringFormat("RECV C%u",
            target + 1u);
        const drawing::Size rowSize = drawing::sizeWithAttributes(rowName, values);
        drawing::drawAtPoint_withAttributes(rowName, drawing::makePoint(
            drawing::midX(rowHeader) - rowSize.width * 0.5,
            drawing::midY(rowHeader) - rowSize.height * 0.5), values);
        for (uint32_t source = 0u; source < s3g::relay::kClusterCount;
             ++source) {
            const uint32_t index = target * s3g::relay::kClusterCount
                + source;
            const double base = s3g::relay::kInterClusterWeights[index];
            const double learned = plugin.visualPlasticity[index].load(
                std::memory_order_relaxed);
            const double effective = base + learned;
            const double sourceActivity = plugin.visualClusters[source].load(
                std::memory_order_relaxed);
            const double displayed = flowMode
                ? sourceActivity * effective : effective;
            const double outlined = flowMode ? effective : base;
            totalDrift += std::abs(learned);
            if (std::abs(learned) > maximumDrift) {
                maximumDrift = std::abs(learned);
                maximumIndex = index;
            }
            if (base * effective < 0.0) ++signFlips;

            const drawing::Rect cell = learningCellRect(target, source);
            drawing::setFill(style.cellBg);
            drawing::rectFill(cell);
            drawing::setStroke(style.dim);
            drawing::Path  cellBorder = drawing::Path::bezierPathWithRect(cell);
            cellBorder.setLineWidth(1.0);
            cellBorder.stroke();

            const auto squareSide = [&](double weight) {
                const double ratio = std::clamp(
                    std::abs(weight) / displayLimit, 0.0, 1.0);
                return static_cast<double>(std::sqrt(ratio) * 44.0);
            };
            const drawing::Point squareCenter = drawing::makePoint(
                drawing::midX(cell), cell.origin.y + 38.0);
            const double effectiveSide = squareSide(displayed);
            if (effectiveSide > 0.5) {
                drawing::setFill(drawing::withAlpha((displayed >= 0.0 ? positive : negative), 0.72));
                drawing::rectFill(drawing::makeRect(
                    squareCenter.x - effectiveSide * 0.5,
                    squareCenter.y - effectiveSide * 0.5,
                    effectiveSide, effectiveSide));
            }
            const double baseSide = squareSide(outlined);
            if (baseSide > 0.5) {
                drawing::setStroke(drawing::withAlpha((outlined >= 0.0 ? positive : negative), 0.92));
                drawing::Path  baseOutline = drawing::Path::bezierPathWithRect(drawing::makeRect(squareCenter.x - baseSide * 0.5,
                        squareCenter.y - baseSide * 0.5,
                        baseSide, baseSide));
                baseOutline.setLineWidth(1.1);
                baseOutline.stroke();
            }
            if (!flowMode) {
                const double deltaHalf = static_cast<double>(std::clamp(
                    std::abs(learned) / 0.22, 0.0, 1.0)) * 29.0;
                const double deltaCenter = drawing::midX(cell);
                const double deltaY = drawing::maxY(cell) - 4.0;
                drawLine(drawing::makePoint(deltaCenter, deltaY),
                    drawing::makePoint(deltaCenter
                        + (learned >= 0.0 ? deltaHalf : -deltaHalf), deltaY),
                    style.accent, 2.0);
            }

            drawing::String  deltaText = drawing::stringFormat(flowMode ? "S%+.2f" : "D%+.2f",
                flowMode ? sourceActivity : learned);
            drawing::drawAtPoint_withAttributes(deltaText, drawing::makePoint(cell.origin.x + 5.0,
                cell.origin.y + 4.0), labels);
            drawing::String  weightText = drawing::stringFormat(flowMode ? "F%+.2f" : "W%+.2f", displayed);
            const drawing::Size weightSize = drawing::sizeWithAttributes(weightText, values);
            drawing::drawAtPoint_withAttributes(weightText, drawing::makePoint(
                drawing::midX(cell) - weightSize.width * 0.5,
                cell.origin.y + 60.0), values);
            if (_selectedMatrixCell == static_cast<int>(index)) {
                drawing::setStroke(style.accent);
                drawing::Path  selection = drawing::Path::bezierPathWithRect(drawing::inset(cell, 2.0, 2.0));
                selection.setLineWidth(1.8);
                selection.stroke();
            }
        }
    }
    drawing::setStroke(style.text);
    drawing::Path  matrixBorder = drawing::Path::bezierPathWithRect(drawing::makeRect(
        kLearningRowHeaderX, kLearningColumnHeaderY,
        kLearningRowHeaderWidth
            + kLearningCellSize * s3g::relay::kClusterCount,
        kLearningColumnHeaderHeight
            + kLearningCellSize * s3g::relay::kClusterCount));
    matrixBorder.setLineWidth(1.4);
    matrixBorder.stroke();

    const bool frozen = publishedValue(plugin, kFreeze) >= 0.5;
    const bool playing = plugin.visualPlaying.load(std::memory_order_relaxed);
    const double mutation = publishedValue(plugin, kMutation);
    drawing::String  status = !playing ? "IDLE" : frozen ? "FROZEN"
        : mutation <= 1.0e-6 ? "STATIC" : "ADAPTING";
    drawing::drawAtPoint_withAttributes((flowMode ? "FLOW READOUT" : "MATRIX READOUT"), drawing::makePoint(480.0, 76.0), labels);
    drawing::setFill((!playing || frozen ? style.strip : style.accent));
    const drawing::Rect statusRect = drawing::makeRect(480.0, 94.0, 266.0, 22.0);
    drawing::rectFill(statusRect);
    auto statusAttrs = values;
    statusAttrs.color = !playing || frozen
        ? style.text : style.bg;
    drawing::drawAtPoint_withAttributes(status, drawing::makePoint(490.0, 99.0), statusAttrs);
    drawing::drawAtPoint_withAttributes(drawing::stringFormat("MUTATION  %3.0f%%", mutation * 100.0), drawing::makePoint(480.0, 130.0), values);
    drawing::drawAtPoint_withAttributes(drawing::stringFormat("MEAN |D|  %.3f",
        totalDrift / static_cast<double>(
            s3g::relay::kClusterCount * s3g::relay::kClusterCount)), drawing::makePoint(480.0, 151.0), values);
    drawing::drawAtPoint_withAttributes(drawing::stringFormat("MAX |D|   %.3f", maximumDrift), drawing::makePoint(480.0, 172.0), values);
    const uint32_t maximumTarget = maximumIndex / s3g::relay::kClusterCount;
    const uint32_t maximumSource = maximumIndex % s3g::relay::kClusterCount;
    drawing::drawAtPoint_withAttributes(drawing::stringFormat("STRONGEST D  C%u > C%u",
        maximumSource + 1u, maximumTarget + 1u), drawing::makePoint(480.0, 193.0), values);
    drawing::drawAtPoint_withAttributes(drawing::stringFormat("SIGN FLIPS %u", signFlips), drawing::makePoint(480.0, 214.0), values);
    drawing::drawAtPoint_withAttributes((flowMode ? "FLOW KEY" : "MATRIX KEY"), drawing::makePoint(480.0, 244.0), labels);
    drawing::setFill(drawing::withAlpha(positive, 0.72));
    drawing::rectFill(drawing::makeRect(480.0, 267.0, 18.0, 18.0));
    drawing::drawAtPoint_withAttributes((flowMode ? "FILLED AREA  LIVE CONTRIBUTION"
               : "FILLED AREA  EFFECTIVE WEIGHT"), drawing::makePoint(508.0, 270.0), values);
    drawing::setStroke(positive);
    drawing::Path  baseKey = drawing::Path::bezierPathWithRect(drawing::makeRect(480.0, 299.0, 18.0, 18.0));
    baseKey.setLineWidth(1.1);
    baseKey.stroke();
    drawing::drawAtPoint_withAttributes((flowMode ? "OUTLINE  EFFECTIVE WEIGHT"
               : "OUTLINE  FIXED BASE WEIGHT"), drawing::makePoint(508.0, 302.0), values);
    if (flowMode) {
        drawing::drawAtPoint_withAttributes("F = SOURCE STATE × EFFECTIVE WEIGHT", drawing::makePoint(480.0, 333.0), values);
    } else {
        drawLine(drawing::makePoint(480.0, 340.0), drawing::makePoint(506.0, 340.0),
            style.accent, 2.0);
        drawing::drawAtPoint_withAttributes("FOOT LINE  LEARNED DELTA", drawing::makePoint(516.0, 333.0), values);
    }
    drawing::drawAtPoint_withAttributes("RUST POSITIVE / TEAL NEGATIVE", drawing::makePoint(480.0, 365.0), labels);
    drawing::drawAtPoint_withAttributes("LEARNING RULE", drawing::makePoint(480.0, 396.0), labels);
    drawing::drawAtPoint_withAttributes("D(t+1) = DECAYED D(t)", drawing::makePoint(480.0, 416.0), labels);
    drawing::drawAtPoint_withAttributes("+ MUTATION x SOURCE(t-1) x TARGET(t)", drawing::makePoint(480.0, 434.0), labels);
    drawing::drawAtPoint_withAttributes("DIAGONAL BASE WEIGHTS ARE ZERO", drawing::makePoint(480.0, 464.0), labels);
    drawing::drawAtPoint_withAttributes("CORRELATION MAY LEARN SELF-COUPLING", drawing::makePoint(480.0, 482.0), labels);
    drawing::drawAtPoint_withAttributes("DELTA CLAMPS TO +/- 0.22 FROM BASE", drawing::makePoint(480.0, 510.0), labels);

}

void drawFormDeck_style_labels_values(Plugin& plugin, const drawing::Style& style, drawing::Attrs  labels, drawing::Attrs  values)  {
    VSTGUI::CColor  positive = drawing::color(0x9a6b50);
    VSTGUI::CColor  negative = drawing::color(0x587d81);
    const uint32_t depthIndex = static_cast<uint32_t>(std::lround(
        publishedValue(plugin, kLatticeDepth)));
    const uint32_t planes = s3g::relay::latticePlaneCount(depthIndex);
    const uint32_t totalCells = s3g::relay::latticeCellCount(depthIndex);
    const uint32_t current = plugin.visualCell.load(
        std::memory_order_relaxed) % totalCells;
    const uint32_t previous = plugin.visualPreviousCell.load(
        std::memory_order_relaxed) % totalCells;
    const uint32_t currentPlane = current
        / s3g::relay::kClimateCellsPerPlane;
    const bool following = _formPlane < 0
        || _formPlane >= static_cast<int>(planes);
    const uint32_t inspectedPlane = following ? currentPlane
        : static_cast<uint32_t>(_formPlane);
    const uint32_t trailCount = std::min<uint32_t>(
        plugin.visualTrailCount.load(std::memory_order_relaxed),
        s3g::relay::kTrailLength);
    const uint32_t seed = static_cast<uint32_t>(publishedValue(plugin, kSeed));
    const int64_t cycleIndex = plugin.visualCycleIndex.load(
        std::memory_order_relaxed);
    const double cyclePhase = plugin.visualCyclePhase.load(
        std::memory_order_relaxed);
    const double gestation = plugin.visualClimateBlend.load(
        std::memory_order_relaxed);
    const bool formHeld = plugin.formHold.load(std::memory_order_acquire);

    static drawing::String  const planeItems[5] = {
        "FOLLOW", "P1", "P2", "P3", "P4",
    };
    for (uint32_t item = 0u; item <= planes; ++item) {
        const bool active = item == 0u ? following
            : (!following && inspectedPlane + 1u == item);
        drawing::drawToolboxHeaderButton(formPlaneButtonRect(item),
            graphPanelRect(), planeItems[item], active, labels, style);
    }
    drawing::drawAtPoint_withAttributes(drawing::stringFormat("CLIMATE DECK / %u SUIT%@ / %u CARDS",
        planes, planes == 1u ? "" : "S", totalCells), drawing::makePoint(368.0, 76.0), values);
    drawing::drawAtPoint_withAttributes(drawing::stringFormat(formHeld ? "FORM HELD @ %u:%lld / DEAL P%u C%02u"
                 : "SEEDED SHUFFLE %u:%lld / DEAL P%u C%02u",
        seed, static_cast<long long>(cycleIndex), currentPlane + 1u,
        current % s3g::relay::kClimateCellsPerPlane + 1u), drawing::makePoint(368.0, 94.0), labels);
    drawing::drawAtPoint_withAttributes(drawing::stringFormat("%@ SUIT P%u",
        following ? "FOLLOWING DEAL /" : "INSPECTING /",
        inspectedPlane + 1u), drawing::makePoint(34.0, 104.0), labels);

    constexpr double gridX = 34.0;
    constexpr double gridY = 122.0;
    constexpr double cardWidth = 103.0;
    constexpr double cardHeight = 84.0;
    constexpr double cardGap = 6.0;
    for (uint32_t localCell = 0u;
         localCell < s3g::relay::kClimateCellsPerPlane; ++localCell) {
        const uint32_t cell = inspectedPlane
            * s3g::relay::kClimateCellsPerPlane + localCell;
        const uint32_t column = localCell % s3g::relay::kClimateWidth;
        const uint32_t row = localCell / s3g::relay::kClimateWidth;
        const drawing::Rect rect = drawing::makeRect(
            gridX + static_cast<double>(column) * (cardWidth + cardGap),
            gridY + static_cast<double>(row) * (cardHeight + cardGap),
            cardWidth, cardHeight);
        bool inTrail = false;
        for (uint32_t index = 0u; index < trailCount; ++index) {
            if (plugin.visualTrail[index].load(std::memory_order_relaxed)
                % totalCells == cell) {
                inTrail = true;
                break;
            }
        }
        VSTGUI::CColor  fill = cell == current
            ? drawing::color(0x3a302b)
            : cell == previous ? drawing::color(0x3a3a3a)
            : inTrail ? drawing::color(0x2e2e2e)
            : style.cellBg;
        drawing::Path card = drawing::roundedPath(rect, 4.0);
        drawing::setFill(fill);
        card.fill();
        drawing::setStroke((cell == current ? style.accent : style.grid));
        card.setLineWidth(cell == current ? 1.6 : 0.8);
        card.stroke();

        drawing::drawAtPoint_withAttributes(drawing::stringFormat("C%02u", localCell + 1u), drawing::makePoint(rect.origin.x + 7.0, rect.origin.y + 7.0), values);
        const int energy = static_cast<int>(std::lround(
            s3g::relay::climateCellTrait(
                seed, cycleIndex, cell, 0u) * 99.0));
        const int coupling = static_cast<int>(std::lround(
            s3g::relay::climateCellTrait(
                seed, cycleIndex, cell, 1u) * 99.0));
        const int hierarchy = static_cast<int>(std::lround(
            s3g::relay::climateCellTrait(
                seed, cycleIndex, cell, 2u) * 99.0));
        const int contrast = static_cast<int>(std::lround(
            s3g::relay::climateCellTrait(
                seed, cycleIndex, cell, 3u) * 99.0));
        drawing::drawAtPoint_withAttributes(drawing::stringFormat("E%+03d  C%+03d", energy, coupling), drawing::makePoint(rect.origin.x + 7.0, rect.origin.y + 34.0), labels);
        drawing::drawAtPoint_withAttributes(drawing::stringFormat("H%+03d  X%+03d", hierarchy, contrast), drawing::makePoint(rect.origin.x + 7.0, rect.origin.y + 54.0), labels);
        if (inTrail) {
            drawing::setFill(style.accent);
            drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
                drawing::maxX(rect) - 12.0, rect.origin.y + 9.0, 4.0, 4.0)).fill();
        }
    }

    const drawing::Rect dealt = drawing::makeRect(500.0, 122.0, 246.0, 286.0);
    drawing::Path dealtCard = drawing::roundedPath(dealt, 7.0);
    drawing::setFill(drawing::color(0x242424));
    dealtCard.fill();
    drawing::setStroke(style.accent);
    dealtCard.setLineWidth(1.4);
    dealtCard.stroke();
    drawing::drawAtPoint_withAttributes((formHeld ? "DEALT CLIMATE / HELD" : "DEALT CLIMATE"), drawing::makePoint(518.0, 140.0), labels);
    drawing::drawAtPoint_withAttributes(drawing::stringFormat("P%u / C%02u",
        currentPlane + 1u,
        current % s3g::relay::kClimateCellsPerPlane + 1u), drawing::makePoint(518.0, 161.0), values);
    drawing::drawAtPoint_withAttributes("SUIT 64% MACRO / CARD 36% LOCAL", drawing::makePoint(518.0, 180.0), labels);

    static drawing::String  const traitNames[4] = {
        "ENERGY", "COUPLING", "HIERARCHY", "CONTRAST",
    };
    for (uint32_t trait = 0u; trait < 4u; ++trait) {
        const double traitValue = s3g::relay::climateCellTrait(
            seed, cycleIndex, current, trait);
        const double y = 217.0 + static_cast<double>(trait) * 38.0;
        drawing::drawAtPoint_withAttributes(traitNames[trait], drawing::makePoint(518.0, y - 8.0), labels);
        constexpr double left = 597.0;
        constexpr double right = 704.0;
        const double center = (left + right) * 0.5;
        drawLine(drawing::makePoint(left, y), drawing::makePoint(right, y),
            style.grid, 2.0);
        drawLine(drawing::makePoint(center, y - 4.0),
            drawing::makePoint(center, y + 4.0), style.dim, 0.8);
        const double end = center + static_cast<double>(traitValue)
            * (right - left) * 0.5;
        drawLine(drawing::makePoint(center, y), drawing::makePoint(end, y),
            traitValue >= 0.0 ? positive : negative, 3.0);
        drawing::drawAtPoint_withAttributes(drawing::stringFormat("%+03d",
            static_cast<int>(std::lround(traitValue * 99.0))), drawing::makePoint(711.0, y - 8.0), values);
    }
    drawing::drawAtPoint_withAttributes("GESTATION", drawing::makePoint(518.0, 361.0), labels);
    drawing::setFill(style.grid);
    drawing::rectFill(drawing::makeRect(597.0, 365.0, 128.0, 4.0));
    drawing::setFill(style.accent);
    drawing::rectFill(drawing::makeRect(597.0, 365.0,
        128.0 * static_cast<double>(gestation), 4.0));
    drawing::drawAtPoint_withAttributes("FORM CYCLE", drawing::makePoint(518.0, 382.0), labels);
    drawing::setFill(style.grid);
    drawing::rectFill(drawing::makeRect(597.0, 386.0, 128.0, 4.0));
    drawing::setFill(style.accent);
    drawing::rectFill(drawing::makeRect(597.0, 386.0,
        128.0 * static_cast<double>(cyclePhase), 4.0));

    drawing::drawAtPoint_withAttributes("RECENT DEAL", drawing::makePoint(500.0, 426.0), labels);
    const uint32_t recentCount = std::min<uint32_t>(trailCount, 8u);
    const uint32_t recentStart = trailCount - recentCount;
    for (uint32_t slot = 0u; slot < recentCount; ++slot) {
        const uint32_t trailCell = plugin.visualTrail[recentStart + slot].load(
            std::memory_order_relaxed) % totalCells;
        const drawing::Rect token = drawing::makeRect(
            500.0 + static_cast<double>(slot % 4u) * 61.0,
            445.0 + static_cast<double>(slot / 4u) * 34.0,
            55.0, 27.0);
        drawing::setFill((trailCell == current ? drawing::color(0x3a302b)
                               : style.cellBg));
        drawing::rectFill(token);
        drawing::setStroke((trailCell == current ? style.accent : style.grid));
        drawing::frameRect(token);
        drawing::drawAtPoint_withAttributes(drawing::stringFormat("P%u C%02u",
            trailCell / s3g::relay::kClimateCellsPerPlane + 1u,
            trailCell % s3g::relay::kClimateCellsPerPlane + 1u), drawing::makePoint(token.origin.x + 5.0,
                token.origin.y + 7.0), labels);
    }
    drawing::drawAtPoint_withAttributes("SEED + FORM CYCLE SHUFFLE TRAITS / REGISTER + FIELD DEAL THE PATH", drawing::makePoint(34.0, 520.0), labels);

}

void markCustomPreset(const char* name)  {
    _factoryPresetIndex = -1;
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        name && name[0] ? name : "CUSTOM");

}

bool applyFactoryPreset(int index)  {
    if (!_plugin || index < 0
        || index >= static_cast<int>(kFactoryPresetCount)) return false;
    auto& plugin = *static_cast<Plugin*>(_plugin);
    if (!queueFactoryPreset(plugin,
            factoryPresetConfig(static_cast<uint32_t>(index)))) return false;
    plugin.formHold.store(false, std::memory_order_release);
    plugin.hasThawMemory.store(false, std::memory_order_relaxed);
    _factoryPresetIndex = index;
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        factoryPresetName(static_cast<uint32_t>(index)));
    setNeedsDisplay(true);
    return true;

}

bool applySafeRandom()  {
    if (!_plugin) return false;
    auto& plugin = *static_cast<Plugin*>(_plugin);
    const auto unit = [] {
        return static_cast<double>(drawing::randomBits()) / 4294967295.0;
    };
    GuiParamBatch batch;
    const auto add = [&](uint32_t index, double value) {
        return batch.add(index, value);
    };
    if (!add(kActivity, 0.32 + unit() * 0.58)
        || !add(kCoupling, 0.22 + unit() * 0.68)
        || !add(kMemory, 0.24 + unit() * 0.72)
        || !add(kMutation, 0.015 + unit() * 0.30)
        || !add(kHierarchy, 0.12 + unit() * 0.80)
        || !add(kContrast, 0.20 + unit() * 0.72)
        || !add(kFreeze, 0.0)
        || !add(kClockRate, 2.0 + std::floor(unit() * 4.0))
        || !add(kGate, 0.04 + unit() * 0.52)
        || !add(kCcRate, 1.0 + std::floor(unit() * 4.0))
        || !add(kFormBars, 32.0 * (2.0 + std::floor(unit() * 7.0)))
        || !add(kDwellBars, 1.0 + std::floor(unit() * 16.0))
        || !add(kTransitionBars, unit() * 6.0)
        || !add(kClimate, 0.20 + unit() * 0.74)
        || !add(kSeed, 1.0 + std::floor(unit() * 65535.0))
        || !add(kDealerLaw, std::floor(unit()
            * static_cast<double>(s3g::relay::kDealerLawCount)))) return false;
    for (uint32_t relay = 0u; relay < s3g::relay::kRelayCount; ++relay) {
        const uint32_t base = kGlobalParamCount + relay * kRelayParamCount;
        if (!add(base + kRelayThreshold, 0.22 + unit() * 0.58)
            || !add(base + kRelayBias, -0.42 + unit() * 0.84)
            || !add(base + kRelayRefractory,
                1.0 + std::floor(unit() * 12.0))
            || !add(base + kRelayFeedback, -0.72 + unit() * 1.44)
            || !add(base + kRelayGate, 0.45 + unit() * 1.45)
            || !add(base + kRelayTopology, std::floor(unit() * 4.0)))
            return false;
    }
    if (!batch.submit(plugin)) return false;
    plugin.formHold.store(false, std::memory_order_release);
    plugin.hasThawMemory.store(false, std::memory_order_relaxed);
    markCustomPreset("RANDOM");
    setNeedsDisplay(true);
    return true;

}

drawing::Rect openMenuRect()  {
    if (_openMenu == kFactoryPresetMenuId && _menuItemCount > 0u) {
        const auto band = drawing::encoderTitleBand(
            kGuiWidth, kGuiHeight);
        const drawing::Rect anchor = drawing::cocoaRect(band.presetMenu);
        return drawing::makeRect(anchor.origin.x, drawing::maxY(anchor) + 2.0,
            anchor.size.width, 18.0 * static_cast<double>(_menuItemCount));
    }
    if (_openMenu < 0 || _openMenu >= static_cast<int>(kParamCount)
        || _menuItemCount == 0u) return drawing::Rect{};
    if (_openMenu == static_cast<int>(kScale)) {
        const uint32_t rows = drawing::multiColumnMenuRows(
            s3g::kMusicalScaleCount, 4u);
        return drawing::makeRect(500.0, 66.0, 720.0,
            18.0 * static_cast<double>(rows));
    }
    const drawing::Rect box = parameterMenuBoxRect(
        static_cast<uint32_t>(_openMenu));
    const uint32_t columns = parameterMenuColumns(
        static_cast<uint32_t>(_openMenu));
    const double height = 18.0 * static_cast<double>(
        drawing::multiColumnMenuRows(_menuItemCount, columns));
    const double below = drawing::maxY(box) + 2.0;
    const double y = below + height <= static_cast<double>(kGuiHeight) - 8.0
        ? below : box.origin.y - height - 2.0;
    return drawing::makeRect(box.origin.x, y, box.size.width, height);

}

bool openParameterMenu(int index)  {
    if (index < 0 || index >= static_cast<int>(kParamCount)
        || !parameterIsMenu(static_cast<uint32_t>(index))) return false;
    const uint32_t count = parameterMenuCount(static_cast<uint32_t>(index));
    if (count == 0u) return false;
    _openMenu = index;
    _hoverMenuItem = -1;
    _menuItemCount = count;
    _dragParam = -1;
    setNeedsDisplay(true);
    return true;

}

bool selectVisualPage(int page)  {
    if (page < 0 || page >= 5) return false;
    _visualPage = page;
    _openMenu = -1;
    _hoverMenuItem = -1;
    _menuItemCount = 0u;
    _dragParam = -1;
    setNeedsDisplay(true);
    return true;

}

bool selectRelayPage(int page)  {
    if (page < 0 || page >= 2) return false;
    _relayPage = page;
    _dragParam = -1;
    setNeedsDisplay(true);
    return true;

}

void drawOpenMenu_style(drawing::Attrs  attributes, const drawing::Style& style)  {
    if (_openMenu < 0 || _menuItemCount == 0u || !_plugin) return;
    if (_openMenu == kFactoryPresetMenuId) {
        drawing::String  items[kFactoryPresetCount] {};
        for (uint32_t item = 0u; item < kFactoryPresetCount; ++item)
            items[item] = drawing::stringFromUtf8(factoryPresetName(item));
        drawing::drawDropdownMenu(openMenuRect(), 18.0,
            items, kFactoryPresetCount, static_cast<int>(_factoryPresetIndex),
            static_cast<int>(_hoverMenuItem), attributes, style);
        return;
    }
    if (_openMenu >= static_cast<int>(kParamCount)) return;
    if (_openMenu == static_cast<int>(kScale)) {
        static const std::array<drawing::String , s3g::kMusicalScaleCount> items = [] {
            std::array<drawing::String , s3g::kMusicalScaleCount> result {};
            for (uint32_t item = 0u; item < result.size(); ++item) {
                const uint32_t scale =
                    s3g::musicalScaleValueForMenuIndex(item);
                result[item] = drawing::stringFromUtf8(s3g::musicalScaleDefinition(scale).name);
            }
            return result;
        }();
        const auto& plugin = *static_cast<Plugin*>(_plugin);
        drawing::drawMultiColumnDropdownMenu(
            openMenuRect(), 18.0, items.data(), _menuItemCount, 4u,
            parameterMenuSelection(kScale, plugin),
            static_cast<int>(_hoverMenuItem), attributes, style);
        return;
    }
    // Some menus legitimately exceed sixteen entries (Input Channel has
    // Omni plus channels 1-16). Keep this scratch storage sized from the
    // actual menu instead of imposing a hidden stack-array limit.
    std::vector<drawing::String> items(_menuItemCount);
    for (uint32_t item = 0u; item < _menuItemCount; ++item)
        items[item] = parameterMenuItem(
            static_cast<uint32_t>(_openMenu), item);
    const auto& plugin = *static_cast<Plugin*>(_plugin);
    const uint32_t index = static_cast<uint32_t>(_openMenu);
    const uint32_t columns = parameterMenuColumns(index);
    if (columns > 1u) {
        drawing::drawMultiColumnDropdownMenu(
            openMenuRect(), 18.0, items.data(), _menuItemCount, columns,
            parameterMenuSelection(index, plugin),
            static_cast<int>(_hoverMenuItem), attributes, style);
    } else {
        drawing::drawDropdownMenu(openMenuRect(), 18.0,
            items.data(), _menuItemCount,
            parameterMenuSelection(index, plugin),
            static_cast<int>(_hoverMenuItem), attributes, style);
    }

}

void drawMidiConsole_style_labels_values(Plugin& plugin, const drawing::Style& style, drawing::Attrs  labels, drawing::Attrs  values)  {
    struct TraceLine {
        uint64_t sequence = 0u;
        double beat = 0.0;
        uint32_t packed = 0u;
    };
    constexpr uint32_t kVisibleLines = 22u;
    std::array<TraceLine, kVisibleLines> lines {};
    uint32_t matching = 0u;
    const uint64_t end = plugin.midiTraceWrite.load(std::memory_order_acquire);
    if (end < _consoleFloor) _consoleFloor = 0u;
    const uint64_t earliest = end > kMidiTraceCapacity
        ? end - kMidiTraceCapacity : 0u;
    const uint64_t begin = std::max<uint64_t>(earliest, _consoleFloor);
    for (uint64_t sequence = begin; sequence < end; ++sequence) {
        const auto& slot = plugin.midiTrace[static_cast<std::size_t>(
            sequence % kMidiTraceCapacity)];
        const uint64_t expected = sequence + 1u;
        if (slot.sequence.load(std::memory_order_acquire) != expected) continue;
        const double beat = slot.beat.load(std::memory_order_relaxed);
        const uint32_t packed = slot.packed.load(std::memory_order_relaxed);
        if (slot.sequence.load(std::memory_order_acquire) != expected) continue;
        const EventKind kind = static_cast<EventKind>(packed & 3u);
        if ((_consoleFilter == 1 && kind == EventKind::ControlChange)
            || (_consoleFilter == 2 && kind != EventKind::ControlChange))
            continue;
        lines[matching % kVisibleLines] = { expected, beat, packed };
        ++matching;
    }

    drawing::drawAtPoint_withAttributes("SEQ       BEAT       SRC     MESSAGE", drawing::makePoint(34.0, 100.0), labels);
    drawing::setStroke(style.grid);
    drawLine(drawing::makePoint(34.0, 116.0), drawing::makePoint(762.0, 116.0),
        style.grid, 0.75);
    const uint32_t count = std::min<uint32_t>(matching, kVisibleLines);
    const uint32_t first = matching > kVisibleLines
        ? matching % kVisibleLines : 0u;
    if (count == 0u) {
        drawing::drawAtPoint_withAttributes("NO MATCHING MIDI OUTPUT YET", drawing::makePoint(34.0, 128.0), values);
    }
    for (uint32_t row = 0u; row < count; ++row) {
        const TraceLine& line = lines[(first + row) % kVisibleLines];
        const EventKind kind = static_cast<EventKind>(line.packed & 3u);
        const uint32_t relay = (line.packed >> 2u) & 7u;
        const uint32_t channel = (line.packed >> 5u) & 15u;
        const uint32_t data1 = (line.packed >> 9u) & 127u;
        const uint32_t data2 = (line.packed >> 16u) & 127u;
        char message[96] {};
        if (kind == EventKind::ControlChange) {
            std::snprintf(message, sizeof(message),
                "%06llu  %8.3f  R%u CH%02u  CC %03u = %03u",
                static_cast<unsigned long long>(line.sequence), line.beat,
                relay + 1u, channel + 1u, data1, data2);
        } else {
            char note[32] {};
            midiNoteText(static_cast<int>(data1), note, sizeof(note));
            std::snprintf(message, sizeof(message),
                "%06llu  %8.3f  R%u CH%02u  %s %-9s V%03u",
                static_cast<unsigned long long>(line.sequence), line.beat,
                relay + 1u, channel + 1u,
                kind == EventKind::NoteOn ? "ON " : "OFF", note, data2);
        }
        VSTGUI::CColor  marker = kind == EventKind::NoteOn ? style.accent
            : (kind == EventKind::NoteOff
                ? drawing::color(0x587d81) : style.grid);
        drawing::setFill(marker);
        drawing::rectFill(drawing::makeRect(28.0, 127.0 + static_cast<double>(row) * 17.0,
            3.0, 12.0));
        drawing::drawAtPoint_withAttributes(drawing::stringFromUtf8(message), drawing::makePoint(36.0,
                125.0 + static_cast<double>(row) * 17.0), values);
    }
    const uint64_t sent = plugin.sentEvents.load(std::memory_order_relaxed);
    const uint64_t dropped = plugin.droppedEvents.load(std::memory_order_relaxed);
    drawing::drawAtPoint_withAttributes("REAL OUTPUT EVENTS / NEWEST AT BOTTOM", drawing::makePoint(34.0, 519.0), values);
    drawing::drawRightStatus(
        drawing::stringFormat("SENT %llu  DROP %llu",
            static_cast<unsigned long long>(sent),
            static_cast<unsigned long long>(dropped)),
        762.0, 519.0, values, 16.0);

}

void drawOriginal(drawing::Rect dirtyRect)  {
    (void)dirtyRect;
    if (!_plugin) return;
    auto& plugin = *static_cast<Plugin*>(_plugin);
    const auto style = drawing::softTextStyle();
    drawing::Attrs  title = drawing::softTitleAttrs();
    drawing::Attrs  labels = drawing::softLabelAttrs();
    drawing::Attrs  values = drawing::softValueAttrs();
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());

    const bool playing = plugin.visualPlaying.load(std::memory_order_relaxed);
    const double beat = plugin.visualBeat.load(std::memory_order_relaxed);
    const double energy = plugin.visualEnergy.load(std::memory_order_relaxed);
    const uint32_t cell = plugin.visualCell.load(std::memory_order_relaxed);
    const uint32_t latticeCells = s3g::relay::latticeCellCount(
        static_cast<uint32_t>(std::lround(
            publishedValue(plugin, kLatticeDepth))));
    const auto titleBand = drawing::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    drawing::drawEncoderTitleBand("s3g RELAY",
        drawing::stringFromUtf8(_presetName),
        drawing::stringFormat("%@  BEAT %.2f  CELL %u/%u  ENERGY %.2f",
            playing ? "RUN" : "STILL", beat,
            cell % latticeCells + 1u, latticeCells, energy),
        titleBand, title, labels, values, style);
    drawing::drawAtPoint_withAttributes("NEURAL CONTROL ECOLOGY / MIDI", drawing::makePoint(98.0, 15.0), labels);

    const drawing::Rect graph = graphPanelRect();
    const drawing::Rect conduct = conductPanelRect();
    const drawing::Rect pitch = pitchPanelRect();
    const drawing::Rect climate = climatePanelRect();
    const drawing::Rect relays = relayPanelRect();
    drawing::drawPanelFrame(graph.origin.x, graph.origin.y,
        graph.size.width, graph.size.height, style);
    drawing::drawPanelHeader("OUTPUT VIEW", true,
        graph.origin.x, graph.origin.y, graph.size.width, kPanelHeaderHeight,
        labels, style);
    static drawing::String  const visualPages[5] = {
        "FIELD", "LEARNING", "FORM", "MIDI", "INJECT",
    };
    for (uint32_t page = 0u; page < 5u; ++page) {
        drawing::drawToolboxHeaderButton(visualPageTabRect(page),
            drawing::makeRect(graph.origin.x, graph.origin.y, graph.size.width,
                kPanelHeaderHeight), visualPages[page],
            _visualPage == static_cast<int>(page), labels, style);
    }
    drawing::drawPanelFrame(conduct.origin.x, conduct.origin.y,
        conduct.size.width, conduct.size.height, style);
    drawing::drawPanelHeader("CONDUCT", true, conduct.origin.x,
        conduct.origin.y, conduct.size.width, kPanelHeaderHeight, labels, style);
    const bool crystallized = plugin.formHold.load(std::memory_order_acquire);
    drawing::drawToolboxHeaderActionButton(crystallizeRect(),
        drawing::makeRect(conduct.origin.x, conduct.origin.y, conduct.size.width,
            kPanelHeaderHeight), crystallized ? "THAW" : "CRYSTALLIZE",
        labels, style);
    drawing::drawPanelFrame(pitch.origin.x, pitch.origin.y,
        pitch.size.width, pitch.size.height, style);
    drawing::drawPanelHeader("PITCH / LOGIC SCALE", true,
        pitch.origin.x, pitch.origin.y, pitch.size.width,
        kPanelHeaderHeight, labels, style);
    drawing::drawPanelFrame(climate.origin.x, climate.origin.y,
        climate.size.width, climate.size.height, style);
    drawing::drawPanelHeader("CLIMATE / LONG FORM", true,
        climate.origin.x, climate.origin.y, climate.size.width,
        kPanelHeaderHeight, labels, style);
    drawing::drawPanelFrame(relays.origin.x, relays.origin.y,
        relays.size.width, relays.size.height, style);
    drawing::drawPanelHeader("RELAYS", true, relays.origin.x,
        relays.origin.y, relays.size.width, kPanelHeaderHeight, labels, style);

    if (_visualPage == 0) {
        drawNeuralGraph_style_labels_values(plugin, style, labels, values);
    } else if (_visualPage == 1) {
        drawLearningPlate_style_labels_values(plugin, style, labels, values);
    } else if (_visualPage == 2) {
        drawFormDeck_style_labels_values(plugin, style, labels, values);
    } else if (_visualPage == 3) {
        static drawing::String  const consoleFilters[3] = {
            "ALL", "NOTES", "CC",
        };
        for (uint32_t filter = 0u; filter < 3u; ++filter) {
            drawing::drawToolboxHeaderButton(consoleFilterRect(filter),
                graph, consoleFilters[filter],
                _consoleFilter == static_cast<int>(filter),
                labels, style);
        }
        drawing::drawToolboxHeaderActionButton(consoleClearRect(),
            graph, "CLEAR", labels, style);
        drawMidiConsole_style_labels_values(plugin, style, labels, values);
    } else {
        drawInjectionView_style_labels_values(plugin, style, labels, values);
    }
    for (uint32_t index = 0u; index < kMidiInputMode; ++index) {
        const drawing::Rect row = globalRowRect(index);
        double panelX = 0.0;
        double panelWidth = 0.0;
        parameterPanelGeometry(index, panelX, panelWidth);
        drawParameter(plugin, index, row, panelX, panelWidth);
    }
    if (_visualPage == 4) {
        for (uint32_t index = kMidiInputMode;
             index <= kMidiInputDecay; ++index) {
            const drawing::Rect row = globalRowRect(index);
            double panelX = 0.0;
            double panelWidth = 0.0;
            parameterPanelGeometry(index, panelX, panelWidth);
            drawParameter(plugin, index, row, panelX, panelWidth);
        }
    }
    drawing::drawAtPoint_withAttributes("PER-RELAY MODE / REGISTER + RECEPTOR + CLIMATE ADDRESS", drawing::makePoint(806.0, 514.0), values);
    for (uint32_t relay = 0u; relay < s3g::relay::kRelayCount; ++relay) {
        drawing::drawToolboxHeaderButton(relayTabRect(relay),
            drawing::makeRect(relays.origin.x, relays.origin.y, relays.size.width,
                kPanelHeaderHeight),
            drawing::stringFormat("R%u", relay + 1u),
            relay == static_cast<uint32_t>(_selectedRelay), labels, style);
    }
    static drawing::String  const relayPages[2] = { "VOICE", "CC ROUTING" };
    for (uint32_t page = 0u; page < 2u; ++page) {
        drawing::drawToolboxHeaderButton(relayModeTabRect(page),
            relays, relayPages[page],
            _relayPage == static_cast<int>(page), labels, style);
    }
    const uint32_t base = kGlobalParamCount
        + static_cast<uint32_t>(_selectedRelay) * kRelayParamCount;
    const auto drawRelayControl = [&](uint32_t local) {
        const drawing::Rect row = relayRowRect(local);
        double panelX = 0.0;
        double panelWidth = 0.0;
        parameterPanelGeometry(base + local, panelX, panelWidth);
        drawParameter(plugin, base + local, row, panelX, panelWidth);
    };
    if (_relayPage == 0) {
        for (uint32_t local : kRelayVoiceControls) drawRelayControl(local);
    } else {
        for (uint32_t local : kRelayCcControls) drawRelayControl(local);
    }
    drawing::drawAtPoint_withAttributes((_relayPage == 0
        ? "COMPARATOR RISE -> ARTICULATION / NOTE -> FIELD IMPULSE"
        : "TWO GENERIC CC LANES / SOURCE -> RANGE -> CURVE -> SLEW"), drawing::makePoint(34.0, 861.0), values);
    drawing::drawAtPoint_withAttributes("DEALER LAW + DWELL + GESTATION SHAPE LONG CYCLES", drawing::makePoint(806.0, 861.0), values);
    drawOpenMenu_style(values, style);

}

int paramAtPoint(drawing::Point point)  {
    for (uint32_t index = 0u; index < kMidiInputMode; ++index) {
        if (drawing::contains(point, parameterInteractionRect(index)))
            return static_cast<int>(index);
    }
    if (_visualPage == 4) {
        for (uint32_t index = kMidiInputMode;
             index <= kMidiInputDecay; ++index) {
            if (drawing::contains(point, parameterInteractionRect(index)))
                return static_cast<int>(index);
        }
    }
    const uint32_t base = kGlobalParamCount
        + static_cast<uint32_t>(_selectedRelay) * kRelayParamCount;
    const auto hitControls = [&](const auto& controls) -> int {
        for (uint32_t local : controls) {
            if (drawing::contains(point, parameterInteractionRect(base + local)))
                return static_cast<int>(base + local);
        }
        return -1;
    };
    const int hit = _relayPage == 0
        ? hitControls(kRelayVoiceControls) : hitControls(kRelayCcControls);
    if (hit >= 0) return hit;
    return -1;

}

void updateParameter_point(int index, drawing::Point point)  {
    if (index < 0 || index >= static_cast<int>(kParamCount)) return;
    auto& plugin = *static_cast<Plugin*>(_plugin);
    const uint32_t unsignedIndex = static_cast<uint32_t>(index);
    const ParamSpec spec = paramSpec(unsignedIndex);
    const drawing::Rect rect = parameterSliderTrackRect(unsignedIndex);
    const double normalized = std::clamp(
        static_cast<double>((point.x - rect.origin.x) / rect.size.width),
        0.0, 1.0);
    double value = spec.minimum + normalized * (spec.maximum - spec.minimum);
    if (spec.stepped) value = std::round(value);
    queueGuiValue(plugin, spec.id, value);
    markCustomPreset("CUSTOM");
    setNeedsDisplay(true);

}

void mouseDown(drawing::Event*  event)  {
    if (!_plugin) return;
    const drawing::Point point = convertPoint_fromView(event->locationInWindow(), nullptr);
    auto& plugin = *static_cast<Plugin*>(_plugin);
    if (_openMenu >= 0) {
        const bool factoryMenu = _openMenu == kFactoryPresetMenuId;
        const uint32_t columns = factoryMenu ? 1u : parameterMenuColumns(
            static_cast<uint32_t>(_openMenu));
        const int hit = columns > 1u
            ? drawing::multiColumnDropdownHitIndex(
                point, openMenuRect(), 18.0, _menuItemCount, columns)
            : drawing::dropdownHitIndex(
                point, openMenuRect(), 18.0, _menuItemCount);
        if (hit >= 0) {
            if (factoryMenu) {
                if (!applyFactoryPreset(hit)) beep();
            } else {
                const uint32_t index = static_cast<uint32_t>(_openMenu);
                const clap_id id = paramIdForIndex(index);
                queueGuiBegin(plugin, id);
                queueGuiValue(plugin, id,
                    parameterMenuValue(index, static_cast<uint32_t>(hit)));
                queueGuiEnd(plugin, id);
                markCustomPreset("CUSTOM");
            }
        }
        _openMenu = -1;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        setNeedsDisplay(true);
        return;
    }
    const auto titleBand = drawing::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    if (drawing::contains(point,
            drawing::cocoaRect(titleBand.presetMenu))) {
        _openMenu = kFactoryPresetMenuId;
        _hoverMenuItem = -1;
        _menuItemCount = kFactoryPresetCount;
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point,
            drawing::cocoaRect(titleBand.loadButton))) {
        drawing::String name;
        if (presetDialog(false, &name)) {
            markCustomPreset(!name.empty() ? name.c_str() : "CUSTOM");
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
            markCustomPreset(!name.empty() ? name.c_str() : "CUSTOM");
        } else {
            beep();
        }
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point,
            drawing::cocoaRect(titleBand.randomButton))) {
        if (!applySafeRandom()) beep();
        return;
    }
    for (uint32_t page = 0u; page < 5u; ++page) {
        if (drawing::contains(point, visualPageTabRect(page))) {
            (void)selectVisualPage(static_cast<int>(page));
            return;
        }
    }
    if (_visualPage == 1) {
        for (uint32_t mode = 0u; mode < 2u; ++mode) {
            if (drawing::contains(point, learningModeRect(mode))) {
                _matrixMode = static_cast<int>(mode);
                setNeedsDisplay(true);
                return;
            }
        }
        for (uint32_t target = 0u;
             target < s3g::relay::kClusterCount; ++target) {
            for (uint32_t source = 0u;
                 source < s3g::relay::kClusterCount; ++source) {
                if (drawing::contains(point, learningCellRect(target, source))) {
                    const int cell = static_cast<int>(
                        target * s3g::relay::kClusterCount + source);
                    _selectedMatrixCell = _selectedMatrixCell == cell
                        ? -1 : cell;
                    setNeedsDisplay(true);
                    return;
                }
            }
        }
    }
    if (_visualPage == 2) {
        const uint32_t depthIndex = static_cast<uint32_t>(std::lround(
            publishedValue(plugin, kLatticeDepth)));
        const uint32_t planes = s3g::relay::latticePlaneCount(depthIndex);
        for (uint32_t item = 0u; item <= planes; ++item) {
            if (drawing::contains(point, formPlaneButtonRect(item))) {
                _formPlane = item == 0u ? -1
                    : static_cast<int>(item - 1u);
                setNeedsDisplay(true);
                return;
            }
        }
    }
    if (_visualPage == 3) {
        for (uint32_t filter = 0u; filter < 3u; ++filter) {
            if (drawing::contains(point, consoleFilterRect(filter))) {
                _consoleFilter = static_cast<int>(filter);
                setNeedsDisplay(true);
                return;
            }
        }
        if (drawing::contains(point, consoleClearRect())) {
            _consoleFloor = plugin.midiTraceWrite.load(
                std::memory_order_acquire);
            setNeedsDisplay(true);
            return;
        }
    }
    if (drawing::contains(point, crystallizeRect())) {
        const bool crystallized = plugin.formHold.load(
            std::memory_order_acquire);
        if (!crystallized) {
            plugin.thawMemory.store(publishedValue(plugin, kMemory),
                std::memory_order_relaxed);
            plugin.heldFormBeat.store(plugin.visualFormBeat.load(
                std::memory_order_relaxed), std::memory_order_release);
            plugin.hasThawMemory.store(true, std::memory_order_relaxed);
        }
        const bool hasThawMemory = plugin.hasThawMemory.load(
            std::memory_order_relaxed);
        const double memory = crystallized
            ? (hasThawMemory
                ? plugin.thawMemory.load(std::memory_order_relaxed)
                : kDefaultConfig.memory)
            : 1.0;
        const double freeze = crystallized ? 0.0 : 1.0;
        plugin.formHold.store(!crystallized, std::memory_order_release);
        for (const auto& change : {
                 std::pair<uint32_t, double> { uint32_t(kMemory), memory },
                 std::pair<uint32_t, double> { uint32_t(kFreeze), freeze },
             }) {
            const uint32_t index = change.first;
            const clap_id id = paramIdForIndex(index);
            queueGuiBegin(plugin, id);
            queueGuiValue(plugin, id, change.second);
            queueGuiEnd(plugin, id);
        }
        if (crystallized)
            plugin.hasThawMemory.store(false, std::memory_order_relaxed);
        markCustomPreset("CUSTOM");
        setNeedsDisplay(true);
        return;
    }
    for (uint32_t page = 0u; page < 2u; ++page) {
        if (drawing::contains(point, relayModeTabRect(page))) {
            (void)selectRelayPage(static_cast<int>(page));
            return;
        }
    }
    for (uint32_t relay = 0u; relay < s3g::relay::kRelayCount; ++relay) {
        if (drawing::contains(point, relayTabRect(relay))
            || (_visualPage == 0
                && drawing::contains(point, relayGlyphRect(relay)))) {
            _selectedRelay = static_cast<int>(relay);
            _dragParam = -1;
            setNeedsDisplay(true);
            return;
        }
    }
    _dragParam = paramAtPoint(point);
    if (_dragParam < 0) return;
    const uint32_t index = static_cast<uint32_t>(_dragParam);
    const ParamSpec spec = paramSpec(index);
    if (parameterIsMenu(index)) {
        (void)openParameterMenu(static_cast<int>(index));
        return;
    }
    queueGuiBegin(plugin, spec.id);
    if (parameterIsBinary(index)) {
        queueGuiValue(plugin, spec.id,
            publishedValue(plugin, index) >= 0.5 ? 0.0 : 1.0);
        queueGuiEnd(plugin, spec.id);
        markCustomPreset("CUSTOM");
        _dragParam = -1;
    } else {
        double defaultValue = spec.defaultValue;
        if (drawing::sliderDoubleClickDefault(event,
                &plugin.plugin, spec.id, &defaultValue)) {
            queueGuiValue(plugin, spec.id, defaultValue);
            queueGuiEnd(plugin, spec.id);
            _dragParam = -1;
            markCustomPreset("CUSTOM");
        } else {
            updateParameter_point(_dragParam, point);
        }
    }

}

void mouseDragged(drawing::Event*  event)  {
    if (_dragParam < 0) return;
    const drawing::Point point = convertPoint_fromView(event->locationInWindow(), nullptr);
    updateParameter_point(_dragParam, point);

}

void mouseUp(drawing::Event*  event)  {
    (void)event;
    if (_dragParam < 0 || !_plugin) return;
    auto& plugin = *static_cast<Plugin*>(_plugin);
    queueGuiEnd(plugin, paramIdForIndex(static_cast<uint32_t>(_dragParam)));
    _dragParam = -1;

}

void mouseMoved(drawing::Event*  event)  {
    if (_openMenu < 0) return;
    const drawing::Point point = convertPoint_fromView(event->locationInWindow(), nullptr);
    const uint32_t columns = _openMenu == kFactoryPresetMenuId ? 1u
        : parameterMenuColumns(static_cast<uint32_t>(_openMenu));
    const int hover = columns > 1u
        ? drawing::multiColumnDropdownHitIndex(
            point, openMenuRect(), 18.0, _menuItemCount, columns)
        : drawing::dropdownHitIndex(
            point, openMenuRect(), 18.0, _menuItemCount);
    if (hover != _hoverMenuItem) {
        _hoverMenuItem = hover;
        setNeedsDisplay(true);
    }

}

};
}
s3g::portable_gui::foundation::ContentView* makePortableCanvas(Plugin& p) { return new midi_tool_canvas::Editor(p); }
#include "s3g_clap_canvas_gui.inc"
