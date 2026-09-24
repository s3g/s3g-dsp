namespace sample_kit_canvas {

using namespace s3g::portable_gui::canvas;
namespace foundation = s3g::portable_gui::foundation;

class Editor final : public View {
public:
    explicit Editor(Plugin& instance)
        : View(kGuiWidth, kGuiHeight), p(instance)
    {
    }

    void service() override
    {
#if defined(S3G_SAMPLE_FILE_WORKER)
        serviceLoads(p);
#endif
    }

    void paint() override
    {
        paintHeader();
        paintTabs();
        if (page == 1u) paintChopPage();
        else if (page == 5u) paintMixer();
        else {
            paintPads();
            paintSelectedPanel();
        }
    }

    bool dropped(const std::vector<std::string>& paths, CPoint point) override
    {
        if (paths.empty()) return false;
        if (page == 1u) {
#if defined(S3G_SAMPLE_FILE_WORKER)
            queueChopSourceLoad(p, paths.front());
            return true;
#else
            return false;
#endif
        }
        const int hitPad = padAt(point);
        const std::size_t pad = hitPad < 0
            ? selectedPad : static_cast<std::size_t>(hitPad);
        if (hitPad >= 0 && static_cast<std::size_t>(hitPad) != selectedPad) {
            selectedPad = static_cast<std::size_t>(hitPad);
            selectedVariation = 0u;
        }
        std::size_t variation = paths.size() > 1u ? 0u : selectedVariation;
        bool accepted = false;
        for (const auto& path : paths) {
            if (variation >= s3g::sample::kSampleKitVariationCount)
                break;
#if defined(S3G_SAMPLE_FILE_WORKER)
            queueSampleLoad(p, pad, variation, path);
            accepted = true;
#endif
            ++variation;
        }
        return accepted;
    }

    void wheel(MouseWheelEvent& event) override
    {
        if (page != 1u) return;
        std::shared_ptr<const SampleAsset> source;
        {
            std::lock_guard<std::mutex> lock(p.statusMutex);
            source = p.chopSourceAsset;
        }
        if (!source) return;
        const CPoint point = event.mousePosition;
        if (!foundation::contains(chopWaveformRect(), point)
            && !foundation::contains(chopNavigatorRect(), point)) return;
        const uint32_t total = source->frameCount();
        const double deltaX = event.deltaX * 10.0;
        const double deltaY = event.deltaY * 10.0;
        const bool horizontal = std::abs(deltaX) > std::abs(deltaY)
            || event.modifiers.has(VSTGUI::ModifierKey::Shift);
        if (horizontal && chopZoom > 1.0) {
            const uint32_t visible = chopVisibleFrames(total);
            const double gesture = std::abs(deltaX) > 0.0
                ? deltaX : deltaY;
            chopPanRemainder += gesture * static_cast<double>(visible)
                / chopWaveformRect().getWidth() * 0.35;
            const int64_t frameDelta
                = static_cast<int64_t>(chopPanRemainder);
            chopPanRemainder -= static_cast<double>(frameDelta);
            const int64_t maximum = total > visible
                ? static_cast<int64_t>(total - visible) : 0;
            chopVisibleStart = static_cast<uint32_t>(std::clamp<int64_t>(
                static_cast<int64_t>(chopVisibleStart) + frameDelta,
                0, maximum));
            event.consumed = true;
            return;
        }
        if (deltaY == 0.0) return;
        const uint32_t anchor = chopFrameForX(point.x, total);
        const double previousZoom = chopZoom;
        chopZoom *= deltaY > 0.0 ? 1.25 : 0.8;
        chopZoom = std::clamp(chopZoom, 1.0,
            std::max(1.0, static_cast<double>(total) / 32.0));
        if (chopZoom != previousZoom) {
            const double fraction = std::clamp(
                (point.x - chopWaveformRect().left)
                    / chopWaveformRect().getWidth(), 0.0, 1.0);
            const uint32_t visible = chopVisibleFrames(total);
            const int64_t desired = static_cast<int64_t>(anchor)
                - static_cast<int64_t>(std::llround(
                    fraction * static_cast<double>(visible)));
            chopVisibleStart = static_cast<uint32_t>(std::clamp<int64_t>(
                desired, 0, total > visible
                    ? static_cast<int64_t>(total - visible) : 0));
        }
        event.consumed = true;
    }

    void onMouseDownEvent(MouseDownEvent& event) override
    {
        if (page == 1u && event.buttonState.isLeft()
            && event.clickCount >= 2
            && foundation::contains(chopWaveformRect(),
                event.mousePosition)) {
            bool added = false;
            bool hasSource = false;
            std::size_t regionCount = 0u;
            {
                std::lock_guard<std::mutex> lock(p.statusMutex);
                const auto& source = p.chopSourceAsset;
                hasSource = static_cast<bool>(source);
                if (source) {
                    const double position = chopPositionForX(
                        event.mousePosition.x, source->frameCount());
                    added = s3g::sample::addChopMarker(
                        p.chopLayout, position);
                    regionCount = p.chopLayout.sliceCount;
                    if (added) {
                        for (std::size_t marker = 1u;
                             marker < p.chopLayout.sliceCount; ++marker) {
                            if (std::abs(p.chopLayout.boundaries[marker]
                                    - position) < 1.0e-9) {
                                selectedChop = marker;
                                break;
                            }
                        }
                    }
                }
            }
            if (added) {
                fileMessage = format("MANUAL MARKER ADDED  //  %zu / 16 REGIONS",
                    regionCount);
                markStateDirty(p);
            } else if (!hasSource) {
                fileMessage = "LOAD A CHOP SOURCE FIRST";
            } else if (regionCount >= s3g::sample::kSampleKitMaximumChops) {
                fileMessage = "MAXIMUM 16 CHOP REGIONS";
            } else {
                fileMessage = "MARKER TOO CLOSE TO AN EXISTING BOUNDARY";
            }
            invalid();
            event.consumed = true;
            return;
        }
        View::onMouseDownEvent(event);
    }

    void pointerUp(MouseUpEvent&) override
    {
        pressedButton = false;
        pressedPad = -1;
        pressedChop = -1;
    }

private:
    static constexpr double kPadLeft = 22.0;
    static constexpr double kPadTop = 104.0;
    static constexpr double kPadWidth = 142.0;
    static constexpr double kPadHeight = 132.0;
    static constexpr double kPadGap = 8.0;
    static constexpr double kPanelLeft = 628.0;
    static constexpr double kPanelWidth = 470.0;

    CRect chopWaveformRect() const
    {
        return rect(28.0, 150.0, 1064.0, 170.0);
    }

    CRect chopNavigatorRect() const
    {
        return rect(28.0, 324.0, 1064.0, 8.0);
    }

    uint32_t chopVisibleFrames(uint32_t total)
    {
        if (total == 0u) return 0u;
        chopZoom = std::clamp(chopZoom, 1.0,
            std::max(1.0, static_cast<double>(total) / 32.0));
        return std::clamp<uint32_t>(static_cast<uint32_t>(std::ceil(
            static_cast<double>(total) / chopZoom)), 1u, total);
    }

    uint32_t chopClampedStart(uint32_t total)
    {
        const uint32_t visible = chopVisibleFrames(total);
        chopVisibleStart = std::min(chopVisibleStart,
            total > visible ? total - visible : 0u);
        return chopVisibleStart;
    }

    double chopXForPosition(double position, uint32_t total)
    {
        const CRect bounds = chopWaveformRect();
        const uint32_t visible = chopVisibleFrames(total);
        const uint32_t start = chopClampedStart(total);
        const double frame = std::clamp(position, 0.0, 1.0)
            * static_cast<double>(total);
        return bounds.left + bounds.getWidth()
            * (frame - static_cast<double>(start))
            / static_cast<double>(visible);
    }

    uint32_t chopFrameForX(double x, uint32_t total)
    {
        const CRect bounds = chopWaveformRect();
        const uint32_t visible = chopVisibleFrames(total);
        const uint32_t start = chopClampedStart(total);
        const double fraction = std::clamp(
            (x - bounds.left) / bounds.getWidth(), 0.0, 1.0);
        return std::min<uint32_t>(total - 1u,
            start + static_cast<uint32_t>(std::llround(
                fraction * static_cast<double>(visible - 1u))));
    }

    double chopPositionForX(double x, uint32_t total)
    {
        return static_cast<double>(chopFrameForX(x, total))
            / static_cast<double>(std::max(1u, total));
    }

    CRect chopViewportHandle(uint32_t total)
    {
        CRect track = chopNavigatorRect();
        track.inset(1.0, 1.0);
        if (total == 0u) return track;
        const uint32_t visible = chopVisibleFrames(total);
        const uint32_t start = chopClampedStart(total);
        const double width = std::clamp(track.getWidth()
                * static_cast<double>(visible) / static_cast<double>(total),
            30.0, track.getWidth());
        const double travel = track.getWidth() - width;
        const double position = total > visible
            ? static_cast<double>(start)
                / static_cast<double>(total - visible)
            : 0.0;
        return rect(track.left + travel * position, track.top,
            width, track.getHeight());
    }

    void setChopViewportOrigin(double origin, uint32_t total)
    {
        CRect track = chopNavigatorRect();
        track.inset(1.0, 1.0);
        const uint32_t visible = chopVisibleFrames(total);
        const CRect handle = chopViewportHandle(total);
        const double travel = track.getWidth() - handle.getWidth();
        if (travel <= 0.0 || total <= visible) {
            chopVisibleStart = 0u;
            return;
        }
        const double position = std::clamp(
            (origin - track.left) / travel, 0.0, 1.0);
        chopVisibleStart = static_cast<uint32_t>(std::llround(position
            * static_cast<double>(total - visible)));
    }

    CRect padRect(std::size_t pad) const
    {
        const std::size_t column = pad % 4u;
        const std::size_t row = 3u - pad / 4u;
        return rect(kPadLeft + column * (kPadWidth + kPadGap),
            kPadTop + row * (kPadHeight + kPadGap),
            kPadWidth, kPadHeight);
    }

    CRect selectedWaveformRect() const
    {
        return rect(kPanelLeft + 16.0, kPadTop + 56.0,
            kPanelWidth - 32.0, 132.0);
    }

    int padAt(CPoint point) const
    {
        for (std::size_t pad = 0u;
             pad < s3g::sample::kSampleKitPadCount; ++pad)
            if (foundation::contains(padRect(pad), point))
                return static_cast<int>(pad);
        return -1;
    }

    std::shared_ptr<const SampleAsset> asset(std::size_t pad,
        std::size_t variation) const
    {
        std::lock_guard<std::mutex> lock(p.statusMutex);
        return pad < p.controlAssets.size()
                && variation < p.controlAssets[pad].size()
            ? p.controlAssets[pad][variation] : nullptr;
    }

    std::size_t variationCount(std::size_t pad) const
    {
        std::lock_guard<std::mutex> lock(p.statusMutex);
        if (pad >= p.controlAssets.size()) return 0u;
        return static_cast<std::size_t>(std::count_if(
            p.controlAssets[pad].begin(), p.controlAssets[pad].end(),
            [](const auto& item) { return static_cast<bool>(item); }));
    }

    std::size_t firstLoadedVariation(std::size_t pad) const
    {
        std::lock_guard<std::mutex> lock(p.statusMutex);
        if (pad >= p.controlAssets.size()) return 0u;
        for (std::size_t variation = 0u;
             variation < p.controlAssets[pad].size(); ++variation)
            if (p.controlAssets[pad][variation]) return variation;
        return 0u;
    }

    std::string status(std::size_t pad, std::size_t variation) const
    {
        std::lock_guard<std::mutex> lock(p.statusMutex);
        return pad < p.statuses.size() && variation < p.statuses[pad].size()
            ? p.statuses[pad][variation] : std::string();
    }

    std::string path(std::size_t pad, std::size_t variation) const
    {
        std::lock_guard<std::mutex> lock(p.statusMutex);
        return pad < p.samplePaths.size()
                && variation < p.samplePaths[pad].size()
            ? p.samplePaths[pad][variation] : std::string();
    }

    std::string storageName() const
    {
        std::lock_guard<std::mutex> lock(p.statusMutex);
        return s3g::sample_storage::storageModeName(p.storageMode);
    }

    void paintHeader()
    {
        foundation::drawPluginTitle(*context, "s3g SAMPLE KIT 32",
            rect(22.0, 14.0, 320.0, 22.0), titleFont);
        const float peak = std::clamp(p.outputPeak.load(
            std::memory_order_relaxed), 0.0f, 1.0f);
        fill(rect(360.0, 18.0, 180.0, 8.0), color(0x171717));
        fill(rect(360.0, 18.0, 180.0 * peak, 8.0),
            peak > 0.92f ? color(0xd77a58) : color(0x78a89a));
        text(format("%u VOICES  //  %d OUT",
                p.activeVoices.load(std::memory_order_relaxed),
                static_cast<int>(std::lround(paramValue(
                    p, kActivePairsParamId))) * 2),
            rect(552.0, 12.0, 190.0, 20.0), style.value);
        if (!fileMessage.empty())
            text(fileMessage, rect(552.0, 30.0, 190.0, 12.0),
                style.dim, kLeftText, smallFont);
        button(rect(760.0, 10.0, 92.0, 24.0), "LOAD KIT",
            [this] { chooseKit(false); });
        button(rect(860.0, 10.0, 92.0, 24.0), "SAVE KIT",
            [this] { chooseKit(true); });
        button(rect(960.0, 10.0, 138.0, 24.0), "KILL ALL",
            [this] {
                p.killRequested.store(true, std::memory_order_release);
                requestProcess(p);
            }, false);
    }

    void paintTabs()
    {
        constexpr std::array<const char*, 7u> names {{
            "KIT", "CHOP", "SOUND", "AMP / FILTER", "NATURAL", "MIXER",
            "OUTPUT",
        }};
        constexpr double width = 111.0;
        for (std::size_t index = 0u; index < names.size(); ++index)
            button(rect(22.0 + index * (width + 5.0), 48.0,
                    width, 34.0), names[index],
                [this, index] { page = index; }, page == index);
        text("16 STEREO PADS  //  MIDI 36-51 AT DEFAULT BASE",
            rect(836.0, 54.0, 262.0, 22.0), style.label, kRightText);
    }

    void paintPads()
    {
        for (std::size_t pad = 0u;
             pad < s3g::sample::kSampleKitPadCount; ++pad) {
            const CRect bounds = padRect(pad);
            const bool selected = pad == selectedPad;
            const std::size_t variants = variationCount(pad);
            const bool loaded = variants != 0u;
            const bool muted = paramValue(p, padParamId(pad, kPadMute)) >= 0.5;
            const bool soloed = paramValue(p, padParamId(pad, kPadSolo)) >= 0.5;
            const float activity = std::clamp(p.padPeaks[pad].load(
                std::memory_order_relaxed), 0.0f, 1.0f);
            const bool pressed = pressedPad == static_cast<int>(pad);
            const bool sounding = activity > 0.002f;
            fill(bounds, color(pressed ? 0x49382f
                    : sounding ? 0x263a34
                    : selected ? 0x303635 : 0x181a1a));
            stroke(bounds, color(pressed ? 0xda8b68
                    : sounding ? 0x77a99b
                    : selected ? 0x89afa5 : 0x4b4d4c),
                pressed || sounding || selected ? 2.0 : 1.0);
            fill(rect(bounds.left + 7.0, bounds.top + 7.0,
                bounds.getWidth() - 14.0, 5.0), color(0x0e0f0f));
            fill(rect(bounds.left + 7.0, bounds.top + 7.0,
                (bounds.getWidth() - 14.0) * activity, 5.0),
                color(activity > 0.9f ? 0xda8b68 : 0x77a99b));
            text(format("PAD %02zu", pad + 1u),
                rect(bounds.left + 10.0, bounds.top + 19.0,
                    70.0, 17.0), style.text);
            const int note = static_cast<int>(std::lround(paramValue(
                p, kBaseNoteParamId))) + static_cast<int>(pad);
            text(format("N%d", note),
                rect(bounds.right - 46.0, bounds.top + 19.0,
                    36.0, 17.0), style.label, kRightText);
            const std::size_t first = firstLoadedVariation(pad);
            const std::string file = sampleDisplayName(path(pad, first));
            text(variants > 1u ? format("%zu VARIANTS", variants)
                    : loaded ? upper(file) : "EMPTY",
                rect(bounds.left + 10.0, bounds.top + 48.0,
                    bounds.getWidth() - 20.0, 24.0),
                loaded ? style.value : style.dim, kCenterText);
            const int pair = static_cast<int>(std::lround(paramValue(
                p, padParamId(pad, kPadOutputPair))));
            const bool natural = paramValue(p,
                padParamId(pad, kPadNaturalEnabled)) >= 0.5;
            text(format("P%02d  %s%s  %s", pair,
                    muted ? "M" : "-", soloed ? "S" : "-",
                    natural ? "NAT" : "---"),
                rect(bounds.left + 10.0, bounds.bottom - 29.0,
                    bounds.getWidth() - 20.0, 18.0), style.label,
                kCenterText);
            hit(bounds, [this, pad] {
                pressedPad = static_cast<int>(pad);
                selectedPad = pad;
                selectedVariation = firstLoadedVariation(pad);
                p.pendingAuditions.fetch_or(1u << pad,
                    std::memory_order_release);
                requestProcess(p);
            });
        }
    }

    void paintSelectedPanel()
    {
        panel(rect(kPanelLeft, kPadTop, kPanelWidth, 552.0),
            page == 0u ? "SELECTED PAD / SAMPLE"
                : page == 2u ? "SELECTED PAD / SOUND"
                : page == 3u ? "SELECTED PAD / AMP + FILTER"
                : page == 4u ? "SELECTED PAD / NATURAL"
                : "MASTER / OUTPUT");
        text(format("PAD %02zu  //  MIDI %d", selectedPad + 1u,
                static_cast<int>(std::lround(paramValue(
                    p, kBaseNoteParamId)))
                    + static_cast<int>(selectedPad)),
            rect(kPanelLeft + 16.0, kPadTop + 23.0,
                kPanelWidth - 32.0, 22.0), style.text);
        if (page == 0u) paintKitPage();
        else if (page == 2u) paintSoundPage();
        else if (page == 3u) paintAmpPage();
        else if (page == 4u) paintNaturalPage();
        else paintEffectsPage();
    }

    void paintKitPage()
    {
        const auto current = asset(selectedPad, selectedVariation);
        drawWaveform(selectedWaveformRect(), current.get());
        text(upper(status(selectedPad, selectedVariation)),
            rect(kPanelLeft + 18.0, kPadTop + 194.0,
                kPanelWidth - 36.0, 24.0), style.value);
        paintVariationButtons(kPadTop + 222.0);
        button(rect(kPanelLeft + 16.0, kPadTop + 258.0, 116.0, 28.0),
            "LOAD SAMPLE", [this] { chooseSample(); });
        button(rect(kPanelLeft + 140.0, kPadTop + 258.0, 84.0, 28.0),
            "CLEAR", [this] {
                clearSample(p, selectedPad, selectedVariation);
            });
        button(rect(kPanelLeft + 232.0, kPadTop + 258.0, 222.0, 28.0),
            "STORE " + storageName(), [this] { cycleStorageMode(p); });
        double y = kPadTop + 314.0;
        sliderRow(padParamId(selectedPad, kPadGain), "GAIN", y);
        sliderRow(padParamId(selectedPad, kPadPan), "PAN", y += 42.0);
        menuRow(padParamId(selectedPad, kPadOutputPair), "OUTPUT", y += 42.0);
        menuRow(padParamId(selectedPad, kPadChokeGroup), "CHOKE", y += 42.0);
        togglePair(y += 42.0);
    }

    void paintSoundPage()
    {
        const auto current = asset(selectedPad, selectedVariation);
        drawWaveform(selectedWaveformRect(), current.get());
        paintVariationButtons(kPadTop + 198.0);
        double y = kPadTop + 240.0;
        sliderRow(padParamId(selectedPad, kPadStart), "START", y);
        sliderRow(padParamId(selectedPad, kPadEnd), "END", y += 42.0);
        sliderRow(padParamId(selectedPad, kPadTune), "TUNE", y += 42.0);
        sliderRow(padParamId(selectedPad, kPadVelocity), "VELOCITY", y += 42.0);
        menuRow(padParamId(selectedPad, kPadPlayMode), "PLAY MODE", y += 42.0);
        menuRow(padParamId(selectedPad, kPadTriggerMode), "TRIGGER", y += 42.0);
        sliderRow(padParamId(selectedPad, kPadLoopCrossfade),
            "LOOP XFADE", y += 42.0);
    }

    void paintNaturalPage()
    {
        const std::size_t variants = variationCount(selectedPad);
        const bool globalNatural = paramValue(
            p, kNaturalGlobalParamId) >= 0.5;
        const bool padNatural = paramValue(p,
            padParamId(selectedPad, kPadNaturalEnabled)) >= 0.5;
        text(format("%zu / %zu VARIATIONS LOADED", variants,
                s3g::sample::kSampleKitVariationCount),
            rect(kPanelLeft + 18.0, kPadTop + 60.0,
                kPanelWidth - 36.0, 20.0), style.value, kCenterText);
        double y = kPadTop + 94.0;
        menuRow(kNaturalGlobalParamId, "GLOBAL NAT", y);
        menuRow(padParamId(selectedPad, kPadNaturalEnabled),
            "PAD NATURAL", y += 38.0);
        menuRow(padParamId(selectedPad, kPadVariationMode),
            "SELECTION", y += 38.0);
        sliderRow(padParamId(selectedPad, kPadNaturalGain),
            "GAIN", y += 44.0);
        sliderRow(padParamId(selectedPad, kPadNaturalPitch),
            "PITCH", y += 44.0);
        sliderRow(padParamId(selectedPad, kPadNaturalStart),
            "START", y += 44.0);
        sliderRow(padParamId(selectedPad, kPadNaturalTiming),
            "TIMING", y += 44.0);
        const char* state = !globalNatural
            ? "GLOBAL NATURAL BYPASSED // HIT JITTER IS OFF"
            : !padNatural
                ? "PAD NATURAL BYPASSED // HIT JITTER IS OFF"
                : variants == 1u
                    ? "ONE SAMPLE // GAIN, PITCH, START + TIMING JITTER ACTIVE"
                    : "VARIATION SELECTION + HIT JITTER ACTIVE";
        text(state, rect(kPanelLeft + 18.0, y + 36.0,
            kPanelWidth - 36.0, 22.0),
            globalNatural && padNatural ? style.value : style.dim,
            kCenterText, smallFont);
        text(format("LAST HIT  GAIN %+.2f DB  //  PITCH %+.1f CT",
                p.lastNaturalGain[selectedPad].load(
                    std::memory_order_relaxed),
                p.lastNaturalPitch[selectedPad].load(
                    std::memory_order_relaxed)),
            rect(kPanelLeft + 18.0, y + 66.0,
                kPanelWidth - 36.0, 18.0), style.text,
            kCenterText, smallFont);
        text(format("START %+.2f MS  //  TIMING +%.2f MS",
                p.lastNaturalStart[selectedPad].load(
                    std::memory_order_relaxed),
                p.lastNaturalTiming[selectedPad].load(
                    std::memory_order_relaxed)),
            rect(kPanelLeft + 18.0, y + 88.0,
                kPanelWidth - 36.0, 18.0), style.text,
            kCenterText, smallFont);
    }

    void paintAmpPage()
    {
        double y = kPadTop + 68.0;
        sliderRow(padParamId(selectedPad, kPadAttack), "ATTACK", y);
        sliderRow(padParamId(selectedPad, kPadDecay), "DECAY", y += 44.0);
        sliderRow(padParamId(selectedPad, kPadSustain), "SUSTAIN", y += 44.0);
        sliderRow(padParamId(selectedPad, kPadRelease), "RELEASE", y += 44.0);
        drawEnvelope(rect(kPanelLeft + 20.0, y + 35.0,
            kPanelWidth - 40.0, 98.0));
        y += 150.0;
        menuRow(padParamId(selectedPad, kPadFilterType), "FILTER", y);
        sliderRow(padParamId(selectedPad, kPadFilterCutoff),
            "CUTOFF", y += 44.0);
        sliderRow(padParamId(selectedPad, kPadFilterResonance),
            "RESONANCE", y += 44.0);
    }

    void paintEffectsPage()
    {
        double y = kPadTop + 70.0;
        menuRow(kActivePairsParamId, "ACTIVE PAIRS", y);
        menuRow(kVelocityCurveParamId, "VELOCITY CURVE", y += 40.0);
        sliderRow(kMasterGainParamId, "MASTER", y += 40.0);
        sliderRow(kGlobalTuneParamId, "GLOBAL TUNE", y += 40.0);
        sliderRow(kDriveParamId, "DRIVE", y += 40.0);
        menuRow(kBitDepthParamId, "BIT DEPTH", y += 40.0);
        menuRow(kRateReductionParamId, "RATE HOLD", y += 40.0);
        menuRow(kNaturalGlobalParamId, "GLOBAL NAT", y += 40.0);
        const uint32_t seed = static_cast<uint32_t>(std::llround(
            paramValue(p, kRandomSeedParamId)));
        text("SEED", rect(kPanelLeft + 18.0, y + 39.0,
            108.0, 18.0), style.label);
        button(rect(kPanelLeft + 132.0, y + 34.0, 320.0, 24.0),
            format("%u  /  NEXT", seed), [this, seed] {
                queueGuiParamGesture(p, kRandomSeedParamId,
                    seed >= 2147483647u ? 1.0
                        : static_cast<double>(seed + 1u));
            });
        paintOutputPairs(rect(kPanelLeft + 18.0, y + 73.0,
            kPanelWidth - 36.0, 76.0));
        text("ROUTE PAIRS THROUGH DOWNSTREAM EFFECT PLUGINS",
            rect(kPanelLeft + 18.0, y + 151.0,
                kPanelWidth - 36.0, 20.0), style.dim, kCenterText);
    }

    void setChopMode(SampleKitChopMode mode)
    {
        {
            std::lock_guard<std::mutex> lock(p.statusMutex);
            p.chopMode = mode;
            regenerateChopLayoutLocked(p);
        }
        selectedChop = 0u;
        markStateDirty(p);
    }

    void setChopCount(uint8_t count, bool transient)
    {
        {
            std::lock_guard<std::mutex> lock(p.statusMutex);
            if (transient) p.chopTransientMaximum = count;
            else p.chopEqualCount = count;
            regenerateChopLayoutLocked(p);
        }
        selectedChop = 0u;
        markStateDirty(p);
    }

    void setChopBpm(double bpm)
    {
        {
            std::lock_guard<std::mutex> lock(p.statusMutex);
            p.chopSourceBpm = std::clamp(
                std::isfinite(bpm) ? bpm : 120.0, 20.0, 999.0);
            regenerateChopLayoutLocked(p);
        }
        selectedChop = 0u;
        markStateDirty(p);
    }

    void setChopPreRoll(double milliseconds)
    {
        {
            std::lock_guard<std::mutex> lock(p.statusMutex);
            p.chopPreRollTenthsMilliseconds = static_cast<uint8_t>(
                std::llround(std::clamp(milliseconds, 0.0, 20.0) * 10.0));
            if (p.chopMode == SampleKitChopMode::Transient)
                regenerateChopLayoutLocked(p);
        }
        markStateDirty(p);
    }

    void paintChopPreRoll(double y, uint8_t tenths)
    {
        constexpr double trackX = 812.0;
        constexpr double trackWidth = 180.0;
        const double milliseconds = static_cast<double>(tenths) * 0.1;
        text("PRE-ROLL", rect(700.0, y - 1.0, 104.0, 18.0), style.label);
        foundation::drawHorizontalSlider(*context,
            rect(trackX, y + 3.0, trackWidth, 8.0),
            milliseconds / 20.0, y, 14.0, style);
        const CRect hitBounds = rect(800.0, y - 7.0, 204.0, 28.0);
        drag(hitBounds, [this](CPoint point) {
            const double unit = std::clamp(
                (point.x - 812.0) / 180.0, 0.0, 1.0);
            setChopPreRoll(std::round(unit * 200.0) * 0.1);
        });
        const CRect valueBounds = rect(1002.0, y - 3.0, 90.0, 22.0);
        fill(valueBounds, style.cell);
        text(format("%.1f MS", milliseconds), valueBounds,
            style.value, kCenterText);
        number(valueBounds, milliseconds, [this](double value) {
            setChopPreRoll(value);
        });
    }

    void paintChopPage()
    {
        std::shared_ptr<const SampleAsset> source;
        SampleKitChopLayout layout;
        SampleKitChopMode mode;
        CutupsLaneMetadata analysis;
        std::string sourceStatus;
        uint8_t equalCount = 16u;
        uint8_t transientMaximum = 16u;
        uint8_t beatDivision = 1u;
        uint8_t previewRateIndex = 0u;
        uint8_t preRollTenths = 10u;
        double bpm = 120.0;
        bool monoChoke = true;
        {
            std::lock_guard<std::mutex> lock(p.statusMutex);
            source = p.chopSourceAsset;
            layout = p.chopLayout;
            mode = p.chopMode;
            analysis = p.chopSourceAnalysis;
            sourceStatus = p.chopSourceStatus;
            equalCount = p.chopEqualCount;
            transientMaximum = p.chopTransientMaximum;
            beatDivision = p.chopBeatDivisionIndex;
            previewRateIndex = p.chopPreviewRateIndex.load(
                std::memory_order_acquire);
            preRollTenths = p.chopPreRollTenthsMilliseconds;
            bpm = p.chopSourceBpm;
            monoChoke = p.chopMonoChoke;
        }
        if (source.get() != chopViewportSource) {
            chopViewportSource = source.get();
            chopZoom = 1.0;
            chopVisibleStart = 0u;
            chopPanRemainder = 0.0;
        }
        panel(rect(14.0, 94.0, kGuiWidth - 28.0, 646.0),
            "CHOP SOURCE  //  ONE SHARED FILE, UP TO SIXTEEN PAD REGIONS");
        text(upper(sourceStatus), rect(28.0, 119.0, 602.0, 20.0),
            source ? style.value : style.dim);
        button(rect(650.0, 112.0, 126.0, 26.0), "LOAD SOURCE",
            [this] { chooseChopSource(); });
        button(rect(784.0, 112.0, 82.0, 26.0), "CLEAR",
            [this] { clearChopSource(p); });
        const bool playing = p.chopPreviewActive.load(
            std::memory_order_relaxed);
        button(rect(874.0, 112.0, 102.0, 26.0), "PLAY ALL",
            [this] { requestChopPreview(p, 0.0, 1.0); }, playing);
        button(rect(984.0, 112.0, 108.0, 26.0), "STOP",
            [this] {
                p.chopPreviewStop.store(true, std::memory_order_release);
                requestProcess(p);
            });

        const CRect wave = chopWaveformRect();
        double viewStart = 0.0;
        double viewEnd = 1.0;
        if (source) {
            const uint32_t total = source->frameCount();
            const uint32_t visible = chopVisibleFrames(total);
            const uint32_t start = chopClampedStart(total);
            viewStart = static_cast<double>(start)
                / static_cast<double>(total);
            viewEnd = static_cast<double>(start + visible)
                / static_cast<double>(total);
        }
        drawWaveformShape(wave, source.get(), "DROP AUDIO ON THE CHOP PAGE",
            viewStart, viewEnd);
        if (source) {
            const uint32_t total = source->frameCount();
            for (std::size_t marker = 0u; marker <= layout.sliceCount;
                 ++marker) {
                const double x = chopXForPosition(
                    layout.boundaries[marker], total);
                if (x < wave.left || x > wave.right) continue;
                line(CPoint(x, wave.top), CPoint(x, wave.bottom),
                    marker == 0u || marker == layout.sliceCount
                        ? style.grid : style.accent,
                    marker == 0u || marker == layout.sliceCount ? 1.0 : 1.5);
            }
            const float playhead = p.chopPreviewPosition.load(
                std::memory_order_relaxed);
            if (playhead >= 0.0f) {
                const double x = chopXForPosition(playhead, total);
                if (x >= wave.left && x <= wave.right)
                    line(CPoint(x, wave.top), CPoint(x, wave.bottom),
                        color(0xd7b36b), 2.0);
            }
            text(format("ZOOM %.2f X", chopZoom),
                rect(wave.right - 132.0, wave.top + 6.0, 120.0, 18.0),
                style.dim, kRightText, smallFont);
            text("DOUBLE-CLICK TO ADD MARKER  //  DRAG A MARKER TO MOVE",
                rect(wave.left + 10.0, wave.bottom - 24.0, 420.0, 18.0),
                style.dim, kLeftText, smallFont);
            drag(wave, [this, total](CPoint point) {
                std::lock_guard<std::mutex> lock(p.statusMutex);
                if (p.chopLayout.sliceCount <= 1u) return;
                const double position = chopPositionForX(point.x, total);
                if (chopDragMarker < 1) {
                    double nearestPixels = 9.0;
                    for (std::size_t marker = 1u;
                         marker < p.chopLayout.sliceCount; ++marker) {
                        const double distancePixels = std::abs(
                            chopXForPosition(
                                p.chopLayout.boundaries[marker], total)
                            - point.x);
                        if (distancePixels <= 8.0
                            && distancePixels < nearestPixels) {
                            nearestPixels = distancePixels;
                            chopDragMarker = static_cast<int>(marker);
                        }
                    }
                }
                if (chopDragMarker > 0)
                    (void)s3g::sample::moveChopMarker(p.chopLayout,
                        static_cast<std::size_t>(chopDragMarker), position);
            }, [this] { chopDragMarker = -1; }, [this] {
                chopDragMarker = -1;
                markStateDirty(p);
            });
            const CRect navigator = chopNavigatorRect();
            fill(navigator, color(0x161616));
            stroke(navigator, color(0x444444));
            const CRect handle = chopViewportHandle(total);
            fill(handle, color(chopZoom > 1.0 ? 0x66616a : 0x3d3d3d));
            stroke(handle, color(chopZoom > 1.0 ? 0x8b8490 : 0x515151));
            drag(navigator, [this, total](CPoint point) {
                const CRect current = chopViewportHandle(total);
                if (chopViewportGrabOffset < 0.0)
                    chopViewportGrabOffset = foundation::contains(
                            current, point)
                        ? point.x - current.left
                        : current.getWidth() * 0.5;
                setChopViewportOrigin(
                    point.x - chopViewportGrabOffset, total);
            }, [this] { chopViewportGrabOffset = -1.0; }, [this] {
                chopViewportGrabOffset = -1.0;
            });
        }

        constexpr std::array<const char*, 4u> modeNames {{
            "LIVE MARK", "TRANSIENT", "EQUAL", "BEAT GRID",
        }};
        for (std::size_t index = 0u; index < modeNames.size(); ++index)
            button(rect(28.0 + index * 138.0, 338.0, 130.0, 30.0),
                modeNames[index], [this, index] {
                    setChopMode(static_cast<SampleKitChopMode>(index));
                }, static_cast<std::size_t>(mode) == index);
        text(mode == SampleKitChopMode::LiveMark
                ? "PLAY ALL, THEN TAP A CHOP PAD TO PLACE EACH MARK"
                : mode == SampleKitChopMode::Transient
                    ? "DETECTED ATTACKS, LIMITED TO THE SELECTED COUNT"
                    : mode == SampleKitChopMode::Equal
                        ? "EVEN REGIONS ACROSS THE COMPLETE SOURCE"
                        : "SOURCE BPM AND BEATS PER REGION",
            rect(590.0, 343.0, 502.0, 20.0), style.dim, kRightText);

        if (mode == SampleKitChopMode::Equal
            || mode == SampleKitChopMode::Transient) {
            const uint8_t count = mode == SampleKitChopMode::Equal
                ? equalCount : transientMaximum;
            text(mode == SampleKitChopMode::Equal ? "REGIONS" : "MAX REGIONS",
                rect(700.0, 386.0, 108.0, 20.0), style.label);
            button(rect(814.0, 381.0, 38.0, 28.0), "-", [this, count, mode] {
                setChopCount(static_cast<uint8_t>(std::max<int>(1,
                    static_cast<int>(count) - 1)),
                    mode == SampleKitChopMode::Transient);
            });
            text(format("%u", count), rect(858.0, 383.0, 54.0, 24.0),
                style.value, kCenterText);
            button(rect(918.0, 381.0, 38.0, 28.0), "+", [this, count, mode] {
                setChopCount(static_cast<uint8_t>(std::min<int>(16,
                    static_cast<int>(count) + 1)),
                    mode == SampleKitChopMode::Transient);
            });
        } else if (mode == SampleKitChopMode::BeatGrid) {
            text("SOURCE BPM", rect(700.0, 386.0, 100.0, 20.0), style.label);
            const CRect bpmBounds = rect(806.0, 382.0, 94.0, 24.0);
            fill(bpmBounds, style.cell);
            text(format("%.2f", bpm), bpmBounds, style.value, kCenterText);
            number(bpmBounds, bpm, [this](double value) {
                setChopBpm(value);
            });
            text("GRID", rect(918.0, 386.0, 44.0, 20.0), style.label);
            constexpr std::array<const char*, 4u> labels {{
                "1/2 BEAT", "1 BEAT", "2 BEATS", "4 BEATS",
            }};
            std::vector<std::string> items;
            for (const char* label : labels) items.emplace_back(label);
            menu(rect(966.0, 386.0, 126.0, 15.0), labels[beatDivision],
                std::move(items), beatDivision, [this](int choice) {
                    {
                        std::lock_guard<std::mutex> lock(p.statusMutex);
                        p.chopBeatDivisionIndex = static_cast<uint8_t>(
                            std::clamp(choice, 0, 3));
                        regenerateChopLayoutLocked(p);
                    }
                    selectedChop = 0u;
                    markStateDirty(p);
                }, true, 1, kCenterText);
        } else {
            button(rect(700.0, 381.0, 168.0, 28.0), "RESET MARKS",
                [this] { setChopMode(SampleKitChopMode::LiveMark); });
            text("PREVIEW RATE", rect(880.0, 386.0, 80.0, 20.0),
                style.label);
            constexpr std::array<const char*, 4u> rateLabels {{
                "1 X", "3/4 X", "1/2 X", "1/4 X",
            }};
            std::vector<std::string> rateItems;
            for (const char* label : rateLabels)
                rateItems.emplace_back(label);
            menu(rect(966.0, 386.0, 126.0, 15.0),
                rateLabels[previewRateIndex], std::move(rateItems),
                previewRateIndex, [this](int choice) {
                    p.chopPreviewRateIndex.store(static_cast<uint8_t>(
                            std::clamp(choice, 0, 3)),
                        std::memory_order_release);
                    markStateDirty(p);
                    requestProcess(p);
                }, true, 1, kCenterText);
        }
        if (mode == SampleKitChopMode::LiveMark
            || mode == SampleKitChopMode::Transient)
            paintChopPreRoll(414.0, preRollTenths);

        const double durationSeconds = source && source->sampleRate > 0.0
            ? static_cast<double>(source->frameCount()) / source->sampleRate
            : 0.0;
        for (std::size_t chop = 0u;
             chop < s3g::sample::kSampleKitMaximumChops; ++chop) {
            const std::size_t column = chop % 4u;
            const std::size_t row = 3u - chop / 4u;
            const CRect cell = rect(28.0 + column * 154.0,
                430.0 + row * 66.0, 146.0, 56.0);
            const bool present = chop < layout.sliceCount;
            const bool pressed = pressedChop == static_cast<int>(chop);
            fill(cell, color(pressed ? 0x49382f
                    : chop == selectedChop && present
                        ? 0x303635 : 0x181a1a));
            stroke(cell, color(pressed ? 0xda8b68
                    : chop == selectedChop && present
                        ? 0x89afa5 : 0x4b4d4c),
                pressed || (chop == selectedChop && present) ? 2.0 : 1.0);
            text(format("PAD %02zu", chop + 1u),
                rect(cell.left + 8.0, cell.top + 7.0, 66.0, 18.0),
                present ? style.text : style.dim);
            const double milliseconds = present
                ? (layout.boundaries[chop + 1u]
                    - layout.boundaries[chop]) * durationSeconds * 1000.0
                : 0.0;
            text(present ? format("%.0f MS", milliseconds) : "--",
                rect(cell.left + 70.0, cell.top + 7.0, 68.0, 18.0),
                present ? style.value : style.dim, kRightText);
            text(present ? format("%05.1f - %05.1f",
                    layout.boundaries[chop] * 100.0,
                    layout.boundaries[chop + 1u] * 100.0) : "EMPTY",
                rect(cell.left + 8.0, cell.top + 30.0,
                    cell.getWidth() - 16.0, 18.0),
                present ? style.label : style.dim, kCenterText, smallFont);
            hit(cell, [this, chop] {
                pressedChop = static_cast<int>(chop);
                SampleKitChopLayout current;
                SampleKitChopMode currentMode;
                {
                    std::lock_guard<std::mutex> lock(p.statusMutex);
                    current = p.chopLayout;
                    currentMode = p.chopMode;
                }
                if (currentMode == SampleKitChopMode::LiveMark
                    && p.chopPreviewActive.load(std::memory_order_relaxed)) {
                    double position = p.chopPreviewPosition.load(
                        std::memory_order_relaxed);
                    bool added = false;
                    {
                        std::lock_guard<std::mutex> lock(p.statusMutex);
                        const auto& liveSource = p.chopSourceAsset;
                        const double duration = liveSource
                                && liveSource->sampleRate > 0.0
                            ? static_cast<double>(liveSource->frameCount())
                                / liveSource->sampleRate
                            : 0.0;
                        position = s3g::sample::preRolledChopPosition(
                            position, duration, static_cast<double>(
                                p.chopPreRollTenthsMilliseconds) * 0.1);
                        added = s3g::sample::addChopMarker(
                            p.chopLayout, position);
                        current = p.chopLayout;
                    }
                    if (added) {
                        selectedChop = current.sliceCount - 1u;
                        markStateDirty(p);
                    }
                } else if (chop < current.sliceCount) {
                    selectedChop = chop;
                    requestChopPreview(p, current.boundaries[chop],
                        current.boundaries[chop + 1u]);
                }
            }, present || mode == SampleKitChopMode::LiveMark);
        }

        text(analysis.tempoValid
                ? format("ANALYZED %.2f BPM  //  %u TRANSIENT REGIONS",
                    analysis.analyzedBpm,
                    analysis.transientRegions.count)
                : format("%u TRANSIENT REGIONS",
                    analysis.transientRegions.count),
            rect(660.0, 446.0, 432.0, 20.0), style.dim, kCenterText);
        button(rect(700.0, 492.0, 352.0, 34.0),
            monoChoke ? "MONO CHOKE  ON" : "MONO CHOKE  OFF",
            [this, monoChoke] {
                {
                    std::lock_guard<std::mutex> lock(p.statusMutex);
                    p.chopMonoChoke = !monoChoke;
                }
                markStateDirty(p);
            }, monoChoke);
        text("ON MAPS CHOKE GROUP 1 FOR CLASSIC PHRASE CUTTING",
            rect(670.0, 532.0, 412.0, 20.0), style.dim, kCenterText,
            smallFont);
        button(rect(700.0, 576.0, 352.0, 40.0), "MAP TO PADS",
            [this] {
                fileMessage = mapChopsToPads(p)
                    ? "CHOPS MAPPED TO PAD VARIATION 1"
                    : "LOAD A CHOP SOURCE FIRST";
            }, false, static_cast<bool>(source));
        text("REPLACES VARIATION 1 AND CLEARS 2-8 ON MAPPED PADS",
            rect(670.0, 622.0, 412.0, 20.0), style.dim, kCenterText,
            smallFont);
        text("PAD MIXER, TUNE, ENVELOPE, FILTER, AND OUTPUTS ARE PRESERVED",
            rect(650.0, 646.0, 442.0, 20.0), style.dim, kCenterText,
            smallFont);
    }

    void paintMixer()
    {
        panel(rect(14.0, 94.0, kGuiWidth - 28.0, 646.0),
            "16-CHANNEL KIT MIXER  //  EACH STRIP FEEDS A STEREO PAIR");
        constexpr double stripWidth = 64.0;
        constexpr double stripGap = 3.0;
        for (std::size_t pad = 0u;
             pad < s3g::sample::kSampleKitPadCount; ++pad) {
            const double x = 24.0 + pad * (stripWidth + stripGap);
            const CRect strip = rect(x, 126.0, stripWidth, 594.0);
            fill(strip, color(pad == selectedPad ? 0x242b29 : 0x151616));
            stroke(strip, color(pad == selectedPad ? 0x789b92 : 0x3d3e3e));
            hit(strip, [this, pad] { selectedPad = pad; });
            text(format("%02zu", pad + 1u), rect(x, 134.0,
                stripWidth, 18.0), style.text, kCenterText);
            const float meter = std::clamp(p.padPeaks[pad].load(
                std::memory_order_relaxed), 0.0f, 1.0f);
            fill(rect(x + 9.0, 164.0, 8.0, 326.0), color(0x090a0a));
            fill(rect(x + 9.0, 164.0 + 326.0 * (1.0 - meter),
                8.0, 326.0 * meter), color(0x739f93));
            verticalFader(padParamId(pad, kPadGain),
                rect(x + 24.0, 164.0, 28.0, 326.0));
            const bool muted = paramValue(p,
                padParamId(pad, kPadMute)) >= 0.5;
            const bool soloed = paramValue(p,
                padParamId(pad, kPadSolo)) >= 0.5;
            button(rect(x + 5.0, 506.0, 25.0, 24.0), "M",
                [this, pad, muted] {
                    queueGuiParamGesture(p, padParamId(pad, kPadMute),
                        muted ? 0.0 : 1.0);
                }, muted);
            button(rect(x + 34.0, 506.0, 25.0, 24.0), "S",
                [this, pad, soloed] {
                    queueGuiParamGesture(p, padParamId(pad, kPadSolo),
                        soloed ? 0.0 : 1.0);
                }, soloed);
            miniHorizontal(padParamId(pad, kPadPan),
                rect(x + 6.0, 548.0, 52.0, 18.0), "PAN");
            const int pair = static_cast<int>(std::lround(paramValue(
                p, padParamId(pad, kPadOutputPair))));
            button(rect(x + 5.0, 588.0, 54.0, 25.0),
                format("P%02d", pair), [this, pad] {
                    selectedPad = pad;
                    page = 0u;
                }, false);
        }
    }

    void drawWaveformShape(CRect bounds, const SampleAsset* current,
        const char* emptyLabel, double viewStart = 0.0,
        double viewEnd = 1.0)
    {
        fill(bounds, style.strip);
        stroke(bounds, style.grid);
        line(CPoint(bounds.left, bounds.getCenter().y),
            CPoint(bounds.right, bounds.getCenter().y), color(0x343737));
        if (!current || !current->valid()) {
            text(emptyLabel, bounds, style.dim, kCenterText);
            return;
        }
        const std::size_t pixels = static_cast<std::size_t>(
            std::max(1.0, std::floor(bounds.getWidth())));
        const std::size_t total = current->frameCount();
        const std::size_t first = std::min(total - 1u,
            static_cast<std::size_t>(std::floor(std::clamp(
                viewStart, 0.0, 1.0) * static_cast<double>(total))));
        const std::size_t last = std::clamp<std::size_t>(
            static_cast<std::size_t>(std::ceil(std::clamp(
                viewEnd, viewStart, 1.0) * static_cast<double>(total))),
            first + 1u, total);
        const std::size_t visible = last - first;
        const double center = bounds.getCenter().y;
        const double amplitude = std::max(2.0,
            bounds.getHeight() * 0.42);
        if (visible <= pixels) {
            const double denominator = static_cast<double>(
                std::max<std::size_t>(1u, visible - 1u));
            for (uint8_t channel = 0u;
                 channel < current->channelCount; ++channel) {
                auto sampleTrace = owned(context->createGraphicsPath());
                if (!sampleTrace) return;
                const auto& samples = current->channels[channel];
                for (std::size_t frame = first; frame < last; ++frame) {
                    const double x = bounds.left + bounds.getWidth()
                        * static_cast<double>(frame - first) / denominator;
                    const double y = center - samples[frame] * amplitude;
                    if (frame == first)
                        sampleTrace->beginSubpath(CPoint(x, y));
                    else sampleTrace->addLine(CPoint(x, y));
                }
                context->setFrameColor(color(0x7b7b7b));
                context->setLineWidth(1.0);
                context->drawGraphicsPath(sampleTrace,
                    CDrawContext::kPathStroked);
            }
            return;
        }
        auto trace = owned(context->createGraphicsPath());
        if (!trace) return;
        for (std::size_t pixel = 0u; pixel < pixels; ++pixel) {
            float minimum = 1.0f;
            float maximum = -1.0f;
            for (uint8_t channel = 0u;
                 channel < current->channelCount; ++channel) {
                const auto& samples = current->channels[channel];
                const std::size_t begin = first
                    + pixel * visible / pixels;
                const std::size_t end = std::max(begin + 1u,
                    std::min(last, first
                        + (pixel + 1u) * visible / pixels));
                const std::size_t stride = std::max<std::size_t>(1u,
                    (end - begin) / 32u);
                for (std::size_t frame = begin; frame < end;
                     frame += stride) {
                    minimum = std::min(minimum, samples[frame]);
                    maximum = std::max(maximum, samples[frame]);
                }
                minimum = std::min(minimum, samples[end - 1u]);
                maximum = std::max(maximum, samples[end - 1u]);
            }
            const double x = bounds.left + static_cast<double>(pixel);
            trace->beginSubpath(CPoint(x, center - maximum * amplitude));
            trace->addLine(CPoint(x, center - minimum * amplitude));
        }
        context->setFrameColor(color(0x7b7b7b));
        context->setLineWidth(1.0);
        context->drawGraphicsPath(trace, CDrawContext::kPathStroked);
    }

    void drawWaveform(CRect bounds, const SampleAsset* current)
    {
        drawWaveformShape(bounds, current, "DROP AUDIO ON A PAD");
        if (!current || !current->valid()) return;
        const double start = paramValue(p,
            padParamId(selectedPad, kPadStart));
        const double end = paramValue(p,
            padParamId(selectedPad, kPadEnd));
        for (double position : {start, end})
            line(CPoint(bounds.left + position * bounds.getWidth(), bounds.top),
                CPoint(bounds.left + position * bounds.getWidth(), bounds.bottom),
                style.accent, 1.5);
    }

    void paintVariationButtons(double y)
    {
        const uint8_t sounding = p.lastVariations[selectedPad].load(
            std::memory_order_relaxed);
        constexpr double width = 50.0;
        constexpr double gap = 5.0;
        for (std::size_t variation = 0u;
             variation < s3g::sample::kSampleKitVariationCount;
             ++variation) {
            const bool loaded = static_cast<bool>(asset(
                selectedPad, variation));
            const std::string label = sounding == variation
                ? format(">%zu", variation + 1u)
                : loaded ? format("%zu", variation + 1u) : "--";
            button(rect(kPanelLeft + 16.0
                    + variation * (width + gap), y, width, 26.0),
                label, [this, variation] {
                    selectedVariation = variation;
                }, variation == selectedVariation);
        }
    }

    void drawEnvelope(CRect bounds)
    {
        fill(bounds, color(0x0c0d0d));
        stroke(bounds, style.grid);
        const double a = paramValue(p, padParamId(selectedPad, kPadAttack));
        const double d = paramValue(p, padParamId(selectedPad, kPadDecay));
        const double s = paramValue(p, padParamId(selectedPad, kPadSustain));
        const double r = paramValue(p, padParamId(selectedPad, kPadRelease));
        const double total = std::max(0.001, a + d + r);
        const double usable = bounds.getWidth() - 20.0;
        const double ax = bounds.left + 10.0 + usable * a / total * 0.55;
        const double dx = ax + usable * d / total * 0.55;
        const double rx = bounds.right - 10.0;
        const double sustainY = bounds.bottom - 10.0
            - s * (bounds.getHeight() - 20.0);
        auto path = owned(context->createGraphicsPath());
        if (!path) return;
        path->beginSubpath(CPoint(bounds.left + 10.0, bounds.bottom - 10.0));
        path->addLine(CPoint(ax, bounds.top + 10.0));
        path->addLine(CPoint(dx, sustainY));
        path->addLine(CPoint(bounds.left + usable * 0.72, sustainY));
        path->addLine(CPoint(rx, bounds.bottom - 10.0));
        context->setFrameColor(style.accent);
        context->setLineWidth(2.0);
        context->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }

    void paintOutputPairs(CRect bounds)
    {
        const int active = static_cast<int>(std::lround(paramValue(
            p, kActivePairsParamId)));
        for (int pair = 0; pair < 16; ++pair) {
            const int row = pair / 8;
            const int column = pair % 8;
            const CRect cell = rect(bounds.left + column * 52.0,
                bounds.top + row * 34.0, 46.0, 26.0);
            fill(cell, color(pair < active ? 0x344b45 : 0x171818));
            stroke(cell, color(pair < active ? 0x75998f : 0x393a3a));
            text(format("%02d", pair + 1), cell,
                pair < active ? style.value : style.dim, kCenterText);
        }
    }

    static bool sameRect(const CRect& first, const CRect& second) noexcept
    {
        return first.left == second.left && first.top == second.top
            && first.right == second.right && first.bottom == second.bottom;
    }

    void button(CRect bounds, const std::string& label,
        std::function<void()> action, bool active = false,
        bool enabled = true)
    {
        const bool down = pressedButton
            && sameRect(bounds, pressedButtonBounds);
        View::button(bounds, label,
            [this, bounds, action = std::move(action)] {
                pressedButton = true;
                pressedButtonBounds = bounds;
                if (action) action();
            }, active || down, enabled);
    }

    double normalized(clap_id id, double value) const
    {
        std::size_t index = 0u;
        const ParamDef* definition = nullptr;
        if (!parameterLocation(id, index, definition)) return 0.0;
        std::size_t pad = 0u;
        PadParamOffset offset = kPadGain;
        (void)parameterLocation(id, index, definition, &pad, &offset);
        if (id >= kPadParamBase && offset == kPadFilterCutoff) {
            return std::log(value / definition->minimum)
                / std::log(definition->maximum / definition->minimum);
        }
        return (value - definition->minimum)
            / (definition->maximum - definition->minimum);
    }

    double fromNormalized(clap_id id, double unit) const
    {
        std::size_t index = 0u;
        std::size_t pad = 0u;
        PadParamOffset offset = kPadGain;
        const ParamDef* definition = nullptr;
        if (!parameterLocation(id, index, definition, &pad, &offset))
            return 0.0;
        unit = std::clamp(unit, 0.0, 1.0);
        if (id >= kPadParamBase && offset == kPadFilterCutoff)
            return definition->minimum * std::pow(
                definition->maximum / definition->minimum, unit);
        const double value = definition->minimum
            + unit * (definition->maximum - definition->minimum);
        return definition->stepped ? std::round(value) : value;
    }

    void sliderRow(clap_id id, const char* label, double y)
    {
        const double x = kPanelLeft + 132.0;
        const double width = 218.0;
        const CRect hitBounds = rect(kPanelLeft + 14.0, y - 8.0,
            kPanelWidth - 28.0, 30.0);
        const CRect track = rect(x, y + 3.0, width, 8.0);
        char valueText[64] {};
        (void)paramsValueToText(&p.plugin, id, paramValue(p, id),
            valueText, sizeof(valueText));
        text(label, rect(kPanelLeft + 18.0, y - 1.0, 108.0, 18.0),
            style.label);
        foundation::drawHorizontalSlider(*context, track,
            normalized(id, paramValue(p, id)), y, 14.0, style);
        text(valueText, rect(x + width + 8.0, y - 2.0, 88.0, 20.0),
            style.value, kRightText);
        drag(hitBounds, [this, id, x, width](CPoint point) {
            queueGuiParamValue(p, id,
                fromNormalized(id, (point.x - x) / width));
        }, [this, id] { queueGuiParamBegin(p, id); },
           [this, id] { queueGuiParamEnd(p, id); },
           [this, id] {
               std::size_t index = 0u;
               const ParamDef* definition = nullptr;
               if (parameterLocation(id, index, definition))
                   queueGuiParamGesture(p, id, definition->defaultValue);
           });
    }

    void menuRow(clap_id id, const char* label, double y)
    {
        constexpr auto& metrics = s3g::gui_layout::kStandardMetrics;
        std::size_t index = 0u;
        const ParamDef* definition = nullptr;
        if (!parameterLocation(id, index, definition)) return;
        char valueText[64] {};
        (void)paramsValueToText(&p.plugin, id, paramValue(p, id),
            valueText, sizeof(valueText));
        const double controlX = kPanelLeft + metrics.controlInset;
        const double menuWidth = kPanelWidth - metrics.controlInset
            - metrics.panelRightInset;
        const CRect menuBounds = rect(controlX, y - 1.0,
            menuWidth, 15.0);
        text(label, rect(kPanelLeft + metrics.labelInset,
                menuBounds.getCenter().y - 7.5,
                controlX - kPanelLeft - metrics.labelInset - 5.0, 15.0),
            style.label);
        std::vector<std::string> items;
        const int first = static_cast<int>(std::lround(definition->minimum));
        const int last = static_cast<int>(std::lround(definition->maximum));
        items.reserve(static_cast<std::size_t>(last - first + 1));
        for (int choice = first; choice <= last; ++choice) {
            char textBuffer[64] {};
            (void)paramsValueToText(&p.plugin, id, choice,
                textBuffer, sizeof(textBuffer));
            items.emplace_back(upper(textBuffer));
        }
        const int columns = items.size() > 16u ? 2 : 1;
        menu(menuBounds, upper(valueText), std::move(items),
            static_cast<int>(std::lround(paramValue(p, id))) - first,
            [this, id, first](int choice) {
                queueGuiParamGesture(p, id,
                    static_cast<double>(first + choice));
            }, true, columns, kCenterText);
    }

    void togglePair(double y)
    {
        const clap_id muteId = padParamId(selectedPad, kPadMute);
        const clap_id soloId = padParamId(selectedPad, kPadSolo);
        const bool muted = paramValue(p, muteId) >= 0.5;
        const bool soloed = paramValue(p, soloId) >= 0.5;
        button(rect(kPanelLeft + 132.0, y - 4.0, 150.0, 28.0), "MUTE",
            [this, muteId, muted] {
                queueGuiParamGesture(p, muteId, muted ? 0.0 : 1.0);
            }, muted);
        button(rect(kPanelLeft + 294.0, y - 4.0, 158.0, 28.0), "SOLO",
            [this, soloId, soloed] {
                queueGuiParamGesture(p, soloId, soloed ? 0.0 : 1.0);
            }, soloed);
    }

    void verticalFader(clap_id id, CRect bounds)
    {
        fill(rect(bounds.getCenter().x - 2.0, bounds.top, 4.0,
            bounds.getHeight()), color(0x3d4140));
        const double unit = normalized(id, paramValue(p, id));
        const double y = bounds.bottom - unit * bounds.getHeight();
        fill(rect(bounds.left, y - 5.0, bounds.getWidth(), 10.0),
            color(0x8da99f));
        drag(bounds, [this, id, bounds](CPoint point) {
            queueGuiParamValue(p, id,
                fromNormalized(id,
                    (bounds.bottom - point.y) / bounds.getHeight()));
        }, [this, id] { queueGuiParamBegin(p, id); },
           [this, id] { queueGuiParamEnd(p, id); });
    }

    void miniHorizontal(clap_id id, CRect bounds, const char* label)
    {
        text(label, rect(bounds.left, bounds.top - 16.0,
            bounds.getWidth(), 14.0), style.label, kCenterText, smallFont);
        foundation::drawHorizontalSlider(*context, bounds,
            normalized(id, paramValue(p, id)), bounds.top + 2.0,
            bounds.getHeight() - 4.0, style);
        drag(bounds, [this, id, bounds](CPoint point) {
            queueGuiParamValue(p, id, fromNormalized(id,
                (point.x - bounds.left) / bounds.getWidth()));
        }, [this, id] { queueGuiParamBegin(p, id); },
           [this, id] { queueGuiParamEnd(p, id); });
    }

    void chooseSample()
    {
#if defined(S3G_SAMPLE_FILE_WORKER)
        foundation::FileDialogOptions options;
        options.title = "Load Sample Kit pad";
        const std::string selected = foundation::runFileDialog(
            getFrame(), options);
        if (!selected.empty()) queueSampleLoad(
            p, selectedPad, selectedVariation, selected);
#endif
    }

    void chooseChopSource()
    {
#if defined(S3G_SAMPLE_FILE_WORKER)
        foundation::FileDialogOptions options;
        options.title = "Load Sample Kit chop source";
        const std::string selected = foundation::runFileDialog(
            getFrame(), options);
        if (!selected.empty()) queueChopSourceLoad(p, selected);
#endif
    }

    void chooseKit(bool save)
    {
        foundation::FileDialogOptions options;
        options.save = save;
        options.title = save ? "Save Sample Kit" : "Load Sample Kit";
        options.extension = "s3gkit";
        options.extensionDescription = "s3g Sample Kit";
        options.defaultSaveName = "Sample Kit.s3gkit";
        options.initialDirectory = foundation::presetDirectory(
            "Sample Kit");
        const std::string selected = foundation::runFileDialog(
            getFrame(), options);
        if (selected.empty()) return;
        fileMessage = save
            ? (s3g::clap_gui::portable::saveStateFile(
                &p.plugin, stateExtension, selected.c_str())
                ? "KIT SAVED" : "KIT SAVE FAILED")
            : (s3g::clap_gui::portable::loadStateFile(
                &p.plugin, stateExtension, selected.c_str())
                ? "KIT LOADED" : "KIT LOAD FAILED");
    }

    Plugin& p;
    std::size_t selectedPad = 0u;
    std::size_t selectedVariation = 0u;
    std::size_t selectedChop = 0u;
    bool pressedButton = false;
    CRect pressedButtonBounds {};
    int pressedPad = -1;
    int pressedChop = -1;
    int chopDragMarker = -1;
    const SampleAsset* chopViewportSource = nullptr;
    double chopZoom = 1.0;
    uint32_t chopVisibleStart = 0u;
    double chopPanRemainder = 0.0;
    double chopViewportGrabOffset = -1.0;
    std::size_t page = 0u;
    std::string fileMessage;
};

} // namespace sample_kit_canvas

s3g::portable_gui::foundation::ContentView* makePortableCanvas(Plugin& p)
{
    return new sample_kit_canvas::Editor(p);
}
