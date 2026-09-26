namespace sample_neon_canvas {

using namespace s3g::portable_gui::canvas;
namespace foundation = s3g::portable_gui::foundation;
using I = s3g::sample_neon_gui::InspectorLayout;
using WaveDisplay = s3g::sample::SampleNeonWaveformDisplay;

class Editor final : public View {
public:
    explicit Editor(Plugin& instance)
        : View(kGuiWidth, kGuiHeight), p(instance)
    {
        setWantsFocus(true);
    }

    void onMouseDownEvent(MouseDownEvent& event) override {
        // Numeric text fields retain their own copy/paste. Only a pad click
        // claims keyboard focus for cell operations.
        if (cellAt(event.mousePosition) >= 0 && getFrame()) getFrame()->setFocusView(this);
        View::onMouseDownEvent(event);
    }
    void onKeyboardEvent(KeyboardEvent& event) override {
        if (resetConfirm || !getFrame() || getFrame()->getFocusView() != this || event.type != EventType::KeyDown
            || !(event.modifiers.has(ModifierKey::Control) || event.modifiers.has(ModifierKey::Super))
            || event.modifiers.has(ModifierKey::Alt)) return;
        const auto key = event.character;
        if (key == 'c' || key == 'C') { copyCell(p, p.selectedSlot.load()); event.consumed = true; }
        else if (key == 'v' || key == 'V') { requestPaste(p.selectedSlot.load()); event.consumed = true; }
        if (event.consumed) invalid();
    }
    bool pointerDown(MouseDownEvent& event) override {
        if (resetConfirm || !event.buttonState.isRight()) return false;
        const int cell = cellAt(event.mousePosition);
        if (cell < 0) return false;
        p.selectedSlot.store(static_cast<uint8_t>(cell));
        openPopup(rect(event.mousePosition.x, event.mousePosition.y, 164.0, 0.0),
            {"COPY CELL", "PASTE CELL"}, -1, [this, cell](int choice) {
                if (choice == 0) copyCell(p, static_cast<std::size_t>(cell)); else requestPaste(static_cast<std::size_t>(cell));
            }, 1, {}, 18.0, {bool(asset(static_cast<std::size_t>(cell))), bool(p.cellClipboard)});
        return true;
    }

    void service() override
    {
        serviceWorkflow(p);
#if defined(S3G_SAMPLE_FILE_WORKER)
        serviceLoads(p);
#endif
        serviceWavesets(p);
        workspaceTab = std::min<uint8_t>(7u, effectiveEditPage(p));
    }

    void paint() override
    {
        // A pad click and page change may precede the next service timer.
        // Draw and bind controls to the selected cell on this very paint.
        selectedSlot = std::min<std::size_t>(p.selectedSlot.load(
            std::memory_order_relaxed), s3g::sample::kSampleNeonSlotCount - 1u);
        workspaceTab = std::min<uint8_t>(7u, effectiveEditPage(p));
        paintHeader();
        paintController();
        paintSelectedSlot();
        if (resetConfirm) {
            // A deliberate two-step action; the waveform itself stays clear.
            hit(rect(0.0, 0.0, kGuiWidth, kGuiHeight), [] {});
            fill(rect(20.0, 52.0, 1128.0, 62.0), style.strip);
            stroke(rect(20.0, 52.0, 1128.0, 62.0), style.text);
            text("CLEAR ALL 32 CELLS + CAPTURE? SAVE A SET FIRST TO KEEP THIS WORK.",
                rect(32.0, 60.0, 780.0, 20.0), style.text);
            text("Output layout and MIDI setup are kept. Source files on disk are not deleted.",
                rect(32.0, 85.0, 780.0, 18.0), style.label);
            button(rect(855.0, 71.0, 132.0, 23.0), "CANCEL", [this] { resetConfirm = false; });
            button(rect(999.0, 71.0, 132.0, 23.0), "CLEAR ALL", [this] {
                resetConfirm = false; requestWorkflow(p, WorkflowCommand::ResetAll);
            });
        }
    }

    bool dropped(const std::vector<std::string>& paths, CPoint point) override
    {
        if (paths.empty() || p.visibleMode.load() == static_cast<uint8_t>(NeonMode::HotLoop)) return false;
        int target = slotAt(point);
        if (target < 0) target = static_cast<int>(selectedSlot);
        bool accepted = false;
        for (const auto& path : paths) {
            if (target >= static_cast<int>(s3g::sample::kSampleNeonSlotCount))
                break;
#if defined(S3G_SAMPLE_FILE_WORKER)
            queueSampleLoad(p, static_cast<std::size_t>(target), path);
            accepted = true;
#endif
            ++target;
        }
        return accepted;
    }

    void wheel(MouseWheelEvent& event) override
    {
        if (resetConfirm || !foundation::contains(waveformRect(), event.mousePosition)) return;
        const bool capture = p.visibleMode.load() == static_cast<uint8_t>(NeonMode::HotLoop);
        const auto current = capture ? p.captureAsset : asset(selectedSlot);
        if (!current) return;
        auto& view = waveformView(capture, current.get());
        const bool pan = event.modifiers.has(VSTGUI::ModifierKey::Shift)
            || std::abs(event.deltaX) > std::abs(event.deltaY);
        if (pan) {
            const double delta = event.deltaX != 0.0 ? event.deltaX : event.deltaY;
            view.pan(delta * 0.15);
        } else {
            if (event.deltaY == 0.0) return;
            // Fractional trackpad deltas remain smooth; a wheel notch is 1.25x.
            view.zoomAt(view.zoom * std::pow(1.25, std::clamp<double>(event.deltaY, -4.0, 4.0)),
                (event.mousePosition.x - waveformRect().left) / waveformRect().getWidth());
            (capture ? p.captureZoom : p.waveformZooms[selectedSlot]).store(static_cast<float>(view.zoom));
        }
        event.consumed = true;
        invalid();
    }

private:
    static CRect inspectorButton(unsigned row, unsigned column = 0u, unsigned count = 1u) {
        return rect(I::columnLeft(column, count), I::button(row), I::columnWidth(count), I::buttonHeight);
    }
    static CRect inspectorMenu(unsigned row) {
        return rect(I::controlLeft, I::menu(row), I::controlWidth, 25.0);
    }
    void inspectorLabel(const char* label, unsigned row) {
        text(label, rect(I::left, I::label(row), I::labelWidth, 18.0), style.label);
    }
    void inspectorReadout(const char* label, const char* value, unsigned row) {
        inspectorLabel(label, row);
        text(value, rect(I::controlLeft, I::label(row), I::controlWidth, 18.0), style.dim, kCenterText);
    }
    bool resetConfirm = false;
    int cellAt(CPoint point) const {
        const auto mode = static_cast<NeonMode>(p.visibleMode.load());
        const auto bank = p.visibleBank.load();
        if (mode == NeonMode::HotLoop) {
            if (!p.visibleLayer.load()) return -1;
        } else if (mode == NeonMode::Slicer || p.visibleLayer.load()) return -1;
        for (unsigned pad = 0u; pad < 8u; ++pad)
            if (foundation::contains(padRect(pad), point)) return static_cast<int>(bank * 8u + pad);
        return -1;
    }
    void requestPaste(std::size_t target) {
        if (!p.cellClipboard || p.resetAllPhase.load()) return;
        if (!asset(target) && path(target).empty()) { pasteCell(p, target); return; }
        openPopup(rect(651.0, 38.0, 260.0, 0.0),
            {format("REPLACE CELL %c%zu?", 'A' + target / 8u, target % 8u + 1u), "CANCEL", "PASTE / REPLACE"}, -1,
            [this, target](int choice) { if (choice == 2) pasteCell(p, target); }, 1, {}, 18.0, {false, true, true});
    }
    void techniqueEnvelopeRows(double y) {
        textureSliderRow(p.techniqueAttackSeconds[selectedSlot], "ATTACK", y, 0.001f, 10.0f,
            format("%.3f s", p.techniqueAttackSeconds[selectedSlot].load()));
        textureSliderRow(p.techniqueReleaseSeconds[selectedSlot], "RELEASE", y + I::rowPitch, 0.001f, 10.0f,
            format("%.3f s", p.techniqueReleaseSeconds[selectedSlot].load()));
    }
    std::array<s3g::sample::SampleNeonWaveformViewport, 33u> waveViews {};
    std::array<const SampleAsset*, 33u> waveAssets {};
    static CRect waveformRect() { return rect(32.0, 122.0, 793.0, 254.0); }
    // Use the shared s3g popup, with centered control labels everywhere.
    void menu(CRect bounds, const std::string& value,
        std::vector<std::string> items, int selected,
        std::function<void(int)> choose, bool enabled = true,
        int columns = 1, CHoriTxtAlign alignment = kCenterText, std::vector<bool> selectable = {})
    {
        // Preserve compact family metrics; right-panel fields share one column.
        const double top = bounds.getCenter().y - I::menuHeight * 0.5;
        const double width = bounds.left >= kPanelLeft ? I::controlWidth : std::min(164.0, bounds.getWidth());
        bounds = rect(bounds.right - width, top, width, I::menuHeight);
        auto palette = style;
        if (!enabled) palette.value = color(0x686868);
        foundation::drawMenuBox(*context, bounds, value, font, palette, alignment);
        hit(bounds, [this, bounds, items = std::move(items), selected,
            choose = std::move(choose), columns, selectable = std::move(selectable)] {
            openPopup(bounds, items, selected, choose, columns, {}, I::popupRowHeight, selectable);
        }, enabled);
    }

    void helpText(const std::string& message, double y)
    {
        // Operation status only; instructional prose lives in the guide.
        std::size_t start = 0u;
        while (start < message.size()) {
            std::size_t end = std::min(start + 43u, message.size());
            if (end < message.size()) {
                const auto space = message.rfind(' ', end);
                if (space != std::string::npos && space > start) end = space;
            }
            text(message.substr(start, end - start),
                rect(I::left, y, I::width, 18.0), style.dim);
            start = end;
            while (start < message.size() && message[start] == ' ') ++start;
            y += 20.0;
        }
    }

    bool routeValid(const SampleAsset* current) const
    {
        return current && s3g::sample::sampleNeonRouteCompatible(
            current->channelCount, static_cast<SampleNeonSourceFormat>(
                static_cast<uint8_t>(paramValue(p, slotParamId(selectedSlot, kSlotSourceFormat)))),
            static_cast<SampleNeonOutputLayout>(static_cast<uint8_t>(paramValue(p, kOutputLayoutParamId))),
            static_cast<uint32_t>(paramValue(p, slotParamId(selectedSlot, kSlotOutputBus))) - 1u);
    }

    std::string routeStatus(const SampleAsset* current) const
    {
        if (!current) return "STEREO USES CH 1-2 / SPATIAL FILES KEEP THEIR CHANNEL ORDER";
        const bool ambi = paramValue(p, slotParamId(selectedSlot, kSlotSourceFormat)) >= 0.5;
        if (!routeValid(current)) return format("%u CH %s / SILENT: MATCH OUTPUT LAYOUT, FORMAT AND BUS",
            current->channelCount, ambi ? "ACN/SN3D" : "DISCRETE");
        const auto layout = static_cast<SampleNeonOutputLayout>(
            static_cast<uint8_t>(paramValue(p, kOutputLayoutParamId)));
        const auto bus = static_cast<unsigned>(paramValue(p, slotParamId(selectedSlot, kSlotOutputBus)));
        const auto width = s3g::sample::sampleNeonBusWidth(layout);
        return format("%u CH %s / BUS %u: CH %u-%u / CHANNEL-LINKED",
            current->channelCount, ambi ? "ACN/SN3D" : "DISCRETE", bus,
            (bus - 1u) * width + 1u, bus * width);
    }

    static constexpr double kControllerLeft = 20.0;
    static constexpr double kControllerTop = 545.0;
    static constexpr double kControllerWidth = 817.0;
    static constexpr double kControllerHeight = 195.0;
    static constexpr double kPadLeft = 237.0;
    static constexpr double kPadTop = 615.0;
    static constexpr double kPadWidth = 82.0;
    static constexpr double kPadHeight = 45.0;
    static constexpr double kPadGapX = 7.0;
    static constexpr double kPadGapY = 8.0;
    static constexpr double kSlicePadTop = 412.0;
    static constexpr double kSlicePadHeight = 60.0;
    static constexpr double kSlicePadGapY = 10.0;
    static constexpr double kPanelLeft = 858.0;
    static constexpr double kPanelWidth = 302.0;

    CRect padRect(std::size_t pad) const
    {
        const std::size_t row = pad / 4u;
        const std::size_t column = pad % 4u;
        return rect(kPadLeft + column * (kPadWidth + kPadGapX),
            kPadTop + row * (kPadHeight + kPadGapY),
            kPadWidth, kPadHeight);
    }

    CRect slicePadRect(std::size_t pad) const
    {
        const std::size_t row = pad / 4u;
        const std::size_t column = pad % 4u;
        return rect(kPadLeft + column * (kPadWidth + kPadGapX),
            kSlicePadTop + row * (kSlicePadHeight + kSlicePadGapY),
            kPadWidth, kSlicePadHeight);
    }

    int slotAt(CPoint point) const
    {
        if (foundation::contains(rect(32.0, 82.0, 793.0, 430.0), point))
            return static_cast<int>(selectedSlot);
        if (p.visibleMode.load(std::memory_order_relaxed) == static_cast<uint8_t>(NeonMode::Slicer)
            || p.visibleMode.load() == static_cast<uint8_t>(NeonMode::HotLoop)) return -1;
        const std::size_t bank = p.visibleBank.load(
            std::memory_order_relaxed);
        for (std::size_t pad = 0u;
             pad < s3g::controller::reloop_neon::kPadsPerBank; ++pad)
            if (foundation::contains(padRect(pad), point))
                return static_cast<int>(bank * 8u + pad);
        return -1;
    }

    std::shared_ptr<const SampleAsset> asset(std::size_t slot) const
    {
        std::lock_guard<std::mutex> lock(p.statusMutex);
        return slot < p.controlAssets.size()
            ? p.controlAssets[slot] : nullptr;
    }

    std::string path(std::size_t slot) const
    {
        std::lock_guard<std::mutex> lock(p.statusMutex);
        return slot < p.samplePaths.size()
            ? p.samplePaths[slot] : std::string();
    }

    std::string status(std::size_t slot) const
    {
        std::lock_guard<std::mutex> lock(p.statusMutex);
        return slot < p.statuses.size()
            ? p.statuses[slot] : std::string();
    }

    std::string statusLabel() const
    {
        auto label = upper(status(selectedSlot));
        if (label.size() > 44u) label = label.substr(0u, 41u) + "...";
        return label;
    }

    void paintHeader()
    {
        foundation::drawPluginTitle(*context,
            "s3g SAMPLE NEON 32",
            rect(20.0, 14.0, 330.0, 23.0), titleFont);
        const float peak = std::clamp(p.outputPeak.load(
            std::memory_order_relaxed), 0.0f, 1.0f);
        fill(rect(220.0, 20.0, 100.0, 6.0), style.cell);
        fill(rect(220.0, 20.0, 100.0 * peak, 6.0),
            peak > 0.92f ? color(0xc8aaa0) : style.fill);
        const auto layout = static_cast<int>(paramValue(p, kOutputLayoutParamId));
        menu(rect(340.0, 10.0, 180.0, 28.0), kOutputLayoutNames[static_cast<std::size_t>(layout)],
            std::vector<std::string>(kOutputLayoutNames.begin(), kOutputLayoutNames.end()),
            layout, [this](int choice) {
                queueGuiParamGesture(p, kOutputLayoutParamId, choice);
            });
        text(format("%u VOICES", p.activeVoices.load(std::memory_order_relaxed)),
            rect(535.0, 12.0, 106.0, 22.0), style.value);
        button(rect(651.0, 12.0, 95.0, 23.0), "RESET ALL", [this] { resetConfirm = true; });
        const bool owns = paramValue(p, kNeonActiveParamId) >= 0.5;
        std::string ownerLabel = owns ? "NEON OWNER: ON" : "NEON OWNER: OFF";
#if defined(__APPLE__)
        if (owns) {
            switch (p.directNeonOutput.status()) {
            case DirectNeonOutput::Status::Connected:
                ownerLabel = "NEON: CONNECTED";
                break;
            case DirectNeonOutput::Status::Searching:
                ownerLabel = "NEON: SEARCHING";
                break;
            case DirectNeonOutput::Status::Error:
                ownerLabel = "NEON: MIDI ERROR";
                break;
            case DirectNeonOutput::Status::Off:
                ownerLabel = "NEON: STARTING";
                break;
            }
        }
#endif
        button(rect(760.0, 10.0, 155.0, 28.0),
            ownerLabel,
            [this, owns] {
                queueGuiParamGesture(p, kNeonActiveParamId,
                    owns ? 0.0 : 1.0);
                p.sendNeonInitialization.store(true,
                    std::memory_order_release);
                p.ledFeedbackDirty.store(true, std::memory_order_release);
            }, owns);
        button(rect(924.0, 10.0, 108.0, 28.0), "LOAD SET",
            [this] { chooseSet(false); });
        button(rect(1040.0, 10.0, 108.0, 28.0), "SAVE SET",
            [this] { chooseSet(true); });
    }

    void selectBank(uint8_t bank) { selectSurfaceBank(p, bank); }
    void selectMode(uint8_t mode)
    {
        if (mode > 3u) return;
        selectSurface(p, static_cast<NeonMode>(mode),
            p.visibleMode.load() == mode ? static_cast<uint8_t>(1u - p.visibleLayer.load()) : 0u);
    }

    CColor padAccent(std::size_t pad, bool bright) const
    {
        (void)pad;
        return color(bright ? 0x91aaa3 : 0x343a39);
    }


    s3g::sample::SampleNeonWaveformViewport& waveformView(bool capture, const SampleAsset* current)
    {
        const std::size_t index = capture ? 32u : selectedSlot;
        auto& view = waveViews[index];
        if (waveAssets[index] != current) {
            view = {}; waveAssets[index] = current;
        }
        const double start = paramValue(p, slotParamId(selectedSlot, kSlotStart));
        const double end = paramValue(p, slotParamId(selectedSlot, kSlotEnd));
        view.sync(capture ? p.captureZoom.load() : p.waveformZooms[selectedSlot].load(),
            capture ? p.captureCursor.load() : start + std::max(0.0, end - start) * p.editPositions[selectedSlot].load());
        return view;
    }

    void drawWaveTrace(CRect bounds, const SampleAsset* current,
        CColor traceColor, bool clear = true, double viewStart = 0.0,
        double viewEnd = 1.0, int onlyChannel = -1)
    {
        if (clear) {
            fill(bounds, color(0x111414));
            stroke(bounds, color(0x414746));
            line(CPoint(bounds.left, bounds.getCenter().y),
                CPoint(bounds.right, bounds.getCenter().y), color(0x292d2c));
        }
        if (!current || current->frameCount() == 0u) return;
        const std::size_t pixels = static_cast<std::size_t>(
            std::max(1.0, std::floor(bounds.getWidth())));
        const std::size_t total = current->frameCount();
        auto trace = owned(context->createGraphicsPath());
        if (!trace) return;
        const double center = bounds.getCenter().y;
        const double amplitude = std::max(1.0, bounds.getHeight() * 0.43);
        CPoint previous;
        for (std::size_t pixel = 0u; pixel < pixels; ++pixel) {
            float minimum = 1.0f;
            float maximum = -1.0f;
            const double unitBegin = viewStart + (viewEnd - viewStart)
                * static_cast<double>(pixel) / static_cast<double>(pixels);
            const double unitEnd = viewStart + (viewEnd - viewStart)
                * static_cast<double>(pixel + 1u)
                    / static_cast<double>(pixels);
            const std::size_t begin = std::min(total - 1u,
                static_cast<std::size_t>(std::floor(unitBegin * total)));
            const std::size_t end = std::max(begin + 1u,
                std::min(total, static_cast<std::size_t>(
                    std::ceil(unitEnd * total))));
            const std::size_t stride = std::max<std::size_t>(
                1u, (end - begin) / 20u);
            for (uint8_t channel = 0u; channel < current->channelCount;
                 ++channel) {
                if (onlyChannel >= 0 && channel != onlyChannel) continue;
                const auto& samples = current->channels[channel];
                for (std::size_t frame = begin; frame < end;
                     frame += stride) {
                    minimum = std::min(minimum, samples[frame]);
                    maximum = std::max(maximum, samples[frame]);
                }
                minimum = std::min(minimum, samples[end - 1u]);
                maximum = std::max(maximum, samples[end - 1u]);
            }
            const double x = bounds.left + static_cast<double>(pixel);
            const CPoint top(x, center - std::clamp(maximum, -1.0f, 1.0f) * amplitude);
            if (pixel != 0u && end - begin <= 2u) {
                trace->beginSubpath(previous);
                trace->addLine(top);
            }
            trace->beginSubpath(top);
            trace->addLine(CPoint(x, center - std::clamp(minimum, -1.0f, 1.0f) * amplitude));
            previous = top;
        }
        context->setFrameColor(traceColor);
        context->setLineWidth(1.0);
        context->drawGraphicsPath(trace, CDrawContext::kPathStroked);
    }

    void waveChannelLabel(CRect bounds, unsigned channel, bool ambi)
    {
        const auto label = rect(bounds.left + 4.0, bounds.top + 1.0, 46.0,
            std::min(14.0, bounds.getHeight() - 2.0));
        fill(label, color(0x111414));
        text(format(ambi ? "ACN %u" : "CH %u", ambi ? channel : channel + 1u),
            label, style.label, kLeftText, smallFont);
    }

    void drawRecordingWave(CRect bounds, WaveDisplay display)
    {
        fill(bounds, color(0x111414)); stroke(bounds, style.grid);
        const unsigned frames = p.recorder.waveFrames.load(std::memory_order_acquire);
        const unsigned bins = std::min(p.recorder.waveCount.load(std::memory_order_acquire), s3g::sample::SampleNeonRecorder::kWaveBins);
        const unsigned stride = p.recorder.waveStride.load();
        const double visibleFrames = std::max(p.sampleRate, static_cast<double>(frames));
        const double head = bounds.left + bounds.getWidth() * frames / visibleFrames;
        const unsigned columns = static_cast<unsigned>(std::max(0.0, std::ceil(head - bounds.left)));
        const unsigned channels = std::clamp(p.recorder.waveChannels.load(), 1u, 16u);
        const unsigned rows = s3g::sample::sampleNeonWaveformRows(display, channels);
        for (unsigned row = 0u; row < rows; ++row) {
            const double height = bounds.getHeight() / rows;
            const double center = bounds.top + (row + 0.5) * height;
            const int selected = s3g::sample::sampleNeonWaveformChannel(display, row);
            const unsigned firstChannel = selected < 0 ? 0u : static_cast<unsigned>(selected);
            const unsigned endChannel = selected < 0 ? channels : firstChannel + 1u;
            if (row) line({bounds.left, bounds.top + row * height},
                {bounds.right, bounds.top + row * height}, color(0x414746));
            line({bounds.left, center}, {bounds.right, center}, color(0x292d2c));
            for (unsigned pixel = 0u; pixel < columns && bins; ++pixel) {
                float lo = 0.0f, hi = 0.0f;
                const unsigned first = std::min(bins - 1u, static_cast<unsigned>(pixel * visibleFrames / bounds.getWidth()) / stride);
                const unsigned last = std::min(bins, 1u + static_cast<unsigned>((pixel + 1u) * visibleFrames / bounds.getWidth()) / stride);
                for (unsigned bin = first; bin < last; ++bin)
                    for (unsigned ch = firstChannel; ch < endChannel; ++ch) {
                        lo = std::min(lo, p.recorder.waveform[ch][bin].minimum.load(std::memory_order_relaxed));
                        hi = std::max(hi, p.recorder.waveform[ch][bin].maximum.load(std::memory_order_relaxed));
                    }
                const double x = bounds.left + pixel + 0.5;
                line({x, center - std::clamp(hi, -1.0f, 1.0f) * height * 0.43}, {x, center - std::clamp(lo, -1.0f, 1.0f) * height * 0.43}, style.fill,
                    1.0);
            }
            if (selected >= 0) waveChannelLabel(
                rect(bounds.left, bounds.top + row * height, bounds.getWidth(), height),
                static_cast<unsigned>(selected), p.captureLayout.load() >= 4u);
        }
        const double x = std::clamp(head, bounds.left + 1.0, bounds.right - 1.0);
        line({x, bounds.top}, {x, bounds.bottom}, color(0xb27d69), 2.0);
    }

    void paintWorkspace()
    {
        const auto mode = static_cast<NeonMode>(p.visibleMode.load());
        const bool chop = mode == NeonMode::Slicer;
        const bool capture = mode == NeonMode::HotLoop;
        const bool recording = capture && p.captureState.load() == Plugin::CaptureState::Recording;
        const bool editable = mode != NeonMode::Sampler;
        const auto current = capture ? p.captureAsset : asset(selectedSlot);
        const char* names[] { "PLAY", "CHOP", "EDIT", "RESAMPLE" };
        panel(rect(20.0, 52.0, 817.0, 480.0),
            format("%s / CELL %c%zu / %s", names[static_cast<unsigned>(mode)],
                static_cast<char>('A' + selectedSlot / 8u), selectedSlot % 8u + 1u,
                capture ? "OUTPUT BUS CAPTURE" : chop ? "SELECTED CELL / SLICE MAP" : "32 ORDINARY CELLS"));
        const bool ambi = capture
            ? p.captureLayout.load() >= static_cast<uint8_t>(SampleNeonOutputLayout::Ambisonic1)
            : paramValue(p, slotParamId(selectedSlot, kSlotSourceFormat)) >= 0.5;
        const auto display = p.waveformDisplay.load();
        const std::vector<std::string> waveChoices {
            "COMBINED PEAKS", ambi ? "ACN 0" : "CHANNEL 1", "ALL CHANNELS" };
        text("WAVE", rect(618.0, 53.0, 38.0, 18.0), style.label);
        menu(rect(661.0, 50.0, 164.0, 24.0), waveChoices[static_cast<unsigned>(display)],
            waveChoices, static_cast<int>(display), [this](int choice) {
                // Like zoom, a view choice must not alter any sound/settings.
                // Stored on the instance so closing/reopening the GUI keeps it.
                p.waveformDisplay.store(static_cast<WaveDisplay>(choice));
            });
        const double start = capture ? p.captureStart.load()
            : paramValue(p, slotParamId(selectedSlot, kSlotStart));
        const double end = capture ? p.captureEnd.load()
            : paramValue(p, slotParamId(selectedSlot, kSlotEnd));
        const double range = std::max(0.0, end - start);
        const double edit = capture ? p.captureCursor.load()
            : start + range * p.editPositions[selectedSlot].load();
        const auto& viewport = waveformView(capture, current.get());
        const double zoom = viewport.zoom;
        const double viewRange = viewport.width();
        const double viewStart = viewport.start;
        const double viewEnd = viewStart + viewRange;
        const CRect wave = waveformRect();
        if (chop) {
            const unsigned chopMode = p.chopModes[selectedSlot].load();
            const std::vector<std::string> choices { "LIVE MARKERS", "TRANSIENTS", "EQUAL", "BEAT GRID" };
            menu(rect(32.0, 86.0, 142.0, 24.0), choices[chopMode], choices,
                static_cast<int>(chopMode), [this](int choice) { setChopMode(p, selectedSlot, static_cast<uint8_t>(choice)); });
            std::vector<std::string> counts;
            for (unsigned n = 1u; n <= 32u; ++n) counts.push_back(format("%u SLICES", n));
            const auto count = sliceCount(p, selectedSlot);
            menu(rect(184.0, 86.0, 114.0, 24.0), format("%u SLICES", count), counts, count - 1u,
                [this](int choice) { setSliceCount(p, selectedSlot, choice + 1); }, true, 4);
            button(rect(308.0, 89.0, 92.0, 18.0), "ADD MARK", [this] { chopToolAction(p, 0u); }, false, bool(current));
            button(rect(410.0, 89.0, 92.0, 18.0), "DELETE", [this] { chopToolAction(p, 1u); }, false, bool(current));
            text(format("SLICE %02u / %02u", p.visibleSlice.load() + 1u, count),
                rect(517.0, 89.0, 168.0, 18.0), style.value);
        } else if (capture) {
            const auto state = p.captureState.load();
            button(rect(32.0, 89.0, 90.0, 18.0), "RECORD", [this] { capturePadAction(p, 0u, false); },
                state == Plugin::CaptureState::Recording, state == Plugin::CaptureState::Empty);
            button(rect(132.0, 89.0, 90.0, 18.0), "STOP", [this] { capturePadAction(p, 1u, false); },
                false, state == Plugin::CaptureState::Recording);
            button(rect(232.0, 89.0, 90.0, 18.0), "AUDITION", [this] { capturePadAction(p, 2u, false); },
                false, bool(current));
            button(rect(332.0, 89.0, 120.0, 18.0), "ASSIGN TO CELL",
                [this] { requestWorkflow(p, WorkflowCommand::AssignCapture); }, false, bool(current));
            text(format("%.2f SEC / %s", p.captureFrames.load() / (current ? current->sampleRate : p.sampleRate),
                state == Plugin::CaptureState::Recording ? "RECORDING" : current ? "REVIEW" : "READY"),
                rect(468.0, 89.0, 216.0, 18.0), style.value);
        } else if (editable) {
            button(rect(32.0, 89.0, 126.0, 18.0), "CURSOR > START",
                [this, edit] { queueGuiParamGesture(p, slotParamId(selectedSlot, kSlotStart), edit); }, false, bool(current));
            button(rect(168.0, 89.0, 126.0, 18.0), "CURSOR > END",
                [this, edit] { queueGuiParamGesture(p, slotParamId(selectedSlot, kSlotEnd), edit); }, false, bool(current));
        } else {
            button(rect(543.0, 89.0, 142.0, 18.0), "EDIT SELECTED CELL",
                [this] { selectSurface(p, NeonMode::HotCue, 0u); });
        }
        const bool zero = capture ? p.captureZeroCross.load()
            : paramValue(p, slotParamId(selectedSlot, kSlotZeroCross)) >= 0.5;
        if (editable) button(rect(707.0, 89.0, 118.0, 18.0),
            zero ? "ZERO CROSS ON" : "ZERO CROSS OFF", [this, capture, zero] {
                if (capture) {
                    p.captureZeroCross.store(!zero);
                    if (!zero) { setCaptureTrim(p, false, p.captureStart.load()); setCaptureTrim(p, true, p.captureEnd.load()); }
                } else queueGuiParamGesture(p, slotParamId(selectedSlot, kSlotZeroCross), zero ? 0.0 : 1.0);
                markStateDirty(p);
            }, zero);
        if (recording) drawRecordingWave(wave, display);
        else {
            const unsigned rows = s3g::sample::sampleNeonWaveformRows(display,
                current ? current->channelCount : 1u);
            const double height = wave.getHeight() / rows;
            for (unsigned rowIndex = 0u; rowIndex < rows; ++rowIndex) {
                const auto row = rect(wave.left, wave.top + rowIndex * height, wave.getWidth(), height);
                const int channel = s3g::sample::sampleNeonWaveformChannel(display, rowIndex);
                drawWaveTrace(row, current.get(), style.fill, true, viewStart, viewEnd, channel);
                if (current && channel >= 0) waveChannelLabel(row, static_cast<unsigned>(channel), ambi);
            }
        }
        if (!current && !recording) text(capture ? "NO CAPTURE" : "NO SAMPLE",
            wave, style.dim, kCenterText);
        const auto xFor = [wave, viewStart, viewRange](double value) {
            return wave.left + wave.getWidth() * (value - viewStart) / viewRange;
        };
        if (editable && current) drag(wave, [this, capture, wave, viewStart, viewRange, start, range](CPoint point) {
            const double absolute = std::clamp(viewStart + viewRange * (point.x - wave.left) / wave.getWidth(), 0.0, 1.0);
            if (capture) p.captureCursor.store(absolute);
            else p.editPositions[selectedSlot].store(std::clamp((absolute - start) / std::max(1.0e-9, range), 0.0, 1.0));
            markStateDirty(p);
        });
        for (unsigned handle = 0u; handle < 2u; ++handle) {
            const double position = handle ? end : start;
            if (position < viewStart || position > viewEnd) continue;
            const double x = xFor(position);
            line(CPoint(x, wave.top), CPoint(x, wave.bottom), style.text, 2.0);
            if (editable && current) drag(rect(x - 6.0, wave.top, 12.0, wave.getHeight()),
                [this, capture, handle, wave, viewStart, viewRange](CPoint point) {
                    const double position = std::clamp(viewStart + viewRange * (point.x - wave.left) / wave.getWidth(), 0.0, 1.0);
                    if (capture) setCaptureTrim(p, handle != 0u, position);
                    else queueGuiParamValue(p, slotParamId(selectedSlot, handle ? kSlotEnd : kSlotStart), position);
                }, [this, capture, handle] { if (!capture) queueGuiParamBegin(p, slotParamId(selectedSlot, handle ? kSlotEnd : kSlotStart)); },
                [this, capture, handle] { if (!capture) queueGuiParamEnd(p, slotParamId(selectedSlot, handle ? kSlotEnd : kSlotStart)); });
        }
        if (chop) {
            const auto layout = sliceLayout(p, selectedSlot);
            for (uint8_t marker = 1u; marker < layout.sliceCount; ++marker) {
                const double position = start + range * layout.boundaries[marker];
                if (position < viewStart || position > viewEnd) continue;
                const double x = xFor(position);
                line(CPoint(x, wave.top), CPoint(x, wave.bottom), color(0x758a84));
                drag(rect(x - 5.0, wave.top, 10.0, wave.getHeight()),
                    [this, marker, wave, viewStart, viewRange, start, range](CPoint point) {
                        moveLiveSliceMarker(p, selectedSlot, marker,
                            (viewStart + viewRange * (point.x - wave.left) / wave.getWidth() - start) / std::max(1.0e-9, range));
                    });
            }
        }
        if (!capture) {
            if (!chop) {
                const bool moving = playbackChoice() == 1u || playbackChoice() == 4u;
                const double next = start + range * (moving
                    ? p.motionPositions[selectedSlot].load() : p.editPositions[selectedSlot].load());
                if (next >= viewStart && next <= viewEnd) {
                    const double x = xFor(next);
                    line(CPoint(x, wave.top), CPoint(x, wave.bottom), color(0x91aaa3), 2.0);
                    text(moving ? "SCAN" : "START", rect(std::clamp(x + 4.0, wave.left, wave.right - 38.0),
                        wave.bottom + 1.0, 34.0, 10.0), style.value, kCenterText, smallFont);
                }
            }
            const auto count = std::min<uint8_t>(p.voiceCursorCounts[selectedSlot].load(), static_cast<uint8_t>(s3g::sample::kMaximumVoices));
            for (uint8_t cursor = 0u; cursor < count; ++cursor) {
                const double position = p.voiceCursorPositions[selectedSlot][cursor].load();
                if (position >= viewStart && position <= viewEnd)
                    line(CPoint(xFor(position), wave.top), CPoint(xFor(position), wave.bottom), style.text, 1.0);
            }
        } else {
            const double position = p.capturePlayhead.load();
            if (position >= viewStart && position <= viewEnd)
                line(CPoint(xFor(position), wave.top), CPoint(xFor(position), wave.bottom), style.text, 1.0);
        }
        if (editable && edit >= viewStart && edit <= viewEnd)
            line(CPoint(xFor(edit), wave.top), CPoint(xFor(edit), wave.bottom), color(0xb27d69), 2.0);
        text(format("CURSOR %.2f%%", edit * 100.0), rect(32.0, 430.0, 142.0, 18.0), style.value);
        text(format("ZOOM %.1fx", zoom), rect(32.0, 455.0, 142.0, 18.0), style.value);
        const auto overview = rect(177.0, 426.0, 635.0, 72.0);
        if (recording) drawRecordingWave(overview, WaveDisplay::CombinedPeaks);
        else drawWaveTrace(overview, current.get(), style.fill);
        stroke(rect(overview.left + overview.getWidth() * viewStart, overview.top,
            overview.getWidth() * viewRange, overview.getHeight()), style.value);
        text(capture ? recording ? "RECORDING / LIVE WAVEFORM / CHANNEL-LINKED" : "CAPTURE REVIEW / CHANNEL-LINKED" : routeStatus(current.get()),
            rect(32.0, 510.0, 793.0, 18.0), !capture && current && !routeValid(current.get())
                ? color(0xc28f7a) : style.value);
    }


    unsigned playbackChoice() const
    {
        return playbackIndex(p, selectedSlot);
    }

    void playbackMenu(double y)
    {
        const unsigned choice = playbackChoice();
        text("PLAYBACK", rect(I::left, y + 3.5, I::labelWidth, 18.0), style.label);
        std::vector<std::string> names(kPlaybackNames.begin(), kPlaybackNames.end());
        std::vector<bool> available(names.size(), true);
        if (paramValue(p, slotParamId(selectedSlot, kSlotSourceFormat)) >= 0.5) { names[5] += " / NOT AMBI"; available[5] = false; }
        menu(rect(I::controlLeft, y, I::controlWidth, 25.0), names[choice], names, static_cast<int>(choice),
            [this](int index) {
                choosePlayback(p, static_cast<unsigned>(index));
                workspaceTab = effectiveEditPage(p);
                auditionEditedSlot(p, selectedSlot);
            }, true, 1, kCenterText, available);
    }

    void triggerMenu(double y)
    {
        const auto trigger = static_cast<int>(paramValue(p, slotParamId(selectedSlot, kSlotTriggerMode)));
        const int choice = trigger == 1 ? 1 : trigger == 3 ? 2 : 0;
        text("TRIGGER", rect(I::left, y + 3.5, I::labelWidth, 18.0), style.label);
        const std::vector<std::string> names { "ONE SHOT", "HOLD", "TOGGLE" };
        menu(rect(I::controlLeft, y, I::controlWidth, 25.0), names[static_cast<std::size_t>(choice)], names, choice,
            [this](int index) { queueGuiParamGesture(p, slotParamId(selectedSlot, kSlotTriggerMode),
                index == 0 ? 2.0 : index == 1 ? 1.0 : 3.0); });
    }

    void characterMenu(double y)
    {
        const auto selected = static_cast<unsigned>(paramValue(p, slotParamId(selectedSlot, kSlotCharacter)));
        std::vector<std::string> names;
        std::vector<bool> available;
        for (unsigned n = 0u; n < 8u; ++n) {
            available.push_back(fxAllowed(p, selectedSlot, n));
            names.push_back(upper(characterName(static_cast<int>(n))) + (available.back() ? "" : " / NOT AMBI"));
        }
        text("CHARACTER", rect(I::left, y + 3.5, I::labelWidth, 18.0), style.label);
        menu(rect(I::controlLeft, y, I::controlWidth, 25.0), names[selected], names, static_cast<int>(selected),
            [this](int choice) { queueGuiParamGesture(p, slotParamId(selectedSlot, kSlotCharacter), choice); },
            true, 1, kCenterText, available);
    }

    void paintFxSettings()
    {
        const auto effect = static_cast<unsigned>(paramValue(p, slotParamId(selectedSlot, kSlotCharacter)));
        characterMenu(I::menu(4));
        sliderRow(slotParamId(selectedSlot, kSlotMangle), "FX AMOUNT", I::slider(5));
        sliderRow(slotParamId(selectedSlot, kSlotPressureDepth), "PRESSURE", I::slider(6));
        for (unsigned n = 0u; n < 3u; ++n) {
            auto& value = p.fxParameters[selectedSlot][effect][n];
            const double y = I::slider(7u + n);
            if ((effect == 0u && n == 0u) || (effect == 3u && n == 1u)) {
                const std::vector<std::string> names = effect == 0u ? std::vector<std::string>{"LOW PASS", "HIGH PASS", "BAND PASS"}
                    : std::vector<std::string>{"SINE", "TRIANGLE", "SQUARE"};
                text(s3g::sample::kSampleNeonFxLabels[effect][n], rect(I::left, y - 2.0, I::labelWidth, 18.0), style.label);
                menu(inspectorMenu(7u + n), names[static_cast<unsigned>(std::lround(value.load() * 2.0f))], names,
                    static_cast<int>(std::lround(value.load() * 2.0f)), [this, effect, n](int choice) {
                        p.fxParameters[selectedSlot][effect][n].store(choice * 0.5f); markStateDirty(p); requestProcess(p);
                    });
            } else textureSliderRow(value, s3g::sample::kSampleNeonFxLabels[effect][n], y, 0.0f, 1.0f, fxValue(effect, n, value.load()),
                [this, effect, n](double x) { return fxDisplay(effect, n, x); },
                [this, effect, n](double x) {
                    double lo = 0.0, hi = 1.0;
                    for (unsigned i = 0u; i < 30u; ++i) { const double mid = (lo + hi) * 0.5; if (fxDisplay(effect, n, mid) < x) lo = mid; else hi = mid; }
                    return (lo + hi) * 0.5;
                });
        }
        inspectorLabel("ENCODERS", 10);
        menu(inspectorMenu(10), p.fxPair.load() ? "PARAM 3 / AMOUNT" : "PARAM 1 / PARAM 2",
            {"PARAM 1 / PARAM 2", "PARAM 3 / AMOUNT"}, p.fxPair.load(), [this](int choice) { p.fxPair.store(static_cast<uint8_t>(choice)); });
        button(inspectorButton(11), "RESET THIS EFFECT", [this, effect] {
            for (unsigned n = 0u; n < 3u; ++n) p.fxParameters[selectedSlot][effect][n].store(s3g::sample::kSampleNeonFxDefaults[effect][n]);
            markStateDirty(p); requestProcess(p);
        });
        if (paramValue(p, slotParamId(selectedSlot, kSlotSourceFormat)) >= 0.5)
            text("ACN/SN3D: DRIVE / CRUSH DISABLED", rect(I::left, I::statusTop, I::width, 18.0), style.dim);
    }

    double fxDisplay(unsigned effect, unsigned index, double value) const {
        if (effect == 0u && index == 1u) return 20.0 * std::pow(1000.0, value);
        if (effect == 1u && index == 0u) return 1.0 + value * 999.0;
        if (effect == 2u && index == 0u) return 20.0 * std::pow(100.0, value);
        if (effect == 3u && index == 0u) return std::pow(2000.0, value);
        if ((effect == 4u || effect == 5u) && index == 0u) return 0.1 * std::pow(effect == 4u ? 120.0 : 320.0, value);
        if (effect == 5u && index == 2u) return 1.0 + value * 39.0;
        if (effect == 7u && index == 0u) return 3.0 + value * 13.0;
        if (effect == 7u && index == 1u) return p.sampleRate / std::pow(128.0, 1.0 - value);
        return value * 100.0;
    }
    std::string fxValue(unsigned effect, unsigned index, float value) const {
        if (effect == 0u && index == 1u) return format("%.0f Hz", 20.0 * std::pow(1000.0, value));
        if (effect == 1u && index == 0u) return format("%.0f ms", 1.0 + value * 999.0);
        if (effect == 2u && index == 0u) return format("%.0f Hz", 20.0 * std::pow(100.0, value));
        if (effect == 3u && index == 0u) return format("%.1f Hz", std::pow(2000.0, value));
        if ((effect == 4u || effect == 5u) && index == 0u) return format("%.2f Hz", 0.1 * std::pow(effect == 4u ? 120.0 : 320.0, value));
        if (effect == 5u && index == 2u) return format("%.1f ms", 1.0 + value * 39.0);
        if (effect == 7u && index == 0u) return format("%.0f bit", 3.0 + std::round(value * 13.0));
        if (effect == 7u && index == 1u) return format("%.0f Hz", p.sampleRate / std::pow(128.0, 1.0 - value));
        return format("%.0f%%", value * 100.0);
    }

    void clockMenu(double y)
    {
        const bool host = p.playbackClocks[selectedSlot].load() != 0u;
        text("CLOCK", rect(I::left, y + 3.5, I::labelWidth, 18.0), style.label);
        menu(rect(I::controlLeft, y, I::controlWidth, 25.0), host ? "HOST TRANSPORT" : "FREE / PAD",
            { "FREE / PAD", "HOST TRANSPORT" }, host ? 1 : 0,
            [this](int choice) {
                p.playbackClocks[selectedSlot].store(static_cast<uint8_t>(choice));
                markStateDirty(p); requestProcess(p);
            });
    }

    void beatMenu(std::atomic<float>& value, const char* label, double y, double divisor = 1.0)
    {
        std::vector<std::string> names;
        for (const auto beats : kMotionCycleBeats) names.push_back(format("%.3g BEATS", beats / divisor));
        const auto selected = nearestMotionCycleIndex(static_cast<float>(value.load() * divisor));
        text(label, rect(I::left, y + 3.5, I::labelWidth, 18.0), style.label);
        menu(rect(I::controlLeft, y, I::controlWidth, 25.0), names[selected], names, static_cast<int>(selected),
            [this, &value, divisor](int index) {
                value.store(static_cast<float>(kMotionCycleBeats[static_cast<std::size_t>(index)] / divisor));
                markStateDirty(p); requestProcess(p);
            });
    }

    void paintMiniNeon()
    {
        const auto mode = static_cast<NeonMode>(p.visibleMode.load());
        const uint8_t bank = p.visibleBank.load();
        const bool secondary = p.visibleLayer.load() != 0u;
        const bool fxPads = secondary && (mode == NeonMode::Sampler || (mode == NeonMode::HotCue && effectiveEditPage(p) == 7u));
        const char* layerNames[4][2] {
            { "PERFORM", "PAD FX" }, { "SLICES", "TOOLS" },
            { "SELECT + PLAY", "EDIT + AUDITION" }, { "RECORD TOOLS", "PLAY / EMPTY = TARGET" }
        };
        panel(rect(kControllerLeft, kControllerTop, kControllerWidth, kControllerHeight),
            format("NEON / BANK %c / %s", 'A' + bank, layerNames[static_cast<unsigned>(mode)][secondary ? 1 : 0]));
        static constexpr const char* modes[] { "PLAY", "CHOP", "EDIT", "RESAMPLE" };
        for (uint8_t index = 0u; index < 4u; ++index)
            button(rect(kPadLeft + index * (kPadWidth + kPadGapX), 578.0, kPadWidth, 21.0),
                modes[index], [this, index] { selectMode(index); }, static_cast<uint8_t>(mode) == index);
        const auto editPage = mode == NeonMode::HotCue ? effectiveEditPage(p) : 0u;
        text(editPage == 7u ? (p.fxPair.load() ? "TRAX / FX AMOUNT" : "TRAX / PARAM 2")
            : editPage == 5u ? "TRAX / TUNE" : editPage == 4u || editPage == 6u ? "TRAX / REPEATS"
            : editPage == 2u ? "TRAX / CYCLE" : editPage == 3u ? "TRAX / RATE" : "TRAX",
            rect(39.0, 578.0, 160.0, 16.0), style.label, kCenterText);
        const bool performance = mode == NeonMode::Sampler;
        const bool capture = mode == NeonMode::HotLoop;
        auto& zoom = capture ? p.captureZoom : p.waveformZooms[selectedSlot];
        const double trax = editPage >= 4u ? advancedKnobValue(p, false) : performance ? paramValue(p, kGlobalMangleParamId)
            : editPage == 2u ? (p.playbackClocks[selectedSlot].load()
                ? nearestMotionCycleIndex(p.motionRates[selectedSlot].load()) / 7.0
                : (p.motionSeconds[selectedSlot].load() - 0.05) / 29.95)
            : editPage == 3u ? (p.playbackClocks[selectedSlot].load()
                ? nearestMotionCycleIndex(p.grainIntervals[selectedSlot].load() * 8.0f) / 7.0
                : (p.grainDensities[selectedSlot].load() - 1.0) / 79.0)
            : std::log2(std::max(1.0f, zoom.load())) / 5.0;
        foundation::drawHorizontalSlider(*context, rect(40.0, 601.0, 160.0, 8.0), trax, 598.0, 12.0, style);
        drag(rect(36.0, 594.0, 168.0, 24.0), [this, capture, performance, editPage](CPoint point) {
            const double value = std::clamp((point.x - 40.0) / 160.0, 0.0, 1.0);
            if (editPage >= 4u) setAdvancedKnob(p, false, value);
            else if (performance) queueGuiParamValue(p, kGlobalMangleParamId, value);
            else if (editPage == 2u) {
                if (p.playbackClocks[selectedSlot].load()) p.motionRates[selectedSlot].store(kMotionCycleBeats[static_cast<std::size_t>(std::lround(value * 7.0))]);
                else p.motionSeconds[selectedSlot].store(static_cast<float>(0.05 + value * 29.95));
            } else if (editPage == 3u) {
                if (p.playbackClocks[selectedSlot].load()) p.grainIntervals[selectedSlot].store(kMotionCycleBeats[static_cast<std::size_t>(std::lround(value * 7.0))] / 8.0f);
                else p.grainDensities[selectedSlot].store(static_cast<float>(1.0 + value * 79.0));
            }
            else (capture ? p.captureZoom : p.waveformZooms[selectedSlot]).store(static_cast<float>(std::pow(2.0, value * 5.0)));
            markStateDirty(p); requestProcess(p);
        });
        for (uint8_t index = 0u; index < 4u; ++index)
            button(rect(39.0 + index * 44.0, 626.0, 38.0, 25.0), format("%c", 'A' + index),
                [this, index] { selectBank(index); }, bank == index);
        for (uint8_t index = 0u; index < 4u; ++index) {
            unsigned loaded = 0u;
            for (uint8_t pad = 0u; pad < 8u; ++pad)
                loaded += p.publishedAssets[index * 8u + pad].load() != nullptr ? 1u : 0u;
            text(format("%u/8", loaded), rect(39.0 + index * 44.0, 650.0, 38.0, 13.0),
                loaded ? style.value : style.dim, kCenterText);
        }
        // Secondary-page selection is explicit, never disguised as Shift.
        button(rect(39.0, 665.0, 82.0, 21.0), capture ? (secondary ? "REC TOOLS" : "PLAY PADS") : secondary ? "PRIMARY" : "SECOND",
            [this, mode, secondary] { selectSurface(p, mode, secondary ? 0u : 1u); }, secondary);
        button(rect(128.0, 665.0, 82.0, 21.0), "KILL", [this] {
            p.killRequested.store(true); p.captureAudition.store(false); requestProcess(p);
        });
        for (uint8_t pad = 0u; pad < 8u; ++pad) {
            const auto bounds = padRect(pad);
            const auto cell = static_cast<uint8_t>(bank * 8u + pad);
            bool enabled = true, active = false;
            std::string label;
            if (fxPads) {
                label = upper(characterName(pad));
                enabled = fxAllowed(p, selectedSlot, pad);
                active = paramValue(p, slotParamId(selectedSlot, kSlotCharacter)) == pad;
            } else if (mode == NeonMode::Slicer && secondary) {
                const char* tools[] { "ADD", "DELETE", "AUDITION", "ZERO", "EQUAL", "TRANSIENT", "ASSIGN 1", "ASSIGN ALL" };
                label = tools[pad];
                enabled = bool(asset(selectedSlot));
                active = pad == 3u && paramValue(p, slotParamId(selectedSlot, kSlotZeroCross)) >= 0.5;
            } else if (mode == NeonMode::Slicer) {
                label = format("SL %02u", cell + 1u);
                enabled = bool(asset(selectedSlot)) && cell < sliceCount(p, selectedSlot);
                active = cell == p.visibleSlice.load();
            } else if (mode == NeonMode::HotCue && secondary) {
                const char* pages[] { "SOURCE", "SAMPLE", "MOTION", "GRAINS", "ONE SHOT", "HOLD", "TOGGLE", "STOP" };
                label = pages[pad];
                active = pad == 0u ? effectiveEditPage(p) == 0u
                    : pad < 4u ? playbackChoice() == pad - 1u
                    : pad < 7u ? paramValue(p, slotParamId(selectedSlot, kSlotTriggerMode))
                        == (pad == 4u ? 2.0 : pad == 5u ? 1.0 : 3.0) : false;
            } else if (capture && !secondary) {
                const char* actions[] { "RECORD", "STOP", "AUDITION", "DISCARD", "BUS -", "BUS +", "REVIEW", "ASSIGN" };
                label = actions[pad];
                active = pad == 0u && p.captureState.load() == Plugin::CaptureState::Recording;
            } else {
                label = format("%c%u", 'A' + bank, pad + 1u);
                active = capture && !asset(cell) ? p.captureTarget.load() == cell : selectedSlot == cell;
            }
            const bool cellPad = (mode == NeonMode::Sampler && !secondary)
                || (mode == NeonMode::HotCue && !secondary) || (capture && secondary);
            const bool loaded = cellPad && p.publishedAssets[cell].load() != nullptr;
            const bool hasLink = cellPad && !loaded && !path(cell).empty();
            fill(bounds, active ? style.buttonActive : cellPad && loaded ? color(0x353535) : style.strip);
            stroke(bounds, active ? style.text : style.grid, active ? 2.0 : 1.0);
            text(label, rect(bounds.left + 2.0, bounds.top + (cellPad ? 1.0 : 7.0), bounds.getWidth() - 4.0, 17.0),
                enabled && (!cellPad || loaded) ? style.text : style.dim, kCenterText);
            if (cellPad) {
                const auto indicator = rect(bounds.left + 5.0, bounds.top + 5.0, 5.0, 5.0);
                if (loaded) fill(indicator, style.value); else stroke(indicator, style.dim);
                text(loaded ? (capture ? "PLAY" : "AUDIO") : hasLink ? (status(cell) == "DECODING..." ? "LOADING" : "OFFLINE")
                        : capture ? "TARGET" : "EMPTY",
                    rect(bounds.left + 2.0, bounds.top + 19.0, bounds.getWidth() - 4.0, 12.0),
                    loaded ? style.value : style.dim, kCenterText, smallFont);
                const double level = std::clamp<double>(p.slotPeaks[cell].load(), 0.0, 1.0);
                if (loaded && level > 0.001)
                    fill(rect(bounds.left + 3.0, bounds.top + 32.0, (bounds.getWidth() - 6.0) * level, 2.0), style.value);
            }
            if ((mode == NeonMode::Sampler && !secondary) || (mode == NeonMode::HotCue && !secondary)
                || (capture && secondary && loaded)) {
                const int trigger = static_cast<int>(paramValue(p, slotParamId(cell, kSlotTriggerMode)));
                const bool generated = normalizedStageOptions(p.textureOptions[cell].load()) != 0u;
                for (uint8_t lamp = 0u; lamp < 5u; ++lamp) {
                    const bool on = lamp < 3u ? lamp == (trigger == 3 ? 1 : trigger == 1 ? 2 : 0)
                        : lamp == 3u ? (generated ? trigger == 3 || trigger == 1
                            : slotOptionEnabled(p, cell, kSlotRepeatOption)
                                || paramValue(p, slotParamId(cell, kSlotDirection)) >= 2.0)
                        : generated ? playbackIndex(p, cell) != 5u && p.playbackClocks[cell].load() != 0u : slotOptionEnabled(p, cell, kSlotSyncOption);
                    fill(rect(bounds.left + 5.0 + lamp * 15.0, bounds.bottom - 8.0, 10.0, 3.0),
                        loaded && on ? style.fill : style.cell);
                }
            }
            if (!enabled) continue;
            if (!fxPads) hit(bounds, [this, mode, secondary, pad, cell] {
                if (mode == NeonMode::HotCue) {
                    if (secondary) editToolAction(p, pad);
                    else p.selectedSlot.store(cell);
                    if (!secondary || pad != 7u) auditionEditedSlot(p, secondary ? selectedSlot : cell);
                } else if (mode == NeonMode::HotLoop) capturePadAction(p, pad, secondary);
                else if (mode == NeonMode::Slicer) {
                    if (secondary) chopToolAction(p, pad);
                    else auditionSlice(p, selectedSlot, cell);
                } else if (secondary) {
                    queueGuiParamGesture(p, slotParamId(selectedSlot, kSlotCharacter), pad);
                    // The visual FX pad uses a momentary gesture, below.
                } else { p.selectedSlot.store(cell); auditionSlot(p, cell); }
            });
            if (fxPads)
                drag(bounds, [](CPoint) {}, [this, pad, slot = selectedSlot] {
                    queueGuiParamGesture(p, slotParamId(slot, kSlotCharacter), pad);
                    p.pendingGuiFx.store(static_cast<uint32_t>((slot << 1u) | 1u));
                    requestProcess(p);
                }, [this, slot = selectedSlot] {
                    p.pendingGuiFx.store(static_cast<uint32_t>(slot << 1u)); requestProcess(p);
                });
            else if (mode == NeonMode::Sampler || mode == NeonMode::HotCue || (capture && secondary && loaded))
                drag(bounds, [](CPoint) {}, [this, mode, secondary, pad, cell, slot = selectedSlot] {
                    if (mode == NeonMode::HotCue) {
                        if (secondary) editToolAction(p, pad); else p.selectedSlot.store(cell);
                        if (!secondary || pad != 7u) auditionEditedSlot(p, secondary ? slot : cell);
                    } else { p.selectedSlot.store(cell); auditionSlot(p, cell); }
                }, [this, mode, secondary, cell, slot = selectedSlot] {
                    p.pendingGuiReleases.fetch_or(1u << (mode == NeonMode::HotCue && secondary ? slot : cell));
                    requestProcess(p);
                });
        }
        text(editPage == 7u ? (p.fxPair.load() ? "LOOP / PARAM 3" : "LOOP / PARAM 1")
            : editPage == 4u ? "LOOP / STEP RATE" : editPage == 5u ? "LOOP / DURATION" : editPage == 6u ? "LOOP / CYCLE GROUP"
            : performance ? "LOOP / CELL GAIN" : editPage == 2u ? "LOOP / SCAN OFFSET"
            : editPage == 3u ? "LOOP / GRAIN SIZE" : "LOOP / EDIT CURSOR",
            rect(604.0, 578.0, 210.0, 20.0), style.label, kCenterText);
        if (editPage >= 4u) {
            foundation::drawHorizontalSlider(*context, rect(620.0, 630.0, 190.0, 8.0), advancedKnobValue(p, true), 625.0, 14.0, style);
            drag(rect(616.0, 620.0, 198.0, 27.0), [this](CPoint point) { setAdvancedKnob(p, true, (point.x - 620.0) / 190.0); });
        } else if (performance) sliderRow(slotParamId(selectedSlot, kSlotGain), "GAIN", 622.0, 604.0, 210.0);
        else {
            const double cursor = capture ? p.captureCursor.load()
                : editPage == 3u ? (p.grainSizes[selectedSlot].load() - 5.0) / 495.0
                : p.editPositions[selectedSlot].load();
            foundation::drawHorizontalSlider(*context, rect(616.0, 630.0, 186.0, 8.0), cursor, 627.0, 14.0, style);
            drag(rect(610.0, 620.0, 198.0, 27.0), [this, capture, editPage](CPoint point) {
                const auto value = std::clamp((point.x - 616.0) / 186.0, 0.0, 1.0);
                if (capture) p.captureCursor.store(value);
                else if (editPage == 3u) p.grainSizes[selectedSlot].store(static_cast<float>(5.0 + value * 495.0));
                else p.editPositions[selectedSlot].store(value);
                markStateDirty(p); requestProcess(p);
            });
        }
        sliderRow(kMasterGainParamId, "MASTER", 695.0, 604.0, 210.0);
        if (p.captureState.load() == Plugin::CaptureState::Recording)
            button(rect(604.0, 658.0, 210.0, 20.0), "STOP CAPTURE",
                [this] { capturePadAction(p, 1u, false); }, true);
    }

    void textureSliderRow(std::atomic<float>& target, const char* label,
        double y, float minimum, float maximum, const std::string& valueLabel,
        std::function<double(double)> displayValue = {}, std::function<double(double)> storageValue = {})
    {
        const double left = I::left;
        const double trackX = I::controlLeft;
        const double trackWidth = I::trackWidth;
        const double unit = (std::clamp(target.load(
            std::memory_order_relaxed), minimum, maximum) - minimum)
            / (maximum - minimum);
        text(label, rect(left, y - 2.0, I::labelWidth, 18.0), style.label);
        foundation::drawHorizontalSlider(*context,
            rect(trackX, y + 3.0, trackWidth, 8.0), unit, y, 14.0, style);
        text(valueLabel, rect(trackX + trackWidth + I::valueGap, y - 2.0,
                63.0, 18.0), style.value, kRightText, font);
        const bool percent = valueLabel.find('%') != std::string::npos;
        number(rect(trackX + trackWidth + I::valueGap, y - 2.0, 63.0, 18.0),
            displayValue ? displayValue(target.load()) : target.load() * (percent ? 100.0 : 1.0),
            [this, &target, minimum, maximum, percent, storageValue](double value) {
                if (!std::isfinite(value)) return;
                value = storageValue ? storageValue(value) : value / (percent ? 100.0 : 1.0);
                if (!std::isfinite(value)) return;
                target.store(static_cast<float>(std::clamp(value, static_cast<double>(minimum), static_cast<double>(maximum))));
                markStateDirty(p); requestProcess(p);
            });
        drag(rect(trackX - 4.0, y - 6.5, trackWidth + 8.0, 27.0),
            [this, &target, trackX, trackWidth, minimum, maximum](CPoint point) {
                const float unitValue = static_cast<float>(std::clamp(
                    (point.x - trackX) / trackWidth, 0.0, 1.0));
                target.store(minimum + unitValue * (maximum - minimum), std::memory_order_release);
                markStateDirty(p);
                requestProcess(p);
            });
    }

    void textureDoubleSliderRow(std::atomic<double>& target,
        const char* label, double y, double minimum, double maximum,
        const std::string& valueLabel)
    {
        const double left = I::left;
        const double trackX = I::controlLeft;
        const double trackWidth = I::trackWidth;
        const double unit = (std::clamp(target.load(
            std::memory_order_relaxed), minimum, maximum) - minimum)
            / (maximum - minimum);
        text(label, rect(left, y - 2.0, I::labelWidth, 18.0), style.label);
        foundation::drawHorizontalSlider(*context,
            rect(trackX, y + 3.0, trackWidth, 8.0), unit, y, 14.0, style);
        text(valueLabel, rect(trackX + trackWidth + I::valueGap, y - 2.0,
                63.0, 18.0), style.value, kRightText, font);
        drag(rect(trackX - 4.0, y - 6.5, trackWidth + 8.0, 27.0),
            [this, &target, trackX, trackWidth, minimum, maximum](
                CPoint point) {
                const double unitValue = std::clamp(
                    (point.x - trackX) / trackWidth, 0.0, 1.0);
                const double value = minimum + unitValue * (maximum - minimum);
                if (&target == &p.captureStart || &target == &p.captureEnd)
                    setCaptureTrim(p, &target == &p.captureEnd, value);
                else target.store(value, std::memory_order_release);
                if (&target == &p.sourceBpms[selectedSlot]
                    && p.chopModes[selectedSlot].load() == static_cast<uint8_t>(SampleNeonChopMode::BeatGrid))
                    regenerateChopLayout(p, selectedSlot);
                markStateDirty(p);
                requestProcess(p);
            });
    }

    void paintPlayToolbox()
    {
        panel(rect(kPanelLeft, 52.0, kPanelWidth, 688.0), "PLAY / SELECTED CELL");
        text(statusLabel(), rect(I::left, I::label(0), I::width, 18.0), style.value, kCenterText);
        button(inspectorButton(1, 0, 2), "LOAD CELL", [this] { chooseSample(); });
        button(inspectorButton(1, 1, 2), "EDIT CELL",
            [this] { selectSurface(p, NeonMode::HotCue, 0u); });
        playbackMenu(I::menu(2));
        sliderRow(slotParamId(selectedSlot, kSlotGain), "GAIN", I::slider(3));
        sliderRow(slotParamId(selectedSlot, kSlotTune), "TUNE", I::slider(4));
        sliderRow(slotParamId(selectedSlot, kSlotMangle), "MANGLE", I::slider(5));
        sliderRow(slotParamId(selectedSlot, kSlotPressureDepth), "PRESSURE", I::slider(6));
        characterMenu(I::menu(7));
        triggerMenu(I::menu(8));
        if (playbackChoice() == 0u) menuRow(slotParamId(selectedSlot, kSlotDirection), "DIRECTION", I::menu(9));
        else if (playbackChoice() == 5u) inspectorReadout("CLOCK", "FREE / PAD", 9);
        else clockMenu(I::menu(9));
        menuRow(slotParamId(selectedSlot, kSlotOutputBus), "OUTPUT", I::menu(10));
        const bool repeat = slotOptionEnabled(p, selectedSlot, kSlotRepeatOption);
        const bool sync = slotOptionEnabled(p, selectedSlot, kSlotSyncOption);
        if (playbackChoice() == 0u) {
            button(inspectorButton(11, 0, 2), "REPEAT",
                [this, repeat] { setSlotOption(p, selectedSlot, kSlotRepeatOption, !repeat); }, repeat);
            button(inspectorButton(11, 1, 2), "SYNC",
                [this, sync] { setSlotOption(p, selectedSlot, kSlotSyncOption, !sync); }, sync);
        } else button(inspectorButton(11), "EDIT PLAYBACK SETTINGS",
            [this] { p.editPage.store(static_cast<uint8_t>(playbackChoice() + 1u)); selectSurface(p, NeonMode::HotCue, 0u); });
        button(inspectorButton(12), "EDIT CHARACTER FX",
            [this] { p.editPage.store(7u); selectSurface(p, NeonMode::HotCue, 0u); });
        if (!p.workflowMessage.empty()) helpText(p.workflowMessage, I::statusTop);
    }

    void paintSliceToolbox(uint8_t)
    {
        panel(rect(kPanelLeft, 52.0, kPanelWidth, 688.0), "CHOP / AUDITION AND ASSIGN");
        text(statusLabel(), rect(I::left, I::label(0), I::width, 18.0), style.value, kCenterText);
        button(inspectorButton(1, 0, 2), "LOAD SOURCE", [this] { chooseSample(); });
        button(inspectorButton(1, 1, 2), "AUDITION SLICE",
            [this] { auditionSlice(p, selectedSlot, p.visibleSlice.load()); }, false, bool(asset(selectedSlot)));
        sliderRow(slotParamId(selectedSlot, kSlotStart), "START", I::slider(2));
        sliderRow(slotParamId(selectedSlot, kSlotEnd), "END", I::slider(3));
        unsigned nextRow = 5u;
        if (p.chopModes[selectedSlot].load() == static_cast<uint8_t>(SampleNeonChopMode::Transient)) {
            const std::size_t slot = selectedSlot;
            const double value = p.transientPreRollMs[slot].load();
            const auto apply = [this, slot](double next) {
                if (!std::isfinite(next)) return;
                p.transientPreRollMs[slot].store(static_cast<float>(std::round(std::clamp(next, 0.0, 50.0) * 10.0) / 10.0));
                regenerateChopLayout(p, slot);
            };
            text("PRE-ROLL", rect(I::left, I::label(4), I::labelWidth, 18.0), style.label);
            foundation::drawHorizontalSlider(*context, rect(I::controlLeft, I::row(4) - 4.0, I::trackWidth, 8.0), value / 50.0, I::slider(4), 14.0, style);
            drag(rect(I::controlLeft - 4.0, I::row(4) - 13.5, I::trackWidth + 8.0, 27.0), [apply](CPoint point) { apply((point.x - I::controlLeft) / I::trackWidth * 50.0); });
            const auto valueBounds = rect(I::controlLeft + I::trackWidth + I::valueGap, I::label(4), I::valueWidth, 18.0);
            text(format("%.1f ms", value), valueBounds, style.value, kRightText);
            number(valueBounds, value, apply);
        } else {
            const auto division = std::min<uint8_t>(4u, p.chopBeatDivisions[selectedSlot].load());
            inspectorLabel("GRID", 4);
            menu(inspectorMenu(4),
                format("%.2g BEAT", kChopBeatDivisions[division]),
                { "1/4 BEAT", "1/2 BEAT", "1 BEAT", "2 BEATS", "4 BEATS" }, division,
                [this](int choice) { setChopBeatDivision(p, selectedSlot, choice); });
            textureDoubleSliderRow(p.sourceBpms[selectedSlot], "SOURCE BPM", I::slider(5),
                40.0, 240.0, format("%.1f", p.sourceBpms[selectedSlot].load()));
            ++nextRow;
        }
        inspectorLabel("SOURCE", nextRow);
        const bool replace = p.replaceChopSource.load();
        menu(inspectorMenu(nextRow), replace ? "REPLACE WITH SLICE 1" : "KEEP SOURCE",
            { "KEEP SOURCE", "REPLACE WITH SLICE 1" }, replace ? 1 : 0,
            [this](int choice) { p.replaceChopSource.store(choice == 1); });
        button(inspectorButton(nextRow + 1u), "ASSIGN SELECTED SLICE TO EMPTY CELL",
            [this] { requestWorkflow(p, WorkflowCommand::MapSlice); }, false, bool(asset(selectedSlot)));
        button(inspectorButton(nextRow + 2u), replace ? "ASSIGN ALL / SOURCE BECOMES SLICE 1" : "ASSIGN ALL / KEEP SOURCE",
            [this] { requestWorkflow(p, WorkflowCommand::MapAll); }, false, bool(asset(selectedSlot)));
        if (!p.workflowMessage.empty()) helpText(p.workflowMessage, I::statusTop);
    }

    void paintCaptureToolbox()
    {
        panel(rect(kPanelLeft, 52.0, kPanelWidth, 688.0), "RESAMPLE / OUTPUT BUS");
        const auto state = p.captureState.load();
        const bool recording = state == Plugin::CaptureState::Recording;
        const auto layout = static_cast<SampleNeonOutputLayout>(static_cast<uint8_t>(paramValue(p, kOutputLayoutParamId)));
        const auto count = s3g::sample::sampleNeonBusCount(layout);
        std::vector<std::string> buses;
        for (unsigned bus = 0u; bus < count; ++bus)
            buses.push_back(format("BUS %u / CH %u-%u", bus + 1u, bus * s3g::sample::sampleNeonBusWidth(layout) + 1u,
                (bus + 1u) * s3g::sample::sampleNeonBusWidth(layout)));
        const auto bus = std::min<unsigned>(p.captureBus.load(), count - 1u);
        inspectorLabel("SOURCE", 0);
        menu(inspectorMenu(0), buses[bus], buses, static_cast<int>(bus),
            [this](int choice) { p.captureBus.store(static_cast<uint8_t>(choice)); }, !recording);
        text(format("MAX %.1f SEC / POST MASTER", std::min(30.0, s3g::sample::SampleNeonRecorder::kMaximumFrames / p.sampleRate)),
            rect(I::left, I::label(1), I::width, 18.0), style.value, kCenterText);
        button(inspectorButton(2, 0, 2), "RECORD",
            [this] { capturePadAction(p, 0u, false); }, recording, state == Plugin::CaptureState::Empty);
        button(inspectorButton(2, 1, 2), "STOP",
            [this] { capturePadAction(p, 1u, false); }, false, recording);
        button(inspectorButton(3, 0, 2), "AUDITION",
            [this] { capturePadAction(p, 2u, false); }, false, bool(p.captureAsset));
        button(inspectorButton(3, 1, 2), "DISCARD",
            [this] { requestWorkflow(p, WorkflowCommand::DiscardCapture); }, false, state == Plugin::CaptureState::Review);
        button(inspectorButton(4), "CROP TO SELECTION / DISCARD OUTSIDE",
            [this] { requestWorkflow(p, WorkflowCommand::CropCapture); }, false, state == Plugin::CaptureState::Review);
        textureDoubleSliderRow(p.captureStart, "START", I::slider(5), 0.0, 1.0, format("%.1f%%", p.captureStart.load() * 100.0));
        textureDoubleSliderRow(p.captureEnd, "END", I::slider(6), 0.0, 1.0, format("%.1f%%", p.captureEnd.load() * 100.0));
        std::vector<std::string> cells;
        for (unsigned cell = 0u; cell < 32u; ++cell)
            cells.push_back(format("%c%u %s", 'A' + cell / 8u, cell % 8u + 1u, asset(cell) ? "USED" : "EMPTY"));
        const unsigned target = p.captureTarget.load();
        inspectorLabel("TARGET", 7);
        menu(inspectorMenu(7), target < 32u ? cells[target] : "CHOOSE EMPTY CELL",
            cells, target < 32u ? static_cast<int>(target) : -1, [this](int choice) {
                if (!asset(static_cast<std::size_t>(choice))) p.captureTarget.store(static_cast<uint8_t>(choice));
            }, true, 4);
        button(inspectorButton(8), "ASSIGN CAPTURE TO CELL",
            [this] { requestWorkflow(p, WorkflowCommand::AssignCapture); }, false, bool(p.captureAsset) && target < 32u);
        const auto source = p.selectedSlot.load();
        const auto sourceBus = static_cast<unsigned>(paramValue(p, slotParamId(source, kSlotOutputBus)));
        if (p.publishedAssets[source].load() && sourceBus != p.captureBus.load() + 1u)
            text(format("CELL BUS %u / CAPTURE BUS %u", sourceBus, p.captureBus.load() + 1u),
                rect(I::left, I::label(10), I::width, 18.0), color(0xc28f7a), kCenterText);
        if (!recording && !p.workflowMessage.empty()) helpText(p.workflowMessage, I::statusTop);
    }

    void paintExtendedPlayback()
    {
        const auto method = playbackChoice();
        auto& values = p.techniqueParameters[selectedSlot][method - 3u];
        triggerMenu(I::menu(4));
        if (method != 5u) clockMenu(I::menu(5));
        const bool host = p.playbackClocks[selectedSlot].load() != 0u;
        if (method == 3u) {
            const int trigger = static_cast<int>(paramValue(p, slotParamId(selectedSlot, kSlotTriggerMode)));
            if (trigger == 0 || trigger == 2)
                textureSliderRow(p.shotSeconds[selectedSlot], "SHOT LEN", I::slider(6), 0.05f, 30.0f, format("%.2f s", p.shotSeconds[selectedSlot].load()));
            else text(trigger == 1 ? "DURATION: WHILE PAD IS HELD" : "DURATION: UNTIL PAD IS TAPPED AGAIN",
                rect(I::left, I::label(6), I::width, 18.0), style.value);
            if (host) beatMenu(p.grainIntervals[selectedSlot], "STEP", I::menu(7), 8.0);
            else textureSliderRow(values[0], "STEP RATE", I::slider(7), 0.0f, 1.0f, format("%.1f Hz", 1.0f + values[0].load() * 19.0f),
                [](double x) { return 1.0 + x * 19.0; }, [](double x) { return (x - 1.0) / 19.0; });
            const std::vector<std::string> orders {"FORWARD", "REVERSE", "RANDOM"};
            const int order = static_cast<int>(std::lround(values[1].load() * 2.0f));
            inspectorLabel("ORDER", 8);
            menu(inspectorMenu(8), orders[static_cast<unsigned>(order)], orders, order,
                [this](int choice) { p.techniqueParameters[selectedSlot][0][1].store(choice * 0.5f); markStateDirty(p); requestProcess(p); });
            textureSliderRow(values[2], "REPEATS", I::slider(9), 0.0f, 1.0f, format("%.0f", 1.0 + std::round(values[2].load() * 7.0)),
                [](double x) { return 1.0 + std::round(x * 7.0); }, [](double x) { return (std::round(x) - 1.0) / 7.0; });
            textureSliderRow(values[3], "CHANCE", I::slider(10), 0.0f, 1.0f, format("%.0f%%", values[3].load() * 100.0f));
        } else if (method == 4u) {
            if (host) beatMenu(p.motionRates[selectedSlot], "DURATION", I::menu(6));
            else textureSliderRow(p.motionSeconds[selectedSlot], "DURATION", I::slider(6), 0.05f, 30.0f, format("%.2f s", p.motionSeconds[selectedSlot].load()));
            sliderRow(slotParamId(selectedSlot, kSlotTune), "TUNE", I::slider(7));
            const std::vector<std::string> dirs {"FORWARD", "REVERSE"};
            const bool reverse = paramValue(p, slotParamId(selectedSlot, kSlotDirection)) == 1.0;
            inspectorLabel("DIRECTION", 8);
            menu(inspectorMenu(8), dirs[reverse ? 1u : 0u], dirs, reverse ? 1 : 0,
                [this](int choice) { queueGuiParamGesture(p, slotParamId(selectedSlot, kSlotDirection), choice); });
        } else {
            textureSliderRow(values[0], "GROUP", I::slider(5), 0.0f, 1.0f, format("%.0f cycles", 1.0 + std::round(values[0].load() * 31.0)),
                [](double x) { return 1.0 + std::round(x * 31.0); }, [](double x) { return (std::round(x) - 1.0) / 31.0; });
            textureSliderRow(values[1], "REPEATS", I::slider(6), 0.0f, 1.0f, format("%.0f", 1.0 + std::round(values[1].load() * 15.0)),
                [](double x) { return 1.0 + std::round(x * 15.0); }, [](double x) { return (std::round(x) - 1.0) / 15.0; });
            const std::vector<std::string> shapes {"REPEAT", "OMIT", "REPLACE", "ENVELOPE", "MULTIPLY", "AVERAGE", "INTERPOLATE", "FRACTAL", "HARMONIC", "GROUP REVERSE", "CYCLE REVERSE", "TELESCOPE"};
            const auto shape = static_cast<int>(std::lround(values[2].load() * 11.0f));
            inspectorLabel("SHAPE", 7);
            menu(inspectorMenu(7), shapes[static_cast<unsigned>(shape)], shapes, shape,
                [this](int choice) { p.techniqueParameters[selectedSlot][2][2].store(choice / 11.0f); markStateDirty(p); requestProcess(p); });
            textureSliderRow(values[3], "PROCESS", I::slider(8), 0.0f, 1.0f, format("%.0f%%", values[3].load() * 100.0f));
            sliderRow(slotParamId(selectedSlot, kSlotTune), "TUNE", I::slider(9));
            if (!p.controlWavesets[selectedSlot]) helpText("NO USABLE WAVEFORM CYCLES", I::statusTop);
        }
    }

    void paintToolbox()
    {
        const uint8_t mode = std::min<uint8_t>(3u,
            p.visibleMode.load(std::memory_order_relaxed));
        if (mode == static_cast<uint8_t>(NeonMode::Sampler)) { paintPlayToolbox(); return; }
        if (mode == static_cast<uint8_t>(NeonMode::HotLoop)) { paintCaptureToolbox(); return; }
        if (mode == static_cast<uint8_t>(NeonMode::Slicer)) {
            paintSliceToolbox(mode);
            return;
        }
        panel(rect(kPanelLeft, 52.0, kPanelWidth, 688.0),
            format("EDIT / CELL %c%zu",
                static_cast<char>('A' + selectedSlot / 8u),
                selectedSlot % 8u + 1u));
        const auto current = asset(selectedSlot);
        text(statusLabel(),
            rect(I::left, I::label(0), I::width, 18.0),
            current ? style.value : style.dim, kCenterText, font);
        button(inspectorButton(1, 0, 4), "LOAD",
            [this] { chooseSample(); });
        button(inspectorButton(1, 1, 4), "CLEAR",
            [this] { clearSample(p, selectedSlot); }, false,
            static_cast<bool>(current));
        button(inspectorButton(1, 2, 4), "AUDITION",
            [this] { auditionEditedSlot(p, selectedSlot); }, false,
            static_cast<bool>(current));
        button(inspectorButton(1, 3, 4), "STOP",
            [this] { p.pendingCellStops.fetch_or(1u << selectedSlot); requestProcess(p); }, false,
            static_cast<bool>(current));
        playbackMenu(I::menu(2));
        inspectorLabel("EDIT VIEW", 3);
        menu(inspectorMenu(3),
            workspaceTab == 0u ? "SOURCE / TRIM" : workspaceTab == 7u ? "CHARACTER FX" : "PLAYBACK SETTINGS",
            { "SOURCE / TRIM", "PLAYBACK SETTINGS", "CHARACTER FX" }, workspaceTab == 0u ? 0 : workspaceTab == 7u ? 2 : 1,
            [this](int choice) {
                workspaceTab = static_cast<uint8_t>(choice == 2 ? 7u : choice ? (playbackChoice() == 0u ? 1u : playbackChoice() + 1u) : 0u);
                p.editPage.store(workspaceTab);
            });

        if (workspaceTab == 7u) { paintFxSettings(); return; }
        if (workspaceTab && playbackChoice() >= 3u) { paintExtendedPlayback(); return; }

        if (workspaceTab == 0u || playbackChoice() == 0u) {
            sliderRow(slotParamId(selectedSlot, kSlotGain), "GAIN", I::slider(4));
            sliderRow(slotParamId(selectedSlot, kSlotPan), "PAN", I::slider(5));
            sliderRow(slotParamId(selectedSlot, kSlotTune), "TUNE", I::slider(6));
            sliderRow(slotParamId(selectedSlot, kSlotStart), "START", I::slider(7));
            sliderRow(slotParamId(selectedSlot, kSlotEnd), "END", I::slider(8));
            if (playbackChoice() == 0u) {
                sliderRow(slotParamId(selectedSlot, kSlotAttack), "ATTACK", I::slider(9));
                sliderRow(slotParamId(selectedSlot, kSlotRelease), "RELEASE", I::slider(10));
            } else if (playbackChoice() == 1u || playbackChoice() == 2u) techniqueEnvelopeRows(I::slider(9));
            else {
                inspectorReadout("ATTACK", "TECHNIQUE", 9);
                inspectorReadout("RELEASE", "TECHNIQUE", 10);
            }
            if (playbackChoice() != 5u) sliderRow(slotParamId(selectedSlot, kSlotFilterCutoff), "CUTOFF", I::slider(11));
            else inspectorReadout("CUTOFF", "CHARACTER FX", 11);
            sliderRow(slotParamId(selectedSlot, kSlotMangle), "MANGLE", I::slider(12));
            sliderRow(slotParamId(selectedSlot, kSlotPressureDepth), "PRESSURE", I::slider(13));
            characterMenu(I::menu(14));
            triggerMenu(I::menu(15));
            menuRow(slotParamId(selectedSlot, kSlotSourceFormat), "FORMAT", I::menu(16));
            menuRow(slotParamId(selectedSlot, kSlotOutputBus), "OUTPUT", I::menu(17));
            if (playbackChoice() == 0u) menuRow(slotParamId(selectedSlot, kSlotDirection), "DIRECTION", I::menu(18));
            else inspectorReadout("DIRECTION", "TECHNIQUE", 18);
            const bool velocity = slotOptionEnabled(p, selectedSlot, kSlotVelocityOption);
            inspectorLabel("VELOCITY", 19);
            menu(inspectorMenu(19), velocity ? "PAD VELOCITY" : "CONSTANT LEVEL",
                { "PAD VELOCITY", "CONSTANT LEVEL" }, velocity ? 0 : 1,
                [this](int choice) { setSlotOption(p, selectedSlot, kSlotVelocityOption, choice == 0); });
            button(inspectorButton(20), "NORMALIZE SOURCE / -1 dBFS",
                [this] { requestWorkflow(p, WorkflowCommand::Normalize); }, false, bool(current));
        } else {
            const bool motion = playbackChoice() == 1u;
            const bool host = p.playbackClocks[selectedSlot].load() != 0u;
            triggerMenu(I::menu(4));
            clockMenu(I::menu(5));
            const int trigger = static_cast<int>(paramValue(p, slotParamId(selectedSlot, kSlotTriggerMode)));
            if (trigger == 0 || trigger == 2) {
                textureSliderRow(p.shotSeconds[selectedSlot], "SHOT LEN", I::slider(6), 0.05f, 30.0f,
                    format("%.2f s", p.shotSeconds[selectedSlot].load()));
            } else text(trigger == 1 ? "DURATION: WHILE PAD IS HELD" : "DURATION: UNTIL PAD IS TAPPED AGAIN",
                rect(I::left, I::label(6), I::width, 18.0), style.value);
            if (motion) {
                textureDoubleSliderRow(p.editPositions[selectedSlot], "OFFSET", I::slider(7), 0.0, 1.0,
                    format("%.1f%%", p.editPositions[selectedSlot].load() * 100.0));
                const auto pathIndex = std::min<uint8_t>(3u, p.motionPaths[selectedSlot].load());
                const std::vector<std::string> paths { "FORWARD", "REVERSE", "BOUNCE", "WANDER" };
                inspectorLabel("PATH", 8);
                menu(inspectorMenu(8), paths[pathIndex], paths, pathIndex,
                    [this](int choice) { p.motionPaths[selectedSlot].store(static_cast<uint8_t>(choice)); markStateDirty(p); requestProcess(p); });
                if (host) beatMenu(p.motionRates[selectedSlot], "CYCLE", I::menu(9));
                else textureSliderRow(p.motionSeconds[selectedSlot], "CYCLE", I::slider(9), 0.05f, 30.0f,
                    format("%.2f s", p.motionSeconds[selectedSlot].load()));
                techniqueEnvelopeRows(I::slider(10));
            } else {
                textureDoubleSliderRow(p.editPositions[selectedSlot], "POSITION", I::slider(7), 0.0, 1.0,
                    format("%.1f%%", p.editPositions[selectedSlot].load() * 100.0));
                if (host) beatMenu(p.grainIntervals[selectedSlot], "INTERVAL", I::menu(8), 8.0);
                else textureSliderRow(p.grainDensities[selectedSlot], "DENSITY", I::slider(8), 1.0f, 80.0f,
                    format("%.1f Hz", p.grainDensities[selectedSlot].load()));
                textureSliderRow(p.grainSizes[selectedSlot], "SIZE", I::slider(9), 5.0f, 500.0f,
                    format("%.0f ms", p.grainSizes[selectedSlot].load()));
                textureSliderRow(p.grainSprays[selectedSlot], "SPRAY", I::slider(10), 0.0f, 1.0f,
                    format("%.0f%%", p.grainSprays[selectedSlot].load() * 100.0f));
                textureSliderRow(p.grainPitchSprays[selectedSlot], "PITCH", I::slider(11), 0.0f, 24.0f,
                    format("%.1f st", p.grainPitchSprays[selectedSlot].load()));
                textureSliderRow(p.grainReverseChances[selectedSlot], "REVERSE", I::slider(12), 0.0f, 1.0f,
                    format("%.0f%%", p.grainReverseChances[selectedSlot].load() * 100.0f));
                techniqueEnvelopeRows(I::slider(13));
            }
        }
        if (fileMessage.empty() && !p.workflowMessage.empty()) helpText(p.workflowMessage, I::statusTop);
        if (!fileMessage.empty())
            text(fileMessage,
                rect(I::left, I::statusTop, I::width, 18.0),
                color(0x77b9a8), kCenterText, font);
    }

    void paintController()
    {
        paintWorkspace();
        paintMiniNeon();
    }

    void paintSelectedSlot()
    {
        paintToolbox();
    }

    double normalized(clap_id id, double value) const
    {
        std::size_t index = 0u;
        std::size_t slot = 0u;
        SlotParamOffset offset = kSlotGain;
        const ParamDef* definition = nullptr;
        if (!parameterLocation(id, index, definition, &slot, &offset))
            return 0.0;
        if (id >= kSlotParamBase && offset == kSlotFilterCutoff)
            return std::log(value / definition->minimum)
                / std::log(definition->maximum / definition->minimum);
        return (value - definition->minimum)
            / (definition->maximum - definition->minimum);
    }

    double fromNormalized(clap_id id, double unit) const
    {
        std::size_t index = 0u;
        std::size_t slot = 0u;
        SlotParamOffset offset = kSlotGain;
        const ParamDef* definition = nullptr;
        if (!parameterLocation(id, index, definition, &slot, &offset))
            return 0.0;
        unit = std::clamp(unit, 0.0, 1.0);
        if (id >= kSlotParamBase && offset == kSlotFilterCutoff)
            return definition->minimum * std::pow(
                definition->maximum / definition->minimum, unit);
        const double value = definition->minimum
            + unit * (definition->maximum - definition->minimum);
        return definition->stepped ? std::round(value) : value;
    }

    void sliderRow(clap_id id, const char* label, double y,
        double left = kPanelLeft + 12.0, double totalWidth = 278.0)
    {
        const auto current = asset(selectedSlot);
        const bool enabled = id != slotParamId(selectedSlot, kSlotPan)
            || !current || current->channelCount <= 2u;
        const bool inspector = left == I::left;
        const double labelWidth = inspector ? 82.0 : 72.0;
        const double valueWidth = inspector ? I::valueWidth : 58.0;
        const double trackX = left + labelWidth;
        const double trackWidth = totalWidth - labelWidth - valueWidth - 8.0;
        const double valueLeft = trackX + trackWidth + (inspector ? I::valueGap : 7.0);
        const CRect track = rect(trackX, y + 3.0, trackWidth, 8.0);
        text(enabled ? label : "PAN N/A", rect(left, y - 2.0, inspector ? I::labelWidth : labelWidth - 5.0, 18.0),
            style.label);
        foundation::drawHorizontalSlider(*context, track,
            normalized(id, paramValue(p, id)), y, 14.0, style);
        char valueText[64] {};
        (void)paramsValueToText(&p.plugin, id, paramValue(p, id),
            valueText, sizeof(valueText));
        text(valueText, rect(valueLeft, y - 2.0,
            valueWidth, 18.0), style.value, kRightText, font);
        if (!enabled) return;
        drag(rect(trackX - 4.0, y - (inspector ? 6.5 : 7.0), trackWidth + 8.0, 27.0),
            [this, id, trackX, trackWidth](CPoint point) {
                queueGuiParamValue(p, id, fromNormalized(id,
                    (point.x - trackX) / trackWidth));
            }, [this, id] { queueGuiParamBegin(p, id); },
            [this, id] {
                queueGuiParamEnd(p, id);
                std::size_t stored = 0u;
                std::size_t slot = 0u;
                SlotParamOffset offset = kSlotGain;
                const ParamDef* definition = nullptr;
                if (parameterLocation(id, stored, definition, &slot, &offset)
                    && (offset == kSlotStart || offset == kSlotEnd)) {
                    const uint8_t mode = p.chopModes[slot].load(
                        std::memory_order_relaxed);
                    if (mode == static_cast<uint8_t>(
                            SampleNeonChopMode::Transient)
                        || mode == static_cast<uint8_t>(
                            SampleNeonChopMode::BeatGrid))
                        regenerateChopLayout(p, slot);
                }
            });
    }

    void menuRow(clap_id id, const char* label, double y)
    {
        std::size_t index = 0u;
        const ParamDef* definition = nullptr;
        if (!parameterLocation(id, index, definition)) return;
        text(label, rect(I::left, y + 3.5, I::labelWidth, 18.0),
            style.label);
        char current[64] {};
        (void)paramsValueToText(&p.plugin, id, paramValue(p, id),
            current, sizeof(current));
        std::vector<std::string> items;
        const int first = static_cast<int>(std::lround(definition->minimum));
        const bool outputBus = id == slotParamId(selectedSlot, kSlotOutputBus);
        const auto layout = static_cast<SampleNeonOutputLayout>(
            static_cast<uint8_t>(paramValue(p, kOutputLayoutParamId)));
        const int last = outputBus ? static_cast<int>(s3g::sample::sampleNeonBusCount(layout))
            : static_cast<int>(std::lround(definition->maximum));
        for (int choice = first; choice <= last; ++choice) {
            char labelText[64] {};
            (void)paramsValueToText(&p.plugin, id,
                static_cast<double>(choice), labelText, sizeof(labelText));
            items.emplace_back(outputBus ? format("BUS %d / CH %u-%u", choice,
                (static_cast<unsigned>(choice) - 1u) * s3g::sample::sampleNeonBusWidth(layout) + 1u,
                static_cast<unsigned>(choice) * s3g::sample::sampleNeonBusWidth(layout)) : upper(labelText));
        }
        const int selected = static_cast<int>(paramValue(p, id)) - first;
        const std::string caption = outputBus && selected >= 0 && selected < static_cast<int>(items.size())
            ? items[static_cast<std::size_t>(selected)] : upper(current);
        menu(rect(I::controlLeft, y, I::controlWidth, 25.0), caption,
            std::move(items),
            static_cast<int>(std::lround(paramValue(p, id))) - first,
            [this, id, first](int choice) {
                queueGuiParamGesture(p, id,
                    static_cast<double>(first + choice));
            }, true, 1, kCenterText);
    }

    void chooseSample()
    {
#if defined(S3G_SAMPLE_FILE_WORKER)
        foundation::FileDialogOptions options;
        options.title = "Load Sample Neon 32 cell";
        const std::string selected = foundation::runFileDialog(
            getFrame(), options);
        if (!selected.empty()) queueSampleLoad(p, selectedSlot, selected);
#endif
    }

    void chooseSet(bool save)
    {
        foundation::FileDialogOptions options;
        options.save = save;
        options.title = save ? "Save Sample Neon 32 set"
                             : "Load Sample Neon 32 set";
        options.extension = "s3gneon";
        options.extensionDescription = "s3g Sample Neon 32 Set";
        options.defaultSaveName = "Sample Neon 32.s3gneon";
        options.initialDirectory = foundation::presetDirectory(
            "Sample Neon");
        const std::string selected = foundation::runFileDialog(
            getFrame(), options);
        if (selected.empty()) return;
        fileMessage = save
            ? (s3g::clap_gui::portable::saveStateFile(
                &p.plugin, stateExtension, selected.c_str())
                ? "SET SAVED" : "SET SAVE FAILED")
            : (s3g::clap_gui::portable::loadStateFile(
                &p.plugin, stateExtension, selected.c_str())
                ? "SET LOADED" : "SET LOAD FAILED");
    }

    Plugin& p;
    std::size_t selectedSlot = 0u;
    uint8_t workspaceTab = 0u;
    std::string fileMessage;
};

} // namespace sample_neon_canvas

s3g::portable_gui::foundation::ContentView* makePortableCanvas(Plugin& p)
{
    return new sample_neon_canvas::Editor(p);
}
