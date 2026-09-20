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
        if (page == 4u) paintMixer();
        else {
            paintPads();
            paintSelectedPanel();
        }
    }

    bool dropped(const std::vector<std::string>& paths, CPoint point) override
    {
        if (paths.empty()) return false;
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

private:
    static constexpr double kPadLeft = 22.0;
    static constexpr double kPadTop = 104.0;
    static constexpr double kPadWidth = 142.0;
    static constexpr double kPadHeight = 132.0;
    static constexpr double kPadGap = 8.0;
    static constexpr double kPanelLeft = 628.0;
    static constexpr double kPanelWidth = 470.0;

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
        constexpr std::array<const char*, 6u> names {{
            "KIT", "SOUND", "AMP / FILTER", "NATURAL", "MIXER", "OUTPUT",
        }};
        constexpr double width = 128.0;
        for (std::size_t index = 0u; index < names.size(); ++index)
            button(rect(22.0 + index * (width + 6.0), 48.0,
                    width, 34.0), names[index],
                [this, index] { page = index; }, page == index);
        text("16 STEREO PADS  //  MIDI 36-51 AT DEFAULT BASE",
            rect(834.0, 54.0, 264.0, 22.0), style.label, kRightText);
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
            fill(bounds, color(selected ? 0x303635 : 0x181a1a));
            stroke(bounds, color(selected ? 0x89afa5 : 0x4b4d4c),
                selected ? 2.0 : 1.0);
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
                : page == 1u ? "SELECTED PAD / SOUND"
                : page == 2u ? "SELECTED PAD / AMP + FILTER"
                : page == 3u ? "SELECTED PAD / NATURAL"
                : "MASTER / OUTPUT");
        text(format("PAD %02zu  //  MIDI %d", selectedPad + 1u,
                static_cast<int>(std::lround(paramValue(
                    p, kBaseNoteParamId)))
                    + static_cast<int>(selectedPad)),
            rect(kPanelLeft + 16.0, kPadTop + 23.0,
                kPanelWidth - 32.0, 22.0), style.text);
        if (page == 0u) paintKitPage();
        else if (page == 1u) paintSoundPage();
        else if (page == 2u) paintAmpPage();
        else if (page == 3u) paintNaturalPage();
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
        text(format("%zu / %zu VARIATIONS LOADED", variants,
                s3g::sample::kSampleKitVariationCount),
            rect(kPanelLeft + 18.0, kPadTop + 60.0,
                kPanelWidth - 36.0, 20.0), style.value, kCenterText);
        double y = kPadTop + 104.0;
        menuRow(padParamId(selectedPad, kPadNaturalEnabled),
            "NATURAL", y);
        menuRow(padParamId(selectedPad, kPadVariationMode),
            "SELECTION", y += 44.0);
        sliderRow(padParamId(selectedPad, kPadNaturalGain),
            "GAIN", y += 44.0);
        sliderRow(padParamId(selectedPad, kPadNaturalPitch),
            "PITCH", y += 44.0);
        sliderRow(padParamId(selectedPad, kPadNaturalStart),
            "START", y += 44.0);
        sliderRow(padParamId(selectedPad, kPadNaturalTiming),
            "TIMING", y += 44.0);
        text("BYPASS USES THE FIRST LOADED VARIATION WITHOUT HIT JITTER",
            rect(kPanelLeft + 18.0, y + 48.0,
                kPanelWidth - 36.0, 36.0), style.dim, kCenterText);
        text("SHUFFLE AND NO REPEAT AVOID IMMEDIATE DUPLICATES",
            rect(kPanelLeft + 18.0, y + 88.0,
                kPanelWidth - 36.0, 24.0), style.dim, kCenterText);
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
        sliderRow(kMasterGainParamId, "MASTER", y += 44.0);
        sliderRow(kGlobalTuneParamId, "GLOBAL TUNE", y += 44.0);
        sliderRow(kDriveParamId, "DRIVE", y += 44.0);
        menuRow(kBitDepthParamId, "BIT DEPTH", y += 44.0);
        menuRow(kRateReductionParamId, "RATE HOLD", y += 44.0);
        menuRow(kNaturalGlobalParamId, "GLOBAL NAT", y += 44.0);
        const uint32_t seed = static_cast<uint32_t>(std::llround(
            paramValue(p, kRandomSeedParamId)));
        text("SEED", rect(kPanelLeft + 18.0, y + 43.0,
            108.0, 18.0), style.label);
        button(rect(kPanelLeft + 132.0, y + 38.0, 320.0, 24.0),
            format("%u  /  NEXT", seed), [this, seed] {
                queueGuiParamGesture(p, kRandomSeedParamId,
                    seed >= 2147483647u ? 1.0
                        : static_cast<double>(seed + 1u));
            });
        paintOutputPairs(rect(kPanelLeft + 18.0, y + 80.0,
            kPanelWidth - 36.0, 76.0));
        text("ROUTE PAIRS THROUGH DOWNSTREAM EFFECT PLUGINS",
            rect(kPanelLeft + 18.0, y + 160.0,
                kPanelWidth - 36.0, 20.0), style.dim, kCenterText);
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

    void drawWaveform(CRect bounds, const SampleAsset* current)
    {
        fill(bounds, style.strip);
        stroke(bounds, style.grid);
        line(CPoint(bounds.left, bounds.getCenter().y),
            CPoint(bounds.right, bounds.getCenter().y), color(0x343737));
        if (!current || !current->valid()) {
            text("DROP AUDIO ON A PAD", bounds, style.dim, kCenterText);
            return;
        }
        const std::size_t pixels = static_cast<std::size_t>(
            std::max(1.0, std::floor(bounds.getWidth())));
        const double center = bounds.getCenter().y;
        const double amplitude = std::max(2.0,
            bounds.getHeight() * 0.42);
        auto trace = owned(context->createGraphicsPath());
        if (!trace) return;
        for (std::size_t pixel = 0u; pixel < pixels; ++pixel) {
            float minimum = 1.0f;
            float maximum = -1.0f;
            for (uint8_t channel = 0u;
                 channel < current->channelCount; ++channel) {
                const auto& samples = current->channels[channel];
                const std::size_t begin = pixel * samples.size() / pixels;
                const std::size_t end = std::max(begin + 1u,
                    std::min(samples.size(),
                        (pixel + 1u) * samples.size() / pixels));
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
    std::size_t page = 0u;
    std::string fileMessage;
};

} // namespace sample_kit_canvas

s3g::portable_gui::foundation::ContentView* makePortableCanvas(Plugin& p)
{
    return new sample_kit_canvas::Editor(p);
}
