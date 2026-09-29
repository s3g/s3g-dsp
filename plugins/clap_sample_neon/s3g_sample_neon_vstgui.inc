namespace sample_neon_canvas {

using namespace s3g::portable_gui::canvas;
namespace foundation = s3g::portable_gui::foundation;
using I = s3g::sample_neon_gui::InspectorLayout;
using E = s3g::sample_neon_gui::PlaybackLayout;
using P = s3g::sample_neon_gui::PadLayout;
using M = s3g::sample_neon_gui::MiniLayout;
using W = s3g::sample_neon_gui::WaveLayout;
using C = s3g::sample_neon_gui::CanvasLayout;
using WaveDisplay = s3g::sample::SampleNeonWaveformDisplay;

class Editor final : public View {
public:
    explicit Editor(Plugin& instance)
        : View(kGuiWidth, kGuiHeight, true), p(instance)
    {
        setWantsFocus(true);
    }
    ~Editor() override { p.fillGuiHeld.store(false); p.pendingStackGuiReleases.store(255); requestProcess(p); }

    void onMouseDownEvent(MouseDownEvent& event) override {
        if (p.resetAllPhase.load()) { event.consumed = true; return; }
        pathDragPoint = -1;
        pathPointLimit = false;
        // Canvas actions claim shortcut focus; native text entry subsequently
        // takes its own focus and retains its own undo/copy/paste behavior.
        if (getFrame()) getFrame()->setFocusView(this);
        View::onMouseDownEvent(event);
    }
    void onKeyboardEvent(KeyboardEvent& event) override {
        if (resetConfirm || p.resetAllPhase.load() || !getFrame() || getFrame()->getFocusView() != this || event.type != EventType::KeyDown
            || !(event.modifiers.has(ModifierKey::Control) || event.modifiers.has(ModifierKey::Super))
            || event.modifiers.has(ModifierKey::Alt)) return;
        const auto key = event.character;
        if (key == 'c' || key == 'C') { copyCell(p, p.selectedSlot.load()); event.consumed = true; }
        else if (key == 'v' || key == 'V') { requestPaste(p.selectedSlot.load()); event.consumed = true; }
        else if (key == 'z' || key == 'Z') {
            requestHistoryRestore(p, event.modifiers.has(ModifierKey::Shift)); event.consumed = true;
        } else if (key == 'y' || key == 'Y') { requestHistoryRestore(p, true); event.consumed = true; }
        if (event.consumed) invalid();
    }
    bool pointerDown(MouseDownEvent& event) override {
        if (!resetConfirm && cutupsPatternVisible() && event.buttonState.isLeft()
            && foundation::contains(stackPathRect(),event.mousePosition)) {
            cutPatternDrag = true; cutPatternSourceDrag = event.modifiers.has(ModifierKey::Alt);
            cutPatternSlot = selectedSlot; editCutPatternAt(event.mousePosition); return true;
        }
        if (!resetConfirm && stackPathVisible() && foundation::contains(stackPathRect(), event.mousePosition)) {
            const bool remove = event.buttonState.isRight()
                || (event.buttonState.isLeft() && event.modifiers.has(ModifierKey::Control));
            if (remove || event.buttonState.isLeft()) {
                editStackPathAt(event.mousePosition, remove); return true;
            }
        }
        if (!resetConfirm && p.visibleMode.load() == static_cast<uint8_t>(NeonMode::HotCue) && !p.visibleLayer.load()
            && (event.modifiers.has(ModifierKey::Shift) || event.buttonState.isRight())) {
            for (unsigned pad = 0; pad < 8; ++pad) if (foundation::contains(padRect(pad), event.mousePosition)) {
                const auto layer = p.stackBank.load() * 8u + pad;
                if (layer < stackCount(p, selectedSlot)) showLayer(p, selectedSlot, layer);
                return true;
            }
        }
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
    void pointerMove(MouseMoveEvent& event) override {
        if (cutPatternDrag) {
            if (cutPatternSlot != selectedSlot || !cutupsPatternVisible()) { cutPatternDrag = false; return; }
            editCutPatternAt(event.mousePosition); event.consumed = true; return;
        }
        if (pathDragPoint < 0) return;
        if (pathDragSlot != selectedSlot || !stackPathVisible()) { pathDragPoint = -1; return; }
        auto f = familySnapshot(); const auto area = stackPathRect();
        if (s3g::sample::neonMoveStackPoint(f, static_cast<unsigned>(pathDragPoint),
            static_cast<float>((event.mousePosition.x - area.left) / area.getWidth()),
            static_cast<float>((event.mousePosition.y - area.top) / area.getHeight()))) storeStackPath(f);
        invalid(); event.consumed = true;
    }
    void pointerUp(MouseUpEvent&) override { pathDragPoint = -1; cutPatternDrag = false; }

    void service() override
    {
        serviceWorkflow(p);
#if defined(S3G_SAMPLE_FILE_WORKER)
        serviceLoads(p);
#endif
        serviceWavesets(p);
        serviceMosaic(p);
        serviceStorage(p);
        workspaceTab = std::min<uint8_t>(12u, effectiveEditPage(p));
    }

    void paint() override
    {
        // A pad click and page change may precede the next service timer.
        // Draw and bind controls to the selected cell on this very paint.
        selectedSlot = std::min<std::size_t>(p.selectedSlot.load(
            std::memory_order_relaxed), s3g::sample::kSampleNeonSlotCount - 1u);
        if (visualSlot != selectedSlot) { visual = {}; visualSlot = selectedSlot; }
        (void)p.playbackVisuals[selectedSlot].read(visual);
        workspaceTab = std::min<uint8_t>(12u, effectiveEditPage(p));
        footerMessage.clear(); footerColor = style.dim;
        paintHeader();
        paintController();
        paintSelectedSlot();
        text("UNDO: " + historyLabel(p, false) + "    |    REDO: " + historyLabel(p, true),
            rect(C::statusLeft, C::statusTop - 24, C::statusWidth, 18), style.dim);
        if (!footerMessage.empty()) text(footerMessage,
            rect(C::statusLeft, C::statusTop, C::statusWidth, 18), footerColor);
        if (resetConfirm) {
            // A deliberate two-step action; the waveform itself stays clear.
            hit(rect(0.0, 0.0, kGuiWidth, kGuiHeight), [] {});
            fill(rect(20.0, 52.0, 1128.0, 62.0), style.strip);
            stroke(rect(20.0, 52.0, 1128.0, 62.0), style.text);
            text("CLEAR ALL 32 CELLS + CAPTURE? UNDO CAN RESTORE THIS DURING THIS SESSION.",
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
        if (paths.empty() || p.resetAllPhase.load() || p.visibleMode.load() == static_cast<uint8_t>(NeonMode::HotLoop)) return false;
        int target = slotAt(point);
        if (target < 0) target = static_cast<int>(selectedSlot);
        if ((effectiveEditPage(p) == 8u || effectiveEditPage(p) == 0u)
            && familyPage != 3 && !p.fillPage.load()
            && (p.visibleMode.load() == static_cast<uint8_t>(NeonMode::HotCue)
                || p.visibleMode.load() == static_cast<uint8_t>(NeonMode::Sampler))) {
#if defined(S3G_SAMPLE_FILE_WORKER)
            unsigned layer = stackCount(p, static_cast<std::size_t>(target));
            if (paths.size() > 32u - layer) { p.workflowMessage = "STACK FULL; DROP NEEDS MORE FREE LAYERS"; return false; }
            for (const auto& path : paths) queueSampleLoad(p, static_cast<std::size_t>(target), path, true, layer++);
            return true;
#endif
        }
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
    template<class L = I> static CRect inspectorButton(unsigned row, unsigned column = 0u, unsigned count = 1u) {
        return rect(L::columnLeft(column, count), L::button(row), L::columnWidth(count), L::buttonHeight);
    }
    template<class L = I> static CRect inspectorMenu(unsigned row) {
        return rect(L::controlLeft, L::menu(row), L::controlWidth, 25.0);
    }
    template<class L = I> void inspectorLabel(const char* label, unsigned row) {
        text(label, rect(L::left, L::label(row), L::labelWidth, 18.0), style.label);
    }
    template<class L = I> void inspectorReadout(const char* label, const char* value, unsigned row) {
        inspectorLabel<L>(label, row);
        text(value, rect(L::controlLeft, L::label(row), L::controlWidth, 18.0), style.dim, kCenterText);
    }
    bool resetConfirm = false;
    int cellAt(CPoint point) const {
        const auto mode = static_cast<NeonMode>(p.visibleMode.load());
        const auto bank = p.visibleBank.load();
        if (mode == NeonMode::HotLoop) {
            if (!p.visibleLayer.load()) return -1;
        } else if (mode == NeonMode::Slicer || mode == NeonMode::HotCue || p.visibleLayer.load()) return -1;
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
        textureSliderRow(p.techniqueAttackSeconds[selectedSlot], "ATTACK", y, 0.001f, 10.0f, ControlDefaults::techniqueEnvelope,
            format("%.3f s", p.techniqueAttackSeconds[selectedSlot].load()));
        textureSliderRow(p.techniqueReleaseSeconds[selectedSlot], "RELEASE", y + I::rowPitch, 0.001f, 10.0f, ControlDefaults::techniqueEnvelope,
            format("%.3f s", p.techniqueReleaseSeconds[selectedSlot].load()));
    }
    std::array<s3g::sample::SampleNeonWaveformViewport, 33u> waveViews {};
    std::array<const SampleAsset*, 33u> waveAssets {};
    s3g::sample::NeonVisualSnapshot visual {};
    std::size_t visualSlot = 32;
    CRect waveformRect() const { return rect(W::left, W::top, W::width,
        W::height()); }
    CRect overviewRect() const { return rect(W::overviewLeft, W::overviewTop(), W::overviewWidth, W::overviewHeight); }
    CRect stackPathRect() const { return rect(W::pathLeft, W::pathTop, W::pathWidth, W::pathHeight); }
    bool stackPathVisible() const {
        const auto mode = static_cast<NeonMode>(p.visibleMode.load());
        return familyPage == 3 && workspaceTab == 8 && !p.fillPage.load()
            && (mode == NeonMode::HotCue || mode == NeonMode::Sampler);
    }
    // Use the shared s3g popup, with centered control labels everywhere.
    void menu(CRect bounds, const std::string& value,
        std::vector<std::string> items, int selected,
        std::function<void(int)> choose, bool enabled = true,
        int columns = 1, CHoriTxtAlign alignment = kCenterText, std::vector<bool> selectable = {})
    {
        // Preserve caller width/inset; every menu uses the compact family height.
        const double top = bounds.getCenter().y - I::menuHeight * 0.5;
        bounds = rect(bounds.left, top, bounds.getWidth(), I::menuHeight);
        auto palette = style;
        if (!enabled) palette.value = color(0x686868);
        foundation::drawMenuBox(*context, bounds, value, font, palette, alignment);
        hit(bounds, [this, bounds, items = std::move(items), selected,
            choose = std::move(choose), columns, selectable = std::move(selectable)] {
            openPopup(bounds, items, selected, choose, columns, {}, I::popupRowHeight, selectable);
        }, enabled);
    }

    std::string footerMessage;
    CColor footerColor {};
    void helpText(const std::string& message, double)
    {
        // One bounded operation-status line; never wrap below the canvas.
        footerMessage = message;
    }

    bool routeValid(const SampleAsset* current) const
    {
        return current && s3g::sample::sampleNeonRouteCompatible(
            current->channelCount, static_cast<SampleNeonSourceFormat>(
                static_cast<uint8_t>(paramValue(p, slotParamId(selectedSlot, kSlotSourceFormat)))),
            static_cast<SampleNeonOutputLayout>(static_cast<uint8_t>(paramValue(p, kOutputLayoutParamId))),
            static_cast<uint32_t>(paramValue(p, slotParamId(selectedSlot, kSlotOutputBus))) - 1u,
            familySnapshot()[NeonFamily::RoutingMode] != 0);
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
        const bool distribute = !ambi && familySnapshot()[NeonFamily::RoutingMode] != 0;
        const bool independent = !ambi && playbackChoice() == 2 && current->channelCount == 2u
            && familySnapshot()[NeonFamily::GrainStereoLink] != 0;
        return format("%u CH %s / BUS %u: CH %u-%u / %s / %s",
            current->channelCount, ambi ? "ACN/SN3D" : "DISCRETE", bus,
            (bus - 1u) * width + 1u, bus * width, distribute ? "DISTRIBUTE" : "PRESERVE FIELD",
            independent ? "STEREO INDEPENDENT" : "LINKED");
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
    static constexpr double kPanelLeft = I::left - 16;
    static constexpr double kPanelWidth = I::width + 32;

    CRect padRect(std::size_t pad) const
    {
        return rect(M::padX(pad), M::padY(pad), M::padWidth, M::padHeight);
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
        if (foundation::contains(rect(W::left, 82, W::width, W::bottom() - 82), point))
            return static_cast<int>(selectedSlot);
        if (p.visibleMode.load(std::memory_order_relaxed) == static_cast<uint8_t>(NeonMode::Slicer)
            || p.visibleMode.load() == static_cast<uint8_t>(NeonMode::HotLoop)
            || p.visibleMode.load() == static_cast<uint8_t>(NeonMode::HotCue)) return -1;
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
            rect(20.0, C::titleTop, 300.0, C::titleHeight), titleFont);
        text(foundation::peakDbText(p.outputPeak.load(std::memory_order_relaxed)),
            rect(C::width - C::statusRightInset - 116, C::titleTop, 116, C::titleHeight), style.value, kRightText);
        const auto layout = static_cast<int>(paramValue(p, kOutputLayoutParamId));
        menu(rect(340.0, C::titleTop, 180.0, C::titleHeight), kOutputLayoutNames[static_cast<std::size_t>(layout)],
            std::vector<std::string>(kOutputLayoutNames.begin(), kOutputLayoutNames.end()),
            layout, [this](int choice) {
                queueGuiParamGesture(p, kOutputLayoutParamId, choice);
            });
        menu(rect(535.0, C::titleTop, 106.0, C::titleHeight), s3g::sample_storage::storageModeName(p.storageMode),
            {"PROJECT", "LINK", "EMBED"}, static_cast<int>(p.storageMode),
            [this](int choice) { chooseStorage(p, static_cast<StorageMode>(choice)); serviceStorage(p); });
        button(rect(651.0, C::titleTop, 95.0, C::titleHeight), "RESET ALL", [this] { resetConfirm = true; });
        const bool owns = paramValue(p, kNeonActiveParamId) >= 0.5;
        std::string ownerLabel = owns ? "NEON OWNER: ON" : "NEON OWNER: OFF";
#if defined(__APPLE__)
        if (owns) {
            switch ((p.visibleUnit.load() ? p.secondNeonOutput : p.directNeonOutput).status()) {
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
        if (owns && p.addressedSurfaces.load()) ownerLabel.insert(4u, p.visibleUnit.load() ? " U2" : " U1");
#endif
        button(rect(760.0, C::titleTop, 155.0, C::titleHeight),
            ownerLabel,
            [this, owns] {
                queueGuiParamGesture(p, kNeonActiveParamId,
                    owns ? 0.0 : 1.0);
                p.sendNeonInitialization.store(true,
                    std::memory_order_release);
                p.ledFeedbackDirty.store(true, std::memory_order_release);
            }, owns);
        button(rect(924.0, C::titleTop, 108.0, C::titleHeight), "LOAD SET",
            [this] { chooseSet(false); });
        button(rect(1040.0, C::titleTop, 108.0, C::titleHeight), "SAVE SET",
            [this] { chooseSet(true); });
        button(rect(1156.0, C::titleTop, 88.0, C::titleHeight), "UNDO",
            [this] { requestHistoryRestore(p, false); }, false, historyAvailable(p, false));
        button(rect(1252.0, C::titleTop, 88.0, C::titleHeight), "REDO",
            [this] { requestHistoryRestore(p, true); }, false, historyAvailable(p, true));
    }

    void selectBank(uint8_t bank) { selectSurfaceBank(p, bank); }
    void selectMode(uint8_t mode)
    {
        if (mode > 3u) return;
        // Workspace navigation always selects primary pads. SECOND is explicit.
        selectSurface(p, static_cast<NeonMode>(mode), 0u);
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

#include "s3g_sample_neon_playback_visual.inc"

    void paintWorkspace()
    {
        const auto mode = static_cast<NeonMode>(p.visibleMode.load());
        const bool chop = mode == NeonMode::Slicer;
        const bool capture = mode == NeonMode::HotLoop;
        const bool recording = capture && p.captureState.load() == Plugin::CaptureState::Recording;
        const unsigned layers = stackCount(p, selectedSlot);
        const int playingLayer = p.stackWaveformLayers[selectedSlot].load();
        const bool scanning = playingLayer == -2;
        const bool canFollow = !capture && !chop && layers > 1u;
        const bool laneView = canFollow && p.laneWaveView.load() && p.followStackWave.load();
        const bool follow = canFollow && p.followStackWave.load() && (scanning || (playingLayer >= 0 && playingLayer < static_cast<int>(layers)));
        const bool editable = !follow && !laneView;
        const auto current = capture ? p.captureAsset : asset(selectedSlot);
        const unsigned layer = static_cast<unsigned>(std::clamp(playingLayer, 0, 31));
        const auto blend = scanning ? s3g::sample::neonStackBlend(p.stackPositions[selectedSlot].load(), layers)
            : s3g::sample::NeonStackBlend {layer, layer, 0.0f};
        const auto& a = p.sources[selectedSlot][blend.first];
        const auto& b = p.sources[selectedSlot][blend.second];
        const char* names[] { "PLAY", "CHOP", "PLAY", "RESAMPLE" };
        panel(rect(W::panelLeft, C::panelTop, W::panelWidth, W::bottom() - C::panelTop),
            format("%s / CELL %c%zu / %s", names[static_cast<unsigned>(mode)],
                static_cast<char>('A' + selectedSlot / 8u), selectedSlot % 8u + 1u,
                capture ? "OUTPUT BUS CAPTURE" : laneView ? format("STACK LANES %02u-%02u / %02u",
                    s3g::sample::neonLaneViewFirst(layers,p.stackPositions[selectedSlot].load())+1u,
                    s3g::sample::neonLaneViewFirst(layers,p.stackPositions[selectedSlot].load())+std::min(8u,layers),layers).c_str() : follow ? (scanning
                    ? format("STACK %02u > %02u / %.0f%%", blend.first + 1u, blend.second + 1u, blend.mix * 100.0f)
                    : format("PLAY LAYER %02u / %02u", blend.first + 1u, layers)).c_str()
                    : format("EDIT LAYER %02u / %02u", p.selectedLayers[selectedSlot].load() + 1u,
                    std::max(1u, stackCount(p, selectedSlot))).c_str()));
        const bool ambi = capture
            ? p.captureLayout.load() >= static_cast<uint8_t>(SampleNeonOutputLayout::Ambisonic1)
            : paramValue(p, slotParamId(selectedSlot, kSlotSourceFormat)) >= 0.5;
        const auto display = p.waveformDisplay.load();
        const std::vector<std::string> waveChoices {
            "COMBINED PEAKS", ambi ? "ACN 0" : "CHANNEL 1", "ALL CHANNELS" };
        text("WAVE", rect(W::left + W::width - 207, W::viewCenter - 9, 38.0, 18.0), style.label);
        menu(rect(W::left + W::width - 164, W::viewTop, 164.0, W::controlHeight), laneView ? "STACK PEAKS" : waveChoices[static_cast<unsigned>(display)],
            waveChoices, static_cast<int>(display), [this](int choice) {
                // Like zoom, a view choice must not alter any sound/settings.
                // Stored on the instance so closing/reopening the GUI keeps it.
                p.waveformDisplay.store(static_cast<WaveDisplay>(choice));
            }, !laneView);
        const double start = follow || laneView ? 0.0 : capture ? p.captureStart.load()
            : paramValue(p, slotParamId(selectedSlot, kSlotStart));
        const double end = follow || laneView ? 1.0 : capture ? p.captureEnd.load()
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
        if (canFollow) menu(rect(340.0, W::viewTop, 164.0, W::controlHeight),
            laneView ? "STACK LANES" : p.followStackWave.load() ? "FOLLOW STACK" : "EDIT LAYER",
            {"STACK LANES", "FOLLOW STACK", "EDIT LAYER"}, laneView ? 0 : p.followStackWave.load() ? 1 : 2,
            [this](int choice) { p.laneWaveView.store(choice == 0); p.followStackWave.store(choice != 2); });
        const auto drawSource = [&](CRect bounds, double left, double right, int channel = -1) {
            if (!follow) { drawWaveTrace(bounds, current.get(), style.fill, true, left, right, channel); return; }
            const auto drawLayer = [&](const LayerSource& source, unsigned layer, float weight, bool clear) {
                const double lo = layer == p.selectedLayers[selectedSlot].load()
                    ? paramValue(p, slotParamId(selectedSlot, kSlotStart)) : source.start;
                const double hi = layer == p.selectedLayers[selectedSlot].load()
                    ? paramValue(p, slotParamId(selectedSlot, kSlotEnd)) : source.end;
                auto shade = style.fill; shade.alpha = static_cast<uint8_t>(std::lround(weight * 255.0f));
                drawWaveTrace(bounds, source.asset.get(), shade, clear, lo + (hi - lo) * left, lo + (hi - lo) * right, channel);
            };
            drawLayer(a, blend.first, blend.first == blend.second ? 1.0f : 1.0f - blend.mix, true);
            if (blend.second != blend.first) drawLayer(b, blend.second, blend.mix, false);
        };
        if (chop) {
            const unsigned chopMode = p.chopModes[selectedSlot].load();
            const std::vector<std::string> choices { "LIVE MARKERS", "TRANSIENTS", "EQUAL", "BEAT GRID" };
            menu(rect(W::left, W::actionTop, 142.0, W::controlHeight), choices[chopMode], choices,
                static_cast<int>(chopMode), [this](int choice) { setChopMode(p, selectedSlot, static_cast<uint8_t>(choice)); });
            std::vector<std::string> counts;
            for (unsigned n = 1u; n <= 32u; ++n) counts.push_back(format("%u SLICES", n));
            const auto count = sliceCount(p, selectedSlot);
            menu(rect(188.0, W::actionTop, 110.0, W::controlHeight), format("%u SLICES", count), counts, count - 1u,
                [this](int choice) { setSliceCount(p, selectedSlot, choice + 1); }, true, 4);
            button(rect(308.0, W::actionTop, 92.0, W::controlHeight), "ADD MARK", [this] { chopToolAction(p, 0u); }, false, bool(current));
            button(rect(410.0, W::actionTop, 92.0, W::controlHeight), "DELETE", [this] { chopToolAction(p, 1u); }, false, bool(current));
            text(format("SLICE %02u / %02u", p.visibleSlice.load() + 1u, count),
                rect(W::left, W::viewCenter - 9, 168.0, 18.0), style.value);
        } else if (capture) {
            const auto state = p.captureState.load();
            button(rect(W::left, W::actionTop, 86.0, W::controlHeight), "RECORD", [this] { capturePadAction(p, 0u, false); },
                state == Plugin::CaptureState::Recording, state == Plugin::CaptureState::Empty);
            button(rect(130.0, W::actionTop, 86.0, W::controlHeight), "STOP", [this] { capturePadAction(p, 1u, false); },
                false, state == Plugin::CaptureState::Recording);
            button(rect(224.0, W::actionTop, 86.0, W::controlHeight), "AUDITION", [this] { capturePadAction(p, 2u, false); },
                false, bool(current));
            button(rect(320.0, W::actionTop, 132.0, W::controlHeight), "ASSIGN TO CELL",
                [this] { requestWorkflow(p, WorkflowCommand::AssignCapture); }, false, bool(current));
            text(format("%.2f SEC / %s", p.captureFrames.load() / (current ? current->sampleRate : p.sampleRate),
                state == Plugin::CaptureState::Recording ? "RECORDING" : current ? "REVIEW" : "READY"),
                rect(W::left, W::viewCenter - 9, 340.0, 18.0), style.value);
        } else {
            button(rect(W::left, W::actionTop, 122.0, W::controlHeight), "CURSOR > START",
                [this, edit] { queueGuiParamGesture(p, slotParamId(selectedSlot, kSlotStart), edit); }, false, bool(current) && editable);
            button(rect(166.0, W::actionTop, 122.0, W::controlHeight), "CURSOR > END",
                [this, edit] { queueGuiParamGesture(p, slotParamId(selectedSlot, kSlotEnd), edit); }, false, bool(current) && editable);
        }
        const bool zero = capture ? p.captureZeroCross.load()
            : paramValue(p, slotParamId(selectedSlot, kSlotZeroCross)) >= 0.5;
        if (editable) button(rect(W::left + W::width - 118, W::actionTop, 118.0, W::controlHeight),
            zero ? "ZERO CROSS ON" : "ZERO CROSS OFF", [this, capture, zero] {
                if (capture) {
                    p.captureZeroCross.store(!zero);
                    if (!zero) { setCaptureTrim(p, false, p.captureStart.load()); setCaptureTrim(p, true, p.captureEnd.load()); }
                } else queueGuiParamGesture(p, slotParamId(selectedSlot, kSlotZeroCross), zero ? 0.0 : 1.0);
                markStateDirty(p);
            }, zero);
        if (laneView) drawLaneStack(wave, layers, viewStart, viewEnd);
        else if (recording) drawRecordingWave(wave, display);
        else {
            const unsigned rows = s3g::sample::sampleNeonWaveformRows(display,
                follow && a.asset ? a.asset->channelCount : follow && b.asset ? b.asset->channelCount : current ? current->channelCount : 1u);
            const double height = wave.getHeight() / rows;
            for (unsigned rowIndex = 0u; rowIndex < rows; ++rowIndex) {
                const auto row = rect(wave.left, wave.top + rowIndex * height, wave.getWidth(), height);
                const int channel = s3g::sample::sampleNeonWaveformChannel(display, rowIndex);
                drawSource(row, viewStart, viewEnd, channel);
                if ((current || follow) && channel >= 0) waveChannelLabel(row, static_cast<unsigned>(channel), ambi);
            }
        }
        if (!current && !recording && !follow) text(capture ? "NO CAPTURE" : "NO SAMPLE",
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
        for (unsigned handle = 0u; !laneView && handle < 2u; ++handle) {
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
        if (!capture && !laneView) {
            if (!chop && playbackChoice() == 1u) drawMotionField(wave, start, range, viewStart, viewRange);
            if (!chop) {
                const bool moving = playbackChoice() == 1u || playbackChoice() == 4u
                    || (playbackChoice() == 2u && familyValue(NeonFamily::GrainSource).load() != 0);
                const double next = start + range * (moving
                    ? p.motionPositions[selectedSlot].load() : p.editPositions[selectedSlot].load());
                if ((!follow || moving) && (!moving || p.slotPlaying[selectedSlot].load()) && next >= viewStart && next <= viewEnd) {
                    const double x = xFor(next);
                    line(CPoint(x, wave.top), CPoint(x, wave.bottom), color(0x91aaa3), 2.0);
                    const bool eventPosition = (playbackChoice() == 1u && familyValue(NeonFamily::MotionModel).load() != 0)
                        || playbackChoice() == 2u;
                    text(moving ? eventPosition ? "EVENT" : "SCAN" : "START", rect(std::clamp(x + 4.0, wave.left, wave.right - 38.0),
                        wave.bottom + 1.0, 34.0, 10.0), style.value, kCenterText, smallFont);
                }
            }
            for (unsigned cursor = 0u; cursor < visual.count; ++cursor) {
                const auto& head = visual.cursors[cursor];
                const auto* source = head.sourceAsset;
                double position = head.sourcePositionNormalized;
                double sourceStart = 0, sourceRange = 1;
                auto shade = style.text;
                if (follow) {
                    const bool first = source == a.asset.get(), second = source == b.asset.get();
                    if (!source || (!first && !second)) continue;
                    const auto& layer = first ? a : b;
                    const unsigned index = first ? blend.first : blend.second;
                    const bool selected = index == p.selectedLayers[selectedSlot].load();
                    const double lo = selected ? paramValue(p, slotParamId(selectedSlot, kSlotStart)) : layer.start;
                    const double hi = selected ? paramValue(p, slotParamId(selectedSlot, kSlotEnd)) : layer.end;
                    position = (position - lo) / std::max(1.0e-9, hi - lo);
                    sourceStart = lo; sourceRange = std::max(1.0e-9, hi - lo);
                    shade.alpha = static_cast<uint8_t>(std::lround(255.0f * (first && second ? 1.0f : first ? 1.0f - blend.mix : blend.mix)));
                } else if (source && source != current.get()) continue;
                const auto technique = playbackChoice();
                if (!chop && technique == 2u) {
                    const unsigned rows = s3g::sample::sampleNeonWaveformRows(display, source ? source->channelCount : 1u);
                    for (unsigned rowIndex=0;rowIndex<rows;++rowIndex) {
                        const auto row = rect(wave.left,wave.top+rowIndex*wave.getHeight()/rows,wave.getWidth(),wave.getHeight()/rows);
                        drawGrainHead(row,head,sourceStart,sourceRange,viewStart,viewRange, false);
                    }
                }
                if (position >= viewStart && position <= viewEnd)
                    line(CPoint(xFor(position), wave.top), CPoint(xFor(position), wave.bottom), shade, 1.0);
            }
        } else if (capture) {
            const double position = p.capturePlayhead.load();
            if (position >= viewStart && position <= viewEnd)
                line(CPoint(xFor(position), wave.top), CPoint(xFor(position), wave.bottom), style.text, 1.0);
        }
        if (editable && edit >= viewStart && edit <= viewEnd)
            line(CPoint(xFor(edit), wave.top), CPoint(xFor(edit), wave.bottom), color(0xb27d69), 2.0);
        if (cutupsPatternVisible()) paintCutupsPattern();
        else if (stackPathVisible()) paintStackPathGraph();
        else {
            const auto overview = overviewRect();
            if (recording) drawRecordingWave(overview, WaveDisplay::CombinedPeaks);
            else drawSource(overview, 0.0, 1.0);
            stroke(rect(overview.left + overview.getWidth() * viewStart, overview.top,
                overview.getWidth() * viewRange, overview.getHeight()), style.value);
        }
        const auto pathPhase = p.stackPathPhases[selectedSlot].load();
        const std::string cursorStatus = cutupsPatternVisible()
            ? pathPhase >= 0 ? format("CUT STEP %02u / %02u",1u+static_cast<unsigned>(std::lround(pathPhase*familyValue(NeonFamily::CutRegions).load())),static_cast<unsigned>(familyValue(NeonFamily::CutRegions).load())) : "CUT PATTERN / CLICK LAYER / ALT: SOURCE"
            : stackPathVisible()
            ? pathPhase >= 0 ? format("STACK PATH %.1f%%", pathPhase * 100) : "STACK PATH / IDLE"
            : follow ? scanning ? format("BLEND %.0f%%", blend.mix * 100.f) : format("PLAYING %02u", blend.first + 1u)
            : format("CURSOR %.2f%%", edit * 100.);
        const auto viewStatus = cursorStatus + format(" / ZOOM %.1fx / ", zoom);
        text(stackPathVisible() && pathPointLimit ? "32 POINT LIMIT / RIGHT-CLICK AN INTERIOR POINT TO REMOVE"
            : viewStatus + (capture ? recording ? "RECORDING / LIVE WAVEFORM / CHANNEL-LINKED"
                : "CAPTURE REVIEW / CHANNEL-LINKED" : routeStatus(current.get())),
            rect(W::left, W::routeTop(), W::width, 18.0), !capture && current && !routeValid(current.get())
                ? color(0xc28f7a) : style.value);
    }


    unsigned playbackChoice() const
    {
        return playbackIndex(p, selectedSlot);
    }

    void playbackMenu(double y)
    {
        const unsigned choice = playbackChoice();
        text("PLAYBACK", rect(P::left, y + 3.5, P::labelWidth, 18.0), style.label);
        // Display order is independent of stable engine/state identities.
        constexpr std::array<unsigned, 9> order {0, 6, 1, 2, 4, 5, 3, 7, 8};
        std::vector<std::string> names;
        for (auto method : order) names.emplace_back(kPlaybackNames[method]);
        std::vector<bool> available(names.size(), true);
        if (paramValue(p, slotParamId(selectedSlot, kSlotSourceFormat)) >= 0.5) { names[5] += " / NOT AMBI"; available[5] = false; }
        const auto item = static_cast<int>(std::find(order.begin(), order.end(), choice) - order.begin());
        menu(rect(P::controlLeft, y, P::controlWidth, 25.0), names[item], names, item,
            [this, order](int index) {
                const auto page = p.editPage.load();
                choosePlayback(p, order[index]);
                // A method change is not a request to leave source/FX/routing.
                p.editPage.store(page);
                if (familyPage == 3 && (order[index] == 0 || order[index] == 3 || order[index] == 8)) {
                    familyPage = 0; p.editPage.store(0);
                }
                workspaceTab = effectiveEditPage(p);
                auditionEditedSlot(p, selectedSlot);
            }, true, 1, kCenterText, available);
    }

    template<class L = I> void triggerMenu(double y)
    {
        const auto trigger = static_cast<int>(paramValue(p, slotParamId(selectedSlot, kSlotTriggerMode)));
        const int choice = trigger == 1 ? 1 : trigger == 3 ? 2 : 0;
        text("TRIGGER", rect(L::left, y + 3.5, L::labelWidth, 18.0), style.label);
        const std::vector<std::string> names { "ONE SHOT", "HOLD", "TOGGLE" };
        menu(rect(L::controlLeft, y, L::controlWidth, 25.0), names[static_cast<std::size_t>(choice)], names, choice,
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

    std::vector<std::string> fxChoices(const char* names) const {
        std::vector<std::string> result;
        std::string source(names);
        for (size_t at = 0; !source.empty();) {
            const auto end = source.find('|',at);
            result.push_back(source.substr(at,end-at));
            if (end == std::string::npos) break;
            at = end+1;
        }
        return result;
    }
    unsigned fxEffect() const { return static_cast<unsigned>(paramValue(p,slotParamId(selectedSlot,kSlotCharacter))); }
    unsigned fxLastRow() const { return 7+s3g::sample::kNeonCharacterCounts[fxEffect()]; }
    std::string fxValue(unsigned effect, unsigned n, float unit) const {
        using namespace s3g::sample;
        const auto& d = kNeonCharacterDefs[effect][n];
        const auto choices = fxChoices(d.choices);
        const double value = neonCharacterDisplay(effect,n,unit);
        if (!choices.empty()) return choices[static_cast<unsigned>(value)];
        if (effect == 4 && n == 0) {
            constexpr const char* vowels[] {"AH","EH","EE","OH","OO"};
            const unsigned first = std::min(3u,static_cast<unsigned>(value));
            return value >= 4 ? "OO" : format("%s/%s %.0f%%",vowels[first],vowels[first+1],(value-first)*100);
        }
        if (effect == 0 && n == 1 && p.fxParameters[selectedSlot][0][0].load() < .83f)
            return format("%.0f Hz",20*std::pow(1000.0,unit));
        return format(std::abs(value) < 10 ? "%.2f%s" : "%.0f%s",value,d.unit);
    }
    void paintFxSettings() {
        using namespace s3g::sample;
        const unsigned effect = fxEffect(), count = kNeonCharacterCounts[effect];
        const bool ambi = paramValue(p,slotParamId(selectedSlot,kSlotSourceFormat)) >= .5;
        characterMenu(I::menu(2));
        sliderRow(slotParamId(selectedSlot,kSlotMangle),"MIX",I::slider(3));
        textureSliderRow(p.fxParameters[selectedSlot][effect][11],"OUT",I::slider(4),0,1,.75f,
            fxValue(effect,11,p.fxParameters[selectedSlot][effect][11].load()),
            [effect](double x){return neonCharacterDisplay(effect,11,x);},
            [effect](double x){return neonCharacterNormalized(effect,11,x);});
        sliderRow(slotParamId(selectedSlot,kSlotPressureDepth),"PRESSURE",I::slider(5));
        for (unsigned n = 0; n < count; ++n) {
            auto& value = p.fxParameters[selectedSlot][effect][n];
            const auto& d = kNeonCharacterDefs[effect][n];
            const unsigned row = 6+n;
            if (effect==1 && n==1 && p.fxParameters[selectedSlot][effect][7].load()>=.5) {
                NeonCharacterValues shown;
                for(unsigned i=0;i<shown.size();++i) shown[i]=p.fxParameters[selectedSlot][effect][i].load();
                const double seconds=neonCharacterEchoSeconds(shown,p.hostTempo.load());
                inspectorReadout("TIME",format(seconds>=2?"%.0f ms / MAX":"%.0f ms / BEATS",seconds*1000).c_str(),row);
                continue;
            }
            const auto choices = fxChoices(d.choices);
            if (!choices.empty()) {
                inspectorLabel(d.label,row);
                std::vector<bool> allowed;
                for (unsigned item = 0; item < choices.size(); ++item)
                    allowed.push_back(neonCharacterControlAllowed(effect,n,item,ambi));
                const unsigned item = static_cast<unsigned>(neonCharacterDisplay(effect,n,value.load()));
                menu(inspectorMenu(row),choices[item],choices,item,[this,effect,n](int item) {
                    p.fxParameters[selectedSlot][effect][n].store(static_cast<float>(neonCharacterNormalized(effect,n,item)));
                    if(effect==0 && n==0 && item==3) p.fxParameters[selectedSlot][0][1].store(.5f);
                    markStateDirty(p); requestProcess(p);
                },true,1,kCenterText,allowed);
            } else {
                const bool frequency = effect == 0 && n == 1 && p.fxParameters[selectedSlot][0][0].load() < .83f;
                textureSliderRow(value,frequency ? "CUTOFF" : d.label,I::slider(row),0,1,effect==0 && n==1 && !frequency ? .5f : d.initial,
                    fxValue(effect,n,value.load()),
                    [effect,n,frequency](double x){return frequency ? 20*std::pow(1000.0,x) : neonCharacterDisplay(effect,n,x);},
                    [effect,n,frequency](double x){return frequency ? std::clamp(std::log(std::max(20.0,x)/20)/std::log(1000.0),0.0,1.0) : neonCharacterNormalized(effect,n,x);});
            }
        }
        std::vector<std::string> pairs;
        for (unsigned pair = 0; pair < kNeonCharacterPairCounts[effect]; ++pair) {
            const bool beats=p.fxParameters[selectedSlot][1][7].load()>=.5f;
            const auto a = neonCharacterKnob(effect,pair,true,beats), b = neonCharacterKnob(effect,pair,false,beats);
            pairs.push_back(std::string(kNeonCharacterDefs[effect][a].label)+" / "+(b==12?"MIX":kNeonCharacterDefs[effect][b].label));
        }
        const unsigned pair = p.fxPair.load()%pairs.size();
        inspectorLabel("ENCODERS",6+count);
        menu(inspectorMenu(6+count),pairs[pair],pairs,pair,[this](int choice){p.fxPair.store(static_cast<uint8_t>(choice));});
        button(inspectorButton(7+count),"RESET THIS EFFECT",[this,effect] {
            for (unsigned n=0;n<kNeonCharacterControls;++n) p.fxParameters[selectedSlot][effect][n].store(kSampleNeonFxDefaults[effect][n]);
            markStateDirty(p);requestProcess(p);
        });
        if (ambi) helpText("ACN/SN3D: LINKED FIELD / DRIVE, CRUSH AND TAPE DISABLED",I::statusTop);
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

    void refreshChopForTrim(clap_id id) {
        std::size_t stored = 0u, slot = 0u;
        SlotParamOffset offset = kSlotGain;
        const ParamDef* definition = nullptr;
        if (id >= kSlotParamBase && parameterLocation(id, stored, definition, &slot, &offset)
            && (offset == kSlotStart || offset == kSlotEnd)) {
            const auto mode = static_cast<SampleNeonChopMode>(p.chopModes[slot].load());
            if (mode == SampleNeonChopMode::Transient || mode == SampleNeonChopMode::BeatGrid)
                regenerateChopLayout(p, slot);
        }
    }
    void resetParameter(clap_id id) {
        std::size_t stored = 0u;
        const ParamDef* definition = nullptr;
        if (!parameterLocation(id, stored, definition)) return;
        queueGuiParamGesture(p, id, definition->defaultValue);
        refreshChopForTrim(id);
    }
    void advancedKnobDrag(CRect bounds, double trackX, double trackWidth, bool loop) {
        const auto page = effectiveEditPage(p);
        const bool host = p.playbackClocks[selectedSlot].load() != 0u;
        const clap_id id = !loop && page == 5u ? slotParamId(selectedSlot, kSlotTune)
            : page == 7u && s3g::sample::neonCharacterKnob(fxEffect(),p.fxPair.load(),loop,p.fxParameters[selectedSlot][1][7].load()>=.5f)==12 ? slotParamId(selectedSlot, kSlotMangle) : CLAP_INVALID_ID;
        double defaultValue = 0.0;
        if (page == 12u) defaultValue = loop ? host ? 4./7 : (8.-.1)/79.9 : 0;
        else if (page == 11u) defaultValue = loop ? 0 : .25;
        else if (page == 6u && familyValue(NeonFamily::WavesetEngine).load() != 0)
            defaultValue = loop ? 0 : std::log(110. / 20) / std::log(100.);
        else if (page == 9u) defaultValue = loop ? 0 : .2;
        else if (page == 8u) defaultValue = loop ? 0.0 : host
            ? nearestMotionCycleIndex(ControlDefaults::cycleBeats) / 7.0 : (ControlDefaults::cycleSeconds - 0.05) / 29.95;
        else if (page == 7u) {
            const auto effect = static_cast<unsigned>(paramValue(p, slotParamId(selectedSlot, kSlotCharacter)));
            const unsigned n = s3g::sample::neonCharacterKnob(effect,p.fxPair.load(),loop,p.fxParameters[selectedSlot][1][7].load()>=.5f);
            defaultValue = n == 12 ? 0 : s3g::sample::kSampleNeonFxDefaults[effect][n];
            if (effect == 0 && n == 1 && p.fxParameters[selectedSlot][0][0].load() > .833f)
                defaultValue = .5; // DJ centre is neutral, including the mini surface.
        } else if (page == 5u) defaultValue = host
            ? nearestMotionCycleIndex(ControlDefaults::cycleBeats) / 7.0 : (ControlDefaults::cycleSeconds - 0.05) / 29.95;
        else if (page == 4u && loop && host) defaultValue = nearestMotionCycleIndex(ControlDefaults::grainInterval * 8.0f) / 7.0;
        else defaultValue = ControlDefaults::technique[loop ? 0u : page == 4u ? 2u : 1u];
        drag(bounds, [this, loop, id, trackX, trackWidth](CPoint point) {
            const auto unit = std::clamp((point.x - trackX) / trackWidth, 0.0, 1.0);
            if (id != CLAP_INVALID_ID) queueGuiParamValue(p, id, fromNormalized(id, unit));
            else setAdvancedKnob(p, loop, unit);
        }, [this, id] { if (id != CLAP_INVALID_ID) queueGuiParamBegin(p, id); },
           [this, id] { if (id != CLAP_INVALID_ID) queueGuiParamEnd(p, id); },
           [this, loop, id, defaultValue] {
               if (id != CLAP_INVALID_ID) resetParameter(id);
               else setAdvancedKnob(p, loop, defaultValue);
           });
    }

#include "s3g_sample_neon_mini_gui.inc"

    void textureSliderRow(std::atomic<float>& target, const char* label,
        double y, float minimum, float maximum, float defaultValue, const std::string& valueLabel,
        std::function<double(double)> displayValue = {}, std::function<double(double)> storageValue = {}, double left = I::left, double totalWidth = I::width,
        double shownValue = std::numeric_limits<double>::quiet_NaN(), bool integral = false)
    {
        const double trackX = left + 82.0;
        const double trackWidth = totalWidth - 82 - I::valueGap - I::valueWidth;
        const double visible = std::isfinite(shownValue) ? shownValue : target.load(std::memory_order_relaxed);
        const double unit = (std::clamp(visible, double(minimum), double(maximum)) - minimum)
            / (maximum - minimum);
        text(label, rect(left, y - 2.0, I::labelWidth, 18.0), style.label);
        foundation::drawHorizontalSlider(*context,
            rect(trackX, y + 3.0, trackWidth, 8.0), unit, y, 14.0, style);
        text(valueLabel, rect(trackX + trackWidth + I::valueGap, y - 2.0,
                63.0, 18.0), style.value, kRightText, font);
        const auto apply = [this, &target, minimum, maximum, integral, slot = selectedSlot](double value) {
            if (!std::isfinite(value)) return;
            if (integral) value = std::round(value);
            for (unsigned n = 0; n < 64; ++n)
                if (&target == &p.familyControls[slot][neonFamilyIndex(NeonFamily::CutPatternSource)+n]) {
                    p.familyControls[slot][neonFamilyIndex(NeonFamily::CutSourceOrder)].store(6); break;
                }
            if (&target == &p.familyControls[slot][neonFamilyIndex(NeonFamily::LanePosition)]) {
                setLanePosition(p, static_cast<unsigned>(slot), value); return;
            }
            target.store(static_cast<float>(std::clamp(value, double(minimum), double(maximum))), std::memory_order_release);
            markStateDirty(p); requestProcess(p);
        };
        const bool percent = valueLabel.find('%') != std::string::npos;
        number(rect(trackX + trackWidth + I::valueGap, y - 2.0, 63.0, 18.0),
            displayValue ? displayValue(visible) : visible * (percent ? 100.0 : 1.0),
            [apply, percent, storageValue](double value) {
                if (!std::isfinite(value)) return;
                value = storageValue ? storageValue(value) : value / (percent ? 100.0 : 1.0);
                apply(value);
            });
        drag(rect(trackX - 4.0, y - 4.5, trackWidth + 8.0, I::rowPitch - 1.0),
            [apply, trackX, trackWidth, minimum, maximum](CPoint point) {
                const float unitValue = static_cast<float>(std::clamp(
                    (point.x - trackX) / trackWidth, 0.0, 1.0));
                apply(minimum + unitValue * (maximum - minimum));
            }, {}, {}, [apply, defaultValue] { apply(defaultValue); });
    }

    void textureDoubleSliderRow(std::atomic<double>& target,
        const char* label, double y, double minimum, double maximum,
        double defaultValue, const std::string& valueLabel)
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
        const auto apply = [this, &target, slot = selectedSlot](double value) {
            if (&target == &p.captureStart || &target == &p.captureEnd)
                setCaptureTrim(p, &target == &p.captureEnd, value);
            else target.store(value, std::memory_order_release);
            if (&target == &p.sourceBpms[slot]
                && p.chopModes[slot].load() == static_cast<uint8_t>(SampleNeonChopMode::BeatGrid))
                regenerateChopLayout(p, slot);
            markStateDirty(p);
            requestProcess(p);
        };
        drag(rect(trackX - 4.0, y - 4.5, trackWidth + 8.0, I::rowPitch - 1.0),
            [apply, trackX, trackWidth, minimum, maximum](CPoint point) {
                apply(minimum + std::clamp((point.x - trackX) / trackWidth, 0.0, 1.0) * (maximum - minimum));
            }, {}, {}, [apply, defaultValue] { apply(defaultValue); });
    }

    template<class L = I> void sourceModeMenu(unsigned row) {
        if (playbackChoice() == 8u) {
            familyMenu<L>(NeonFamily::CutFileOrder,"LAYER ORDER",row,
                {"DOWN","UP","PALINDROME","RANDOM","RANDOM CYCLE","MANUAL","PAIRS","OUTSIDE IN","STAGGER","CENTER OUT"});
            return;
        }
        if (playbackChoice() == 6u) { familyMenu<L>(NeonFamily::LaneAuto, "NAVIGATION", row, {"MANUAL", "PATH"}); return; }
        if (playbackChoice() == 3u && familyValue(NeonFamily::MosaicMode).load() != 0) {
            inspectorReadout<L>("LAYER SRC", familyValue(NeonFamily::MosaicScope).load() != 0 ? "MOSAIC / STACK" : "MOSAIC / PRIMARY", row);
            return;
        }
        const unsigned mode = playbackChoice() == 3u ? 0u : std::min<unsigned>(4u, p.sourceModes[selectedSlot].load());
        const std::vector<std::string> names {"PRIMARY", "SELECTED LAYER", "VELOCITY", "RANDOM / TRIGGER", "STACK SCAN"};
        std::vector<bool> enabled(5u, playbackChoice() != 5u && playbackChoice() != 3u);
        enabled[0] = true; enabled[4] = playbackChoice() == 1u || playbackChoice() == 2u || playbackChoice() == 4u || playbackChoice() == 5u || playbackChoice() == 7u;
        inspectorLabel<L>("LAYER SRC", row);
        menu(inspectorMenu<L>(row), names[mode], names, static_cast<int>(mode), [this](int choice) {
            p.sourceModes[selectedSlot].store(static_cast<uint8_t>(choice));
            takeStackNavigation(p, static_cast<unsigned>(selectedSlot)); markStateDirty(p);
        }, true, 1, kCenterText, enabled);
    }
    template<class L = I> void layerMenu(unsigned row) {
        const unsigned count = stackCount(p, selectedSlot), selected = p.selectedLayers[selectedSlot].load();
        std::vector<std::string> choices;
        for (unsigned n = 0u; n < count; ++n) choices.push_back(format("%02u / %s", n + 1u,
            p.sources[selectedSlot][n].asset ? n ? "LAYER" : "PRIMARY" : "MISSING"));
        inspectorLabel<L>("EDIT LAYER", row);
        menu(inspectorMenu<L>(row), count ? choices[std::min(selected, count - 1u)] : "EMPTY STACK", choices,
            count ? static_cast<int>(selected) : -1, [this](int choice) { showLayer(p, selectedSlot, static_cast<unsigned>(choice)); }, count != 0u, 4);
    }
#include "s3g_sample_neon_family_gui.inc"
#include "s3g_sample_neon_cutups_gui.inc"

    unsigned normalizeScope = 0;
    void paintStackSettings() {
        const auto count = stackCount(p, selectedSlot);
        button(inspectorButton(2, 0, 3), "LOAD", [this] { chooseSample(); });
        button(inspectorButton(2, 1, 3), "ADD LAYER", [this] { chooseSample(true); }, false, count < 32u);
        button(inspectorButton(2, 2, 3), "REMOVE", [this] { removeLayer(p, selectedSlot); }, false, count != 0u);
        button(inspectorButton(3), "AUDITION EDIT LAYER", [this] { auditionLayer(p, selectedSlot); }, false, bool(asset(selectedSlot)));
        sliderRow(slotParamId(selectedSlot, kSlotStart), "START", I::slider(4));
        sliderRow(slotParamId(selectedSlot, kSlotEnd), "END", I::slider(5));
        const std::vector<std::string> scopes {"EDIT LAYER", "STACK / EACH LAYER", "STACK / KEEP BALANCE"};
        inspectorLabel("NORMALIZE", 6);
        menu(inspectorMenu(6), scopes[normalizeScope], scopes, normalizeScope, [this](int n) { normalizeScope = n; });
        button(inspectorButton(7), "APPLY NORMALIZE / -1 dBFS", [this] {
            requestWorkflow(p, normalizeScope == 0 ? WorkflowCommand::Normalize : normalizeScope == 1
                ? WorkflowCommand::NormalizeStack : WorkflowCommand::NormalizeStackBalanced);
        }, false, normalizeScope ? count != 0u : bool(asset(selectedSlot)));
        inspectorReadout("LAYERS", format("%u / 32", count).c_str(), 8);
        const int playingLayer = p.stackWaveformLayers[selectedSlot].load();
        const double position = playingLayer == -2 ? p.stackPositions[selectedSlot].load() * (count ? count - 1u : 0u)
            : static_cast<double>(playingLayer);
        for (unsigned n = 0u; n < 32u; ++n) {
            const auto bounds = inspectorButton(9u + n / 4u, n % 4u, 4u);
            button(bounds, format("%02u%s", n + 1u, n == 0u ? " P" : ""),
                [this, n] { showLayer(p, selectedSlot, n); }, p.selectedLayers[selectedSlot].load() == n, n < count);
            if (n < count && playingLayer != -1 && std::abs(position - n) < 1.0)
                fill(rect(bounds.left + 2.0, bounds.bottom - 3.0, (bounds.getWidth() - 4.0) * (1.0 - std::abs(position - n)), 2.0), style.fill);
        }
    }

    void paintPadToolbox() {
        panel(rect(P::left - 16, 42, kPanelWidth, P::row(7) + 18 - 42),
            format("PAD / %c%zu / %u LAYERS", 'A' + selectedSlot / 8, selectedSlot % 8 + 1, stackCount(p, selectedSlot)));
        const int playing = p.stackWaveformLayers[selectedSlot].load();
        const auto count = stackCount(p, selectedSlot);
        const double position = 1 + p.stackPositions[selectedSlot].load() * (count ? count - 1 : 0);
        const auto sounding = p.stackHeld[selectedSlot].load() ? format("HELD / %.2f", position)
            : p.stackManual[selectedSlot].load() >= 0 ? format("OVERRIDE / %.2f", position)
            : playing == -2 ? format("%.2f / %u", position, count)
            : playing >= 0 ? format("%02d / %u", playing + 1, count)
            : p.slotPlaying[selectedSlot].load() ? std::string("EDIT AUDITION") : std::string("STOPPED");
        inspectorReadout<P>("PLAY LAYER", sounding.c_str(), 0);
        playbackMenu(P::menu(1)); sourceModeMenu<P>(2); layerMenu<P>(3); triggerMenu<P>(P::menu(4));
        sliderRow(slotParamId(selectedSlot, kSlotGain), "GAIN", P::slider(5), P::left);
        sliderRow(slotParamId(selectedSlot, kSlotTune), "TUNE", P::slider(6), P::left);
        button(inspectorButton<P>(7, 0, 2), "AUDITION PAD", [this] { auditionEditedSlot(p, selectedSlot); }, false, count != 0u);
        button(inspectorButton<P>(7, 1, 2), "STOP PAD", [this] { p.pendingCellStops.fetch_or(1u << selectedSlot); requestProcess(p); });
    }

    void paintRouting() {
        if (noteEditor.open) {
            const bool received = p.utilityMapAvailable.load();
            const bool following = noteEditor.draft.followUtility;
            inspectorLabel("MAP SOURCE",1);
            menu(inspectorMenu(1), following ? "FOLLOW UTILITY" : "LOCAL", {"FOLLOW UTILITY","LOCAL"}, following ? 0 : 1,
                [this](int n) { noteEditor.draft.followUtility = n == 0; noteEditor.error.clear(); });
            if (received && following) {
                s3g::controller::neon_midi::NoteMap remote;
                if (p.utilityNoteMap.read(remote)) { noteEditor.draft.notes=remote.notes; noteEditor.draft.custom=true; }
            }
            text(received && following ? "UTILITY MAP RECEIVED" : "MIDI 0-127 / APPLY STOPS PADS",
                rect(I::left,I::label(2),I::width,18),style.value);
            noteEditor.paint(*this,I::left,I::label(3),I::width,[this](auto map) {
                p.noteMap.store(map); p.noteNamesPending.store(true); markStateDirty(p); requestProcess(p);
            }, !(received && following));
            // Following can be selected after a local edit even though its
            // map cells are read-only. The receiving map remains authoritative.
            if (received && following) button(rect(I::left+(I::width+8)/2,I::label(3)+304,(I::width-8)/2,20),"FOLLOW",[this] {
                p.noteMap.store(noteEditor.draft); noteEditor.open=false;
                p.noteNamesPending.store(true); markStateDirty(p); requestProcess(p);
            });
            return;
        }
        menuRow(kMidiReceiveParamId, "MIDI IN", I::menu(2));
        menuRow(slotParamId(selectedSlot, kSlotSourceFormat), "FORMAT", I::menu(3));
        menuRow(slotParamId(selectedSlot, kSlotOutputBus), "OUTPUT", I::menu(4));
        const bool ambi = paramValue(p, slotParamId(selectedSlot, kSlotSourceFormat)) != 0;
        const bool distribute = !ambi && familyValue(NeonFamily::RoutingMode).load() != 0;
        const auto choice = [&](NeonFamily key, const char* label, unsigned row,
            const std::vector<std::string>& names, bool enabled, int forced = -1) {
            const int value = forced >= 0 ? forced : static_cast<int>(familyValue(key).load());
            inspectorLabel(label,row);
            menu(inspectorMenu(row),names[static_cast<std::size_t>(value)],names,value,[this,key](int n) {
                familyValue(key).store(static_cast<float>(n)); markStateDirty(p); requestProcess(p);
            },enabled);
        };
        choice(NeonFamily::RoutingMode,"ROUTE MODE",5,{"PRESERVE FIELD","DISTRIBUTE"},!ambi,ambi ? 0 : -1);
        choice(NeonFamily::RoutingWidth,"OBJECT",6,{"MONO","STEREO"},distribute);
        choice(NeonFamily::RoutingTraversal,"TRAVERSAL",7,{"SEQUENTIAL","REVERSE","PALINDROME","RANDOM","RANDOM CYCLE"},distribute);
        const auto source = asset(selectedSlot);
        const bool stereoGrains = !ambi && playbackChoice() == 2 && source && source->channelCount == 2;
        choice(NeonFamily::GrainStereoLink,"STEREO LINK",8,{"LINKED","INDEPENDENT"},stereoGrains,stereoGrains ? -1 : 0);
        sliderRow(slotParamId(selectedSlot, kSlotPan), "PAN", I::slider(9));
        const bool velocity = slotOptionEnabled(p, selectedSlot, kSlotVelocityOption);
        inspectorLabel("VELOCITY", 10);
        menu(inspectorMenu(10), velocity ? "PAD VELOCITY" : "CONSTANT LEVEL", {"PAD VELOCITY", "CONSTANT LEVEL"}, velocity ? 0 : 1,
            [this](int n) { setSlotOption(p, selectedSlot, kSlotVelocityOption, n == 0); });
        button(inspectorButton(11,0,1),"PAD NOTE MAP",[this] {
            noteEditor.begin(noteMapSnapshot(p),static_cast<unsigned>(paramValue(p,kBaseNoteParamId)));
        });
    }

    void sliceEnvelopeRows(unsigned first) {
        const NeonFamily keys[] {NeonFamily::SliceAttack,NeonFamily::SliceDecay,NeonFamily::SliceSustain,NeonFamily::SliceRelease};
        const char* labels[] {"ATTACK %","DECAY %","SUSTAIN","RELEASE %"};
        for (unsigned n=0;n<4;++n) {
            const auto key=keys[n]; const auto d=neonFamilyDef(neonFamilyIndex(key));
            textureSliderRow(familyValue(key),labels[n],I::slider(first+n),0,1,d.initial,
                format("%.1f%%",familyValue(key).load()*100),[](double x) { return x*100; },[](double x) { return x*.01; });
        }
    }

    void paintSamplePlayback() {
        sliderRow(slotParamId(selectedSlot, kSlotAttack), "ATTACK", I::slider(4));
        sliderRow(slotParamId(selectedSlot, kSlotRelease), "RELEASE", I::slider(5));
        sliderRow(slotParamId(selectedSlot, kSlotFilterCutoff), "CUTOFF", I::slider(6));
        menuRow(slotParamId(selectedSlot, kSlotDirection), "DIRECTION", I::menu(7));
        const bool repeat = slotOptionEnabled(p, selectedSlot, kSlotRepeatOption), sync = slotOptionEnabled(p, selectedSlot, kSlotSyncOption);
        inspectorLabel("REPEAT", 8); menu(inspectorMenu(8), repeat ? "ON" : "OFF", {"OFF", "ON"}, repeat,
            [this](int n) { setSlotOption(p, selectedSlot, kSlotRepeatOption, n != 0); });
        inspectorLabel("SYNC", 9); menu(inspectorMenu(9), sync ? "ON" : "OFF", {"OFF", "ON"}, sync,
            [this](int n) { setSlotOption(p, selectedSlot, kSlotSyncOption, n != 0); });
    }

    void paintSliceToolbox(uint8_t)
    {
        const bool transient = p.chopModes[selectedSlot].load() == static_cast<uint8_t>(SampleNeonChopMode::Transient);
        const bool stack = p.slicesToStack.load();
        const unsigned target = p.chopDestination.load();
        const auto one = chopDestinationPlan(p, static_cast<unsigned>(selectedSlot), 1u, stack, target);
        const auto all = chopDestinationPlan(p, static_cast<unsigned>(selectedSlot), sliceCount(p, selectedSlot), stack, target);
        std::vector<std::string> routeLines;
        std::string route = "ALL > " + chopDestinationSummary(all);
        while (!route.empty()) {
            auto length = route.size();
            if (length > 43u) { length = route.rfind(' ', 43u); if (length == std::string::npos) length = 43u; }
            routeLines.push_back(route.substr(0, length)); route.erase(0, length);
            if (!route.empty() && route.front() == ' ') route.erase(0, 1);
        }
        panel(rect(kPanelLeft, 42.0, kPanelWidth, I::row((transient ? 15u : 16u) + static_cast<unsigned>(routeLines.size())) + 18 - 42), "CHOP / AUDITION AND ASSIGN");
        text(statusLabel(), rect(I::left, I::label(0), I::width, 18.0), style.value, kCenterText);
        button(inspectorButton(1, 0, 2), "LOAD SOURCE", [this] { chooseSample(); });
        button(inspectorButton(1, 1, 2), "AUDITION SLICE",
            [this] { auditionSlice(p, selectedSlot, p.visibleSlice.load()); }, false, bool(asset(selectedSlot)));
        sliderRow(slotParamId(selectedSlot, kSlotStart), "START", I::slider(2));
        sliderRow(slotParamId(selectedSlot, kSlotEnd), "END", I::slider(3));
        unsigned nextRow = 5u;
        if (transient) {
            const std::size_t slot = selectedSlot;
            const double value = p.transientPreRollMs[slot].load();
            const auto apply = [this, slot](double next) {
                if (!std::isfinite(next)) return;
                p.transientPreRollMs[slot].store(static_cast<float>(std::round(std::clamp(next, 0.0, 50.0) * 10.0) / 10.0));
                regenerateChopLayout(p, slot);
            };
            text("PRE-ROLL", rect(I::left, I::label(4), I::labelWidth, 18.0), style.label);
            foundation::drawHorizontalSlider(*context, rect(I::controlLeft, I::row(4) - 4.0, I::trackWidth, 8.0), value / 50.0, I::slider(4), 14.0, style);
            drag(rect(I::controlLeft - 4.0, I::row(4) - 11.5, I::trackWidth + 8.0, I::rowPitch - 1.0),
                [apply](CPoint point) { apply((point.x - I::controlLeft) / I::trackWidth * 50.0); },
                {}, {}, [apply] { apply(0.0); });
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
                40.0, 240.0, ControlDefaults::sourceBpm, format("%.1f", p.sourceBpms[selectedSlot].load()));
            ++nextRow;
        }
        inspectorLabel("DESTINATION", nextRow);
        menu(inspectorMenu(nextRow), stack ? "ONE PAD / LAYER STACK" : "PAD CELLS",
            {"PAD CELLS", "ONE PAD / LAYER STACK"}, stack ? 1 : 0,
            [this](int choice) { p.slicesToStack.store(choice == 1); p.workflowMessage.clear(); });
        ++nextRow;
        inspectorLabel(stack ? "TARGET PAD" : "START PAD", nextRow);
        const bool replace = target == selectedSlot;
        std::vector<std::string> pads {"AUTO / EMPTY"};
        std::vector<bool> selectable {true};
        for (unsigned pad = 0u; pad < 32u; ++pad) {
            const bool empty = emptyCell(p, pad);
            pads.push_back(chopPadName(pad) + (pad == selectedSlot ? " / REPLACE" : empty ? " / EMPTY" : " / USED"));
            selectable.push_back(empty || pad == selectedSlot);
        }
        const unsigned choice = target < 32u ? target + 1u : 0u;
        menu(inspectorMenu(nextRow), replace ? chopPadName(target) + " / REPLACE SOURCE" : pads[choice], pads,
            static_cast<int>(choice), [this](int item) {
                p.chopDestination.store(static_cast<uint8_t>(item ? static_cast<unsigned>(item - 1) : s3g::sample::kNeonChopAutoDestination));
                p.workflowMessage.clear();
            }, true, 4, kCenterText, selectable);
        const auto oneTo = one.valid() ? chopPadName(one.pads[0]) : "BLOCKED";
        const auto allSummary = chopDestinationSummary(all);
        const auto allTo = all.valid() ? stack ? chopPadName(all.pads[0]) + " LAYERS"
            : allSummary.find(',') == std::string::npos ? allSummary : std::to_string(all.count) + " PADS" : "BLOCKED";
        button(inspectorButton(nextRow + 1u), format("%s SLICE %02u > %s", replace ? "REPLACE /" : "ASSIGN",
                p.visibleSlice.load() + 1u, oneTo.c_str()),
            [this] { requestWorkflow(p, WorkflowCommand::MapSlice); }, false, bool(asset(selectedSlot)) && one.valid());
        button(inspectorButton(nextRow + 2u), format("%s ALL %02u > %s", replace ? "REPLACE /" : "ASSIGN",
                sliceCount(p, selectedSlot), allTo.c_str()),
            [this] { requestWorkflow(p, WorkflowCommand::MapAll); }, false, bool(asset(selectedSlot)) && all.valid());
        // The exact plan is also shown for AUTO, which can skip occupied pads.
        // Wrap the route at separators instead of silently clipping destinations.
        unsigned routeRow = nextRow + 3u;
        for (const auto& line : routeLines)
            text(line, rect(I::left, I::label(routeRow++), I::width, 18.0), all.valid() ? style.value : color(0xc28f7a));
        text(replace ? "SOURCE " + chopPadName(selectedSlot) + " WILL BE REPLACED" : "SOURCE " + chopPadName(selectedSlot) + " KEPT",
            rect(I::left, I::label(routeRow), I::width, 18.0), replace ? color(0xc28f7a) : style.dim);
        text("SLICE AMP / PROPORTIONAL",rect(I::left,I::label(routeRow+2),I::width,18),style.label);
        sliceEnvelopeRows(routeRow+3);
        if (!p.workflowMessage.empty()) helpText(p.workflowMessage, I::statusTop);
    }

    void paintCaptureToolbox()
    {
        const auto source = p.selectedSlot.load();
        const auto sourceBus = static_cast<unsigned>(paramValue(p, slotParamId(source, kSlotOutputBus)));
        const bool busMismatch = p.publishedAssets[source].load() && sourceBus != p.captureBus.load() + 1u;
        panel(rect(kPanelLeft, 42.0, kPanelWidth, I::row(busMismatch ? 9 : 8) + 18 - 42), "RESAMPLE / OUTPUT BUS");
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
        textureDoubleSliderRow(p.captureStart, "START", I::slider(5), 0.0, 1.0, 0.0, format("%.1f%%", p.captureStart.load() * 100.0));
        textureDoubleSliderRow(p.captureEnd, "END", I::slider(6), 0.0, 1.0, 1.0, format("%.1f%%", p.captureEnd.load() * 100.0));
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
        if (busMismatch)
            text(format("CELL BUS %u / CAPTURE BUS %u", sourceBus, p.captureBus.load() + 1u),
                rect(I::left, I::label(9), I::width, 18.0), color(0xc28f7a), kCenterText);
        if (!recording && !p.workflowMessage.empty()) helpText(p.workflowMessage, I::statusTop);
    }

    void lanesPositionRow(unsigned row) {
        const unsigned count = stackCount(p, selectedSlot);
        if (count < 2) { inspectorReadout<E>("LAYER POS", count ? "1 / 1" : "EMPTY STACK", row); return; }
        const double span = std::max(1u, count - 1u);
        auto& value = familyValue(NeonFamily::LanePosition);
        const double shown = familyValue(NeonFamily::LaneAuto).load() != 0
            ? p.stackPositions[selectedSlot].load() : value.load();
        textureSliderRow(value, "LAYER POS", E::slider(row), 0, 1, 0,
            format("%.2f / %u", 1 + shown * (count - 1u), count),
            [span](double x) { return 1 + x * span; }, [span](double x) { return (x - 1) / span; }, E::left, E::width, shown);
    }
    void paintLanesPlayback() {
        clockMenu(E::menu(5));
        lanesPositionRow(7);
        const auto step = [this](int delta) {
            const unsigned count = stackCount(p, selectedSlot);
            if (count < 2) return;
            const double at = (familyValue(NeonFamily::LaneAuto).load() != 0 ? p.stackPositions[selectedSlot].load()
                : familyValue(NeonFamily::LanePosition).load()) * (count - 1);
            const double next = delta > 0 ? std::floor(at + .00001) + 1 : std::ceil(at - .00001) - 1;
            setLanePosition(p, static_cast<unsigned>(selectedSlot), next / (count - 1));
        };
        button(inspectorButton<E>(8, 0, 2), "PREVIOUS LAYER", [step] { step(-1); }, false, stackCount(p, selectedSlot) > 1);
        button(inspectorButton<E>(8, 1, 2), "NEXT LAYER", [step] { step(1); }, false, stackCount(p, selectedSlot) > 1);
        familyMenu<E>(NeonFamily::StackJump, "TRANSITION", 9, {"CROSSFADE", "JUMP"});
        familySlider<E>(NeonFamily::LaneRate, "SPEED", 10, " x");
        auto& slew = familyValue(NeonFamily::LaneSlew);
        textureSliderRow(slew, "LAYER SLEW", E::slider(11), .001f, .1f, .01f, format("%.1f ms", slew.load() * 1000),
            [](double x) { return x * 1000; }, [](double x) { return x / 1000; });
        auto& join = familyValue(NeonFamily::LaneJoin);
        textureSliderRow(join, "LOOP JOIN", E::slider(12), 0, .25f, .02f, format("%.1f%%", join.load() * 100),
            [](double x) { return x * 100; }, [](double x) { return x / 100; });
        menuRow(slotParamId(selectedSlot, kSlotDirection), "DIRECTION", E::menu(13));
        const auto trigger = paramValue(p, slotParamId(selectedSlot, kSlotTriggerMode));
        if (trigger == 1 || trigger == 3) inspectorReadout<E>("DURATION", trigger == 1 ? "PAD HOLD + RELEASE" : "UNTIL NEXT PRESS", 14);
        else textureSliderRow(p.shotSeconds[selectedSlot], "SHOT LEN", E::slider(14), .05f, 30, ControlDefaults::shotSeconds,
            format("%.2f s", p.shotSeconds[selectedSlot].load()));
        techniqueEnvelopeRows(E::slider(15));
        if (familyValue(NeonFamily::LaneAuto).load() == 0) inspectorReadout<E>("LAYERS", format("%u / 32", stackCount(p, selectedSlot)).c_str(), 17);
        else if (p.playbackClocks[selectedSlot].load()) beatMenu(p.stackBeats[selectedSlot], "PATH CYCLE", E::menu(17));
        else textureSliderRow(p.stackSeconds[selectedSlot], "PATH CYCLE", E::slider(17), .05f, 30, ControlDefaults::cycleSeconds,
            format("%.2f s", p.stackSeconds[selectedSlot].load()));
        button(inspectorButton<E>(18), "EDIT STACK PATH", [this] { familyPage = 3; workspaceTab = 8; p.editPage.store(8); });
        button(inspectorButton<E>(19), "EDIT SAMPLE STACK", [this] { familyPage = 0; workspaceTab = 8; p.editPage.store(8); });
    }

    void paintExtendedPlayback()
    {
        const auto method = playbackChoice();
        if (method == 8u) { paintCutupsPlayback(); return; }
        if (method == 6u) { paintLanesPlayback(); return; }
        const bool oscillator = method == 5u && familyValue(NeonFamily::WavesetEngine).load() != 0;
        if (method == 7u || oscillator) {
            using F = NeonFamily;
            if (oscillator) familyMenu<E>(F::WavesetEngine, "ENGINE", 5, {"REARRANGE", "OSCILLATOR"});
            clockMenu(E::menu(oscillator ? 6 : 5));
            if (oscillator) {
                familySlider<E>(F::OscPosition, "CYCLE POS", 7);
                familySlider<E>(F::OscFrequency, "FREQUENCY", 8, " Hz");
                familySlider<E>(F::OscScan, "SCAN", 9);
                familySlider<E>(F::OscGroup, "GROUP", 10, " cycles");
            } else {
                textureDoubleSliderRow(p.editPositions[selectedSlot], "POSITION", E::slider(6), 0, 1, 0,
                    format("%.1f%%", p.editPositions[selectedSlot].load() * 100));
                familySlider<E>(F::SpectralAdvance, "ADVANCE", 7);
                familySlider<E>(F::SpectralPressure, "PRESS ADV", 8);
                familySlider<E>(F::SpectralBlur, "BLUR", 10);
                familySlider<E>(F::SpectralSmear, "SMEAR", 11, " s");
                familySlider<E>(F::SpectralFocus, "FOCUS", 12);
                familySlider<E>(F::SpectralTilt, "TILT/OCT", 13, " dB");
                familySlider<E>(F::SpectralThin, "THIN", 14);
            }
            const unsigned row = oscillator ? 11 : 9;
            if (p.playbackClocks[selectedSlot].load()) beatMenu(p.motionRates[selectedSlot], "SCAN CYCLE", E::menu(row));
            else textureSliderRow(p.motionSeconds[selectedSlot], "SCAN CYCLE", E::slider(row), .05f, 30, ControlDefaults::cycleSeconds,
                format("%.2f s", p.motionSeconds[selectedSlot].load()));
            const unsigned envelopeRow = oscillator ? 12 : 15;
            textureSliderRow(p.shotSeconds[selectedSlot], "SHOT LEN", E::slider(envelopeRow), .05f, 30, ControlDefaults::shotSeconds,
                format("%.2f s", p.shotSeconds[selectedSlot].load()));
            techniqueEnvelopeRows(E::slider(envelopeRow + 1));
            return;
        }
        auto& values = p.techniqueParameters[selectedSlot][method - 3u];
        const unsigned waveScan = method == 5u && p.sourceModes[selectedSlot].load() == 4u ? 1u : 0u;
        if (method == 5u) familyMenu<E>(NeonFamily::WavesetEngine, "ENGINE", 5, {"REARRANGE", "OSCILLATOR"});
        if (method != 5u || waveScan) clockMenu(E::menu(method == 5u ? 6 : 5));
        const bool host = p.playbackClocks[selectedSlot].load() != 0u;
        if (method == 3u) {
            const int trigger = static_cast<int>(paramValue(p, slotParamId(selectedSlot, kSlotTriggerMode)));
            if (trigger == 0 || trigger == 2)
                textureSliderRow(p.shotSeconds[selectedSlot], "SHOT LEN", E::slider(6), 0.05f, 30.0f, ControlDefaults::shotSeconds, format("%.2f s", p.shotSeconds[selectedSlot].load()));
            else text(trigger == 1 ? "DURATION: WHILE PAD IS HELD" : "DURATION: UNTIL PAD IS TAPPED AGAIN",
                rect(E::left, E::label(6), E::width, 18.0), style.value);
            if (host) beatMenu(p.grainIntervals[selectedSlot], "STEP", E::menu(7), 8.0);
            else textureSliderRow(values[0], "STEP RATE", E::slider(7), 0.0f, 1.0f, ControlDefaults::technique[0u], format("%.1f Hz", 1.0f + values[0].load() * 19.0f),
                [](double x) { return 1.0 + x * 19.0; }, [](double x) { return (x - 1.0) / 19.0; });
            const std::vector<std::string> orders {"FORWARD", "REVERSE", "RANDOM"};
            familyMenu<E>(NeonFamily::MosaicMode, "NAVIGATION", 8, {"SEQUENCE", "SIMILAR", "CONTRAST", "ENERGY", "BRIGHTNESS"});
            if (familyValue(NeonFamily::MosaicMode).load() != 0) {
                familyMenu<E>(NeonFamily::MosaicScope, "CANDIDATES", 9, {"PRIMARY SLICES", "STACK SLICES"});
                if (familyValue(NeonFamily::MosaicMode).load() >= 3) familySlider<E>(NeonFamily::MosaicTarget, "TARGET", 12);
            }
            const int order = static_cast<int>(std::lround(values[1].load() * 2.0f));
            if (familyValue(NeonFamily::MosaicMode).load() == 0) {
                inspectorLabel<E>("ORDER", 9);
                menu(inspectorMenu<E>(9), orders[static_cast<unsigned>(order)], orders, order,
                    [this](int choice) { p.techniqueParameters[selectedSlot][0][1].store(choice * 0.5f); markStateDirty(p); requestProcess(p); });
            }
            textureSliderRow(values[2], "REPEATS", E::slider(10), 0.0f, 1.0f, ControlDefaults::technique[2u], format("%.0f", 1.0 + std::round(values[2].load() * 7.0)),
                [](double x) { return 1.0 + std::round(x * 7.0); }, [](double x) { return (std::round(x) - 1.0) / 7.0; });
            textureSliderRow(values[3], "CHANCE", E::slider(11), 0.0f, 1.0f, ControlDefaults::technique[3u], format("%.0f%%", values[3].load() * 100.0f));
            button(inspectorButton<E>(familyValue(NeonFamily::MosaicMode).load() >= 3 ? 13 : 12), "SLICE ADSR / CHOP", [this] { selectMode(static_cast<uint8_t>(NeonMode::Slicer)); });
        } else if (method == 4u) {
            if (host) beatMenu(p.motionRates[selectedSlot], "DURATION", E::menu(6));
            else textureSliderRow(p.motionSeconds[selectedSlot], "DURATION", E::slider(6), 0.05f, 30.0f, ControlDefaults::cycleSeconds, format("%.2f s", p.motionSeconds[selectedSlot].load()));
            const std::vector<std::string> dirs {"FORWARD", "REVERSE"};
            const bool reverse = paramValue(p, slotParamId(selectedSlot, kSlotDirection)) == 1.0;
            inspectorLabel<E>("DIRECTION", 7);
            menu(inspectorMenu<E>(7), dirs[reverse ? 1u : 0u], dirs, reverse ? 1 : 0,
                [this](int choice) { queueGuiParamGesture(p, slotParamId(selectedSlot, kSlotDirection), choice); });
            textureSliderRow(p.techniqueAttackSeconds[selectedSlot], "ATTACK", E::slider(8), 0.001f, 10.0f, ControlDefaults::techniqueEnvelope, format("%.3f s", p.techniqueAttackSeconds[selectedSlot].load()));
            textureSliderRow(p.techniqueReleaseSeconds[selectedSlot], "RELEASE", E::slider(9), 0.001f, 10.0f, ControlDefaults::techniqueEnvelope, format("%.3f s", p.techniqueReleaseSeconds[selectedSlot].load()));
        } else {
            textureSliderRow(values[0], "GROUP", E::slider(6u + waveScan), 0.0f, 1.0f, ControlDefaults::technique[0u], format("%.0f cycles", 1.0 + std::round(values[0].load() * 31.0)),
                [](double x) { return 1.0 + std::round(x * 31.0); }, [](double x) { return (std::round(x) - 1.0) / 31.0; });
            textureSliderRow(values[1], "REPEATS", E::slider(7u + waveScan), 0.0f, 1.0f, ControlDefaults::technique[1u], format("%.0f", 1.0 + std::round(values[1].load() * 15.0)),
                [](double x) { return 1.0 + std::round(x * 15.0); }, [](double x) { return (std::round(x) - 1.0) / 15.0; });
            const std::vector<std::string> shapes {"REPEAT", "OMIT", "REPLACE", "ENVELOPE", "MULTIPLY", "AVERAGE", "INTERPOLATE", "FRACTAL", "HARMONIC", "GROUP REVERSE", "CYCLE REVERSE", "TELESCOPE"};
            const auto shape = static_cast<int>(std::lround(values[2].load() * 11.0f));
            inspectorLabel<E>("SHAPE", 8u + waveScan);
            menu(inspectorMenu<E>(8u + waveScan), shapes[static_cast<unsigned>(shape)], shapes, shape,
                [this](int choice) { p.techniqueParameters[selectedSlot][2][2].store(choice / 11.0f); markStateDirty(p); requestProcess(p); });
            textureSliderRow(values[3], "PROCESS", E::slider(9u + waveScan), 0.0f, 1.0f, ControlDefaults::technique[3u], format("%.0f%%", values[3].load() * 100.0f));
            if (waveScan) {
                textureSliderRow(p.shotSeconds[selectedSlot], "SHOT LEN", E::slider(11), 0.05f, 30.0f, ControlDefaults::shotSeconds, format("%.2f s", p.shotSeconds[selectedSlot].load()));
                textureSliderRow(p.techniqueAttackSeconds[selectedSlot], "ATTACK", E::slider(12), 0.001f, 10.0f, ControlDefaults::techniqueEnvelope, format("%.3f s", p.techniqueAttackSeconds[selectedSlot].load()));
                textureSliderRow(p.techniqueReleaseSeconds[selectedSlot], "RELEASE", E::slider(13), 0.001f, 10.0f, ControlDefaults::techniqueEnvelope, format("%.3f s", p.techniqueReleaseSeconds[selectedSlot].load()));
            }
            bool ready = false, pending = false;
            const unsigned count = waveScan ? stackCount(p, selectedSlot) : 1u;
            for (unsigned layer = 0u; layer < count; ++layer) {
                const auto& source = p.sources[selectedSlot][layer];
                ready |= source.wavesets && source.wavesets->asset == source.asset;
                pending |= source.waveAnalysisPending;
            }
            if (pending || !ready) helpText(pending ? "ANALYZING LAYERS" : "NO USABLE WAVEFORM CYCLES", E::statusTop);
        }
    }

    void fillHoldButton(CRect bounds) {
        const std::string label = p.fillActive.load() ? "FILL / OVERRIDE" : "FILL HOLD";
        buttonFace(bounds,label,p.fillActive.load(),true,"FILL HOLD");
        drag(bounds, [](CPoint) {}, [this,bounds] { flashButton(bounds,"FILL HOLD"); p.fillGuiHeld.store(true); requestProcess(p); },
            [this] { p.fillGuiHeld.store(false); requestProcess(p); });
    }
    void fillRepeatMenu(CRect bounds) {
        const std::vector<std::string> names {"1/4", "1/8", "1/16", "1/32", "1/64", "1/128"};
        const auto choice = static_cast<unsigned>(p.fillRepeat.load());
        menu(bounds, names[choice], names, static_cast<int>(choice), [this](int n) {
            p.fillRepeat.store(static_cast<float>(n)); markStateDirty(p); requestProcess(p);
        });
    }
    void paintFillToolbox() {
        panel(rect(kPanelLeft,42,kPanelWidth,I::row(14)+18-42), "BUFFER FILL / ALL OUTPUT BUSES");
        inspectorReadout("OUTPUT", p.fillActive.load() ? "FROZEN OVERRIDE" : "LIVE / ROLLING HISTORY", 0);
        fillHoldButton(inspectorButton(1));
        const std::vector<std::string> buffers {"1/4 BEAT", "1/2 BEAT", "1 BEAT", "2 BEATS", "4 BEATS"};
        const auto choice = static_cast<unsigned>(p.fillBuffer.load());
        inspectorLabel("GRAB",2);
        menu(inspectorMenu(2), buffers[choice], buffers, static_cast<int>(choice), [this](int n) {
            p.fillBuffer.store(static_cast<float>(n)); markStateDirty(p); requestProcess(p);
        });
        inspectorLabel("REPEAT",3); fillRepeatMenu(inspectorMenu(3));
        textureSliderRow(p.fillBreakup,"BREAKUP",I::slider(4),0,1,.5f,format("%.0f%%",p.fillBreakup.load()*100));
        inspectorReadout(p.fillActive.load() ? "FROZEN" : "HISTORY",format("%.2f / 4.00 s",p.fillAvailable.load()).c_str(),5);
        inspectorReadout("TEMPO",format("%.1f BPM",p.hostTempo.load()).c_str(),6);
        inspectorReadout("NEON HOLD","SHIFT + MODE/CENSOR",8);
        inspectorReadout("LOOP","REPEAT LENGTH",9);
        inspectorReadout("TRAX","BREAKUP",10);
        button(inspectorButton(12),"RELEASE FILL",[this] { p.fillRelease.store(true); p.fillGuiHeld.store(false); requestProcess(p); });
        button(inspectorButton(14),"BACK TO CELL",[this] { p.fillPage.store(false); });
    }

    void paintToolbox()
    {
        paintPadToolbox();
        if (p.fillPage.load()) { paintFillToolbox(); return; }
        const auto mode = static_cast<NeonMode>(p.visibleMode.load());
        if (mode == NeonMode::HotLoop) { paintCaptureToolbox(); return; }
        if (mode == NeonMode::Slicer) { paintSliceToolbox(static_cast<uint8_t>(mode)); return; }
        // Hardware source/stack shortcuts share the same editor; 8 retains its
        // existing layer encoder mapping, while 0 retains cursor/zoom editing.
        if (familyPage == 3 && workspaceTab != 8) familyPage = 0;
        const unsigned view = familyPage == 3 ? 2 : workspaceTab == 0 || workspaceTab == 8 ? 0
            : workspaceTab == 7 ? 3 : workspaceTab == 10 ? 4 : 1;
        const std::vector<std::string> views {"SOURCE / STACK", "PLAYBACK", "STACK PATH", "CHARACTER FX", "ROUTING"};
        const auto method = playbackChoice();
        const unsigned playbackLast = method == 0 ? 9 : method == 1 || method == 2
            ? s3g::sample_neon_gui::FamilyGrid::cutoff(method == 1,
                static_cast<unsigned>(familyValue(NeonFamily::MotionSound).load()))
            : E::index(method == 8 ? 28 : method == 6 ? 19 : method == 7 ? 17 : method == 5
                ? familyValue(NeonFamily::WavesetEngine).load() != 0 ? 14 : p.sourceModes[selectedSlot].load() == 4 ? 13 : 9
                : method == 4 ? 9 : familyValue(NeonFamily::MosaicMode).load() >= 3 ? 13 : 12);
        const unsigned last = view == 0 ? 16 : view == 2 ? 17 : view == 3 ? fxLastRow() : view == 4 ? noteEditor.open ? 16 : 11 : playbackLast;
        panel(rect(kPanelLeft, 42, kPanelWidth, I::row(last) + 18 - 42), "EDIT / " + views[view]);
        inspectorLabel("EDIT VIEW", 0);
        menu(inspectorMenu(0), views[view], views, view, [this](int n) {
            noteEditor.open = false;
            familyPage = n == 2 ? 3 : 0; familyDetail = 0;
            workspaceTab = n == 0 ? 0 : n == 2 ? 8 : n == 3 ? 7 : n == 4 ? 10 : playbackEditPage(playbackChoice());
            p.editPage.store(workspaceTab);
        }, true, 1, kCenterText, {true, true, playbackChoice() != 0 && playbackChoice() != 3 && playbackChoice() != 8, true, true});
        if (view != 4 || !noteEditor.open) text(statusLabel(), rect(I::left, I::label(1), I::width, 18), style.value, kCenterText);
        if (view == 0) paintStackSettings();
        else if (view == 2) paintFamilySettings();
        else if (view == 3) paintFxSettings();
        else if (view == 4) paintRouting();
        else if (playbackChoice() == 0) paintSamplePlayback();
        else {
            // Character, FX Amount and Pressure have one home: CHARACTER FX.
            if (playbackChoice() >= 3) paintExtendedPlayback();
            else { paintTexturePlayback(); paintIntegratedFamily(); }
        }
        if (fileMessage.empty() && !p.workflowMessage.empty()) helpText(p.workflowMessage, I::statusTop);
        if (!fileMessage.empty()) { footerMessage = fileMessage; footerColor = color(0x77b9a8); }
    }

    void paintTexturePlayback() {
            const bool motion = playbackChoice() == 1u;
            const bool host = p.playbackClocks[selectedSlot].load() != 0u;
            clockMenu(E::menu(5));
            const int trigger = static_cast<int>(paramValue(p, slotParamId(selectedSlot, kSlotTriggerMode)));
            if (trigger == 0 || trigger == 2) {
                textureSliderRow(p.shotSeconds[selectedSlot], "SHOT LEN", E::slider(6), 0.05f, 30.0f, ControlDefaults::shotSeconds,
                    format("%.2f s", p.shotSeconds[selectedSlot].load()));
            } else text(trigger == 1 ? "DURATION: WHILE PAD IS HELD" : "DURATION: UNTIL PAD IS TAPPED AGAIN",
                rect(E::left, E::label(6), E::width, 18.0), style.value);
            if (motion) {
                textureDoubleSliderRow(p.editPositions[selectedSlot], "OFFSET", E::slider(7), 0.0, 1.0, 0.0,
                    format("%.1f%%", p.editPositions[selectedSlot].load() * 100.0));
                const auto pathIndex = std::min<uint8_t>(3u, p.motionPaths[selectedSlot].load());
                const std::vector<std::string> paths { "FORWARD", "REVERSE", "BOUNCE", "WANDER" };
                inspectorLabel<E>("PATH", 8);
                menu(inspectorMenu<E>(8), paths[pathIndex], paths, pathIndex,
                    [this](int choice) { p.motionPaths[selectedSlot].store(static_cast<uint8_t>(choice)); markStateDirty(p); requestProcess(p); });
                if (host) beatMenu(p.motionRates[selectedSlot], "CYCLE", E::menu(9));
                else textureSliderRow(p.motionSeconds[selectedSlot], "CYCLE", E::slider(9), 0.05f, 30.0f, ControlDefaults::cycleSeconds,
                    format("%.2f s", p.motionSeconds[selectedSlot].load()));
                techniqueEnvelopeRows(E::slider(10));
            } else {
                textureDoubleSliderRow(p.editPositions[selectedSlot], "POSITION", E::slider(7), 0.0, 1.0, 0.0,
                    format("%.1f%%", p.editPositions[selectedSlot].load() * 100.0));
                if (host) beatMenu(p.grainIntervals[selectedSlot], "INTERVAL", E::menu(8), 8.0);
                else textureSliderRow(p.grainDensities[selectedSlot], "DENSITY", E::slider(8), 1.0f, 80.0f, ControlDefaults::grainDensity,
                    format("%.1f Hz", p.grainDensities[selectedSlot].load()));
                textureSliderRow(p.grainSizes[selectedSlot], "SIZE", E::slider(9), 5.0f, 500.0f, ControlDefaults::grainSize,
                    format("%.0f ms", p.grainSizes[selectedSlot].load()));
                textureSliderRow(p.grainSprays[selectedSlot], "SPRAY", E::slider(10), 0.0f, 1.0f, ControlDefaults::grainSpray,
                    format("%.0f%%", p.grainSprays[selectedSlot].load() * 100.0f));
                textureSliderRow(p.grainPitchSprays[selectedSlot], "PITCH", E::slider(11), 0.0f, 24.0f, 0.0f,
                    format("%.1f st", p.grainPitchSprays[selectedSlot].load()));
                textureSliderRow(p.grainReverseChances[selectedSlot], "REVERSE", E::slider(12), 0.0f, 1.0f, 0.0f,
                    format("%.0f%%", p.grainReverseChances[selectedSlot].load() * 100.0f));
                techniqueEnvelopeRows(E::slider(13));
            }
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
        double left = I::left, double totalWidth = I::width)
    {
        const auto current = asset(selectedSlot);
        const bool distribute = paramValue(p,slotParamId(selectedSlot,kSlotSourceFormat)) == 0
            && familySnapshot()[NeonFamily::RoutingMode] != 0;
        const auto width = s3g::sample::sampleNeonBusWidth(static_cast<SampleNeonOutputLayout>(
            static_cast<unsigned>(paramValue(p,kOutputLayoutParamId))));
        const bool enabled = id != slotParamId(selectedSlot, kSlotPan)
            || (distribute ? width == 2u : !current || current->channelCount <= 2u);
        const bool inspector = left == I::left || left == P::left;
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
        drag(rect(trackX - 4.0, y - (inspector ? 4.5 : 7.0), trackWidth + 8.0, I::rowPitch - 1.0),
            [this, id, trackX, trackWidth](CPoint point) {
                queueGuiParamValue(p, id, fromNormalized(id,
                    (point.x - trackX) / trackWidth));
            }, [this, id] { queueGuiParamBegin(p, id); },
            [this, id] {
                queueGuiParamEnd(p, id);
                refreshChopForTrim(id);
            }, [this, id] { resetParameter(id); });
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

    void chooseSample(bool append = false)
    {
#if defined(S3G_SAMPLE_FILE_WORKER)
        foundation::FileDialogOptions options;
        options.title = append ? "Add Sample Neon stack layer" : "Replace selected Sample Neon layer";
        const std::string selected = foundation::runFileDialog(
            getFrame(), options);
        if (!selected.empty()) queueSampleLoad(p, selectedSlot, selected, true,
            append ? stackCount(p, selectedSlot) : p.selectedLayers[selectedSlot].load());
#endif
    }

    void chooseSet(bool save, bool confirmed = false)
    {
        if (historyBusy(p)) { p.workflowMessage = "FINISH RECORDING / LOADING / EDITING FIRST"; return; }
        if (!save && !confirmed) {
            bool hasWork = bool(p.captureAsset) || (p.editHistory && !p.editHistory->undo.empty());
            for (unsigned pad = 0; pad < 32; ++pad) hasWork |= stackCount(p, pad) != 0;
            if (hasWork) {
                openPopup(rect(924, 38, 300, 0),
                    {"LOAD SET STARTS NEW HISTORY", "SAVE CURRENT SET FIRST IF NEEDED", "CANCEL", "CONTINUE TO LOAD"}, -1,
                    [this](int choice) { if (choice == 3) chooseSet(false, true); }, 1, {}, 18, {false, false, true, true});
                return;
            }
        }
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
    NeonNoteMapEditor noteEditor;
    std::size_t selectedSlot = 0u;
    uint8_t workspaceTab = 0u;
    std::string fileMessage;
};

} // namespace sample_neon_canvas

s3g::portable_gui::foundation::ContentView* makePortableCanvas(Plugin& p)
{
    return new sample_neon_canvas::Editor(p);
}
