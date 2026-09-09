// Included in each instrument's drum_canvas namespace, after its preset adapter.
// Only the established title/control behavior is shared. Rows and normalization
// are the SAME tables/functions used by Cocoa; each instrument paints its own
// panels, pads, and DSP-driven illustration in its local VSTGUI adapter.
inline std::string fittedDrumReadout(CDrawContext& context, CFontRef font,
    const char* display, double width) {
    context.setFont(font);
    if (context.getStringWidth(display) <= width) return display;
    // A large frequency must never lose its leading digits to right-aligned
    // clipping (e.g. 1180.0 Hz becoming 80.0 Hz). Try a compact, unit-bearing
    // numeric representation within the original value-cell bounds.
    char* end = nullptr;
    const double number = std::strtod(display, &end);
    if (end == display || !std::isfinite(number)) return display;
    const bool hadSpace = *end == ' ';
    while (*end == ' ') ++end;
    for (int precision = 6; precision >= 1; --precision) {
        if (hadSpace) {
            auto spaced = format("%s%.*g %s", display[0] == '+' ? "+" : "",
                precision, number, end);
            if (context.getStringWidth(spaced.c_str()) <= width) return spaced;
        }
        auto candidate = format("%s%.*g%s", display[0] == '+' ? "+" : "",
            precision, number, end);
        if (context.getStringWidth(candidate.c_str()) <= width) return candidate;
    }
    return foundation::sliderValueTextToFit(context, display, width, font);
}

inline SharedPointer<CFontDesc> fittedDrumLabelFont(CDrawContext& context,
    CFontRef font, const char* label, double width) {
    context.setFont(font);
    const double measured = context.getStringWidth(label);
    if (measured <= width) return SharedPointer<CFontDesc>(font);
    auto result = makeOwned<CFontDesc>(*font);
    result->setSize(font->getSize() * width / measured);
    return result;
}

class DrumEditor : public View {
public:
    explicit DrumEditor(Plugin& instance) : View(kGuiWidth, kGuiHeight), p(instance) {
        randomState = std::random_device{}();
        refreshPreset();
    }
    ~DrumEditor() override { stopRefresh(); }
    void service() override {
        if (observedRevision != p.parameterRevision.load(std::memory_order_acquire))
            refreshPreset();
    }
    void refreshPreset() {
        observedRevision = p.parameterRevision.load(std::memory_order_acquire);
        presetIndex = factoryIndex(p);
        presetName = presetIndex >= 0 ? factoryName(presetIndex) : "CUSTOM";
    }
    void markCustom() { presetIndex = -1; presetName = "CUSTOM"; }
    void selectPreset(int index) {
        if (index < 0 || index >= factoryCount || !queueGuiFactoryPreset(p, index))
            return;
        presetIndex = index;
        presetName = factoryName(index);
    }
    void randomize() {
        if (randomizeVoice(p, randomState)) markCustom();
    }

    // The file operation is separate from the picker so Unicode paths, corrupt
    // files, and Cocoa/VSTGUI state interchange can be exercised without dialogs.
    bool presetFile(const std::string& path, bool save) {
        auto* file = foundation::openFileUtf8(path.c_str(), save ? "wb" : "rb");
        if (!file) return false;
        bool result = false;
        if (save) {
            clap_ostream_t stream{file, [](const clap_ostream_t* s,
                    const void* data, uint64_t size) -> int64_t {
                return static_cast<int64_t>(std::fwrite(data, 1, size,
                    static_cast<std::FILE*>(s->ctx)));
            }};
            result = stateSave(&p.plugin, &stream);
        } else {
            clap_istream_t stream{file, [](const clap_istream_t* s,
                    void* data, uint64_t size) -> int64_t {
                return static_cast<int64_t>(std::fread(data, 1, size,
                    static_cast<std::FILE*>(s->ctx)));
            }};
            result = loadUserPreset(p, &stream);
        }
        const bool closed = std::fclose(file) == 0;
        if (!result || !closed) return false;
        if (!save) {
            presetIndex = -1;
            observedRevision = p.parameterRevision.load(std::memory_order_acquire);
        }
        presetName = foundation::pathToUtf8(
            foundation::pathFromUtf8(path.c_str()).stem());
        return true;
    }
    void choosePresetFile(bool save) {
        foundation::FileDialogOptions options;
        options.save = save;
        options.title = std::string(save ? "Save " : "Load ") + pluginName + " Preset";
        options.extension = "s3gpreset";
        options.extensionDescription = "s3g Preset";
        options.defaultSaveName = "Preset.s3gpreset";
        options.initialDirectory = foundation::presetDirectory(pluginName);
        const auto path = foundation::runFileDialog(getFrame(), options);
        if (path.empty()) return;
        fileError = presetFile(path, save) ? "" : "PRESET FILE ERROR";
    }
    static CRect box(s3g::gui_layout::Rect r) {
        return rect(r.x, r.y, r.width, r.height);
    }
    void popup(CRect anchor, const std::vector<std::string>& items, int selected,
            std::function<void(int)> apply, int columns = 1) {
        // Cocoa dropdowns use 18px rows, a 2px gap, and the control's width.
        const auto height = 18. * ((items.size() + columns - 1) / columns);
        double y = anchor.bottom + 2.;
        if (y + height > kGuiHeight) y = anchor.top - 2. - height;
        openPopup(anchor, items, selected, std::move(apply), columns,
            rect(anchor.left, y, anchor.getWidth(), height), 18.);
    }
    void paintTitle(float activity) {
        const auto band = s3g::gui_layout::encoderTitleBand({kGuiWidth, kGuiHeight});
        const auto menuBounds = box(band.presetMenu);
        foundation::drawPluginTitle(*context, pluginTitle,
            rect(band.titleX, 13., band.presetLabelX - band.titleX - 10., 15.), titleFont);
        text("PRESET", rect(band.presetLabelX, 13., 60., 15.), style.label);
        foundation::drawMenuBox(*context, menuBounds, upper(presetName), font, style);
        hit(menuBounds, [this, menuBounds] {
            std::vector<std::string> items;
            for (int i = 0; i < factoryCount; ++i) items.emplace_back(factoryName(i));
            popup(menuBounds, items, presetIndex, [this](int i) { selectPreset(i); });
        });
        const auto action = [this](CRect bounds, const char* label, std::function<void()> callback) {
            // Match the measured Cocoa header fill, not the brighter general
            // foundation button default. Cocoa NSFrameRect uses its fill color.
            foundation::drawButton(*context, bounds, label, font, false, color(0x2b2b2b));
            hit(bounds, std::move(callback));
        };
        action(box(band.loadButton), "LOAD", [this] { choosePresetFile(false); });
        action(box(band.saveButton), "SAVE", [this] { choosePresetFile(true); });
        action(box(band.randomButton), "RANDOM", [this] { randomize(); });
        text(fileError.empty() ? format("LEVEL %3.0f%%", activity * 100.f) : fileError,
            rect(650., 13., kGuiWidth - 650. - band.statusRightInset, 15.),
            style.value, kRightText);
    }
    void paintRows() {
        const auto& metrics = s3g::gui_layout::kStandardMetrics;
        for (const auto& row : kUiRows) {
            const auto id = row.id;
            const double x = s3g::gui_layout::processorControlX(row.panelX);
            const double width = s3g::gui_layout::processorTrackWidth(row.panelWidth);
            const auto bounds = rect(row.panelX + metrics.hitInset, row.y - 9.,
                row.panelWidth - 2. * metrics.hitInset, metrics.hitHeight);
            char display[64]{};
            paramsValueToText(&p.plugin, id, paramValue(p, id), display, sizeof(display));
            const bool midi = id == kMidiReceiveParamId;
            const double labelWidth = x - s3g::gui_layout::processorLabelX(row.panelX) - 4.;
            // The Windows foundation uses a larger font. Fit long labels such
            // as RECONSTRUCTION without moving the original slider or clipping.
            const auto labelFont = fittedDrumLabelFont(*context, font, row.label, labelWidth);
            text(row.label, rect(s3g::gui_layout::processorLabelX(row.panelX),
                row.y - (midi ? 1. : 2.), labelWidth, 15.),
                style.label, kLeftText, labelFont);
            if (midi) {
                const auto menuBounds = rect(x, row.y - 1.,
                    s3g::gui_layout::processorMenuWidth(row.panelWidth), 15.);
                foundation::drawMenuBox(*context, menuBounds, display, font, style);
                hit(bounds, [this, menuBounds, id] {
                    std::vector<std::string> items;
                    for (int i = 0; i <= 16; ++i) {
                        char label[32]{};
                        s3g::drum_midi::valueToText(i, label, sizeof(label));
                        items.emplace_back(label);
                    }
                    popup(menuBounds, items, static_cast<int>(std::lround(paramValue(p, id))),
                        [this, id](int i) { queueGuiParamGesture(p, id, i); }, 2);
                });
                continue;
            }
            foundation::drawHorizontalSlider(*context, rect(x, row.y + 1., width, 9.),
                uiNormalizedValue(id, paramValue(p, id)), row.y - 1., 13., style);
            const auto value = fittedDrumReadout(*context, font, display, metrics.processorValueWidth);
            // Some count/unit strings (e.g. "12 modes") still exceed the
            // original value cell after numeric compaction. Fit the face as a
            // final fallback rather than clipping a leading digit to "2".
            const auto valueFont = fittedDrumLabelFont(*context, font,
                value.c_str(), metrics.processorValueWidth);
            text(value, rect(s3g::gui_layout::processorValueX(row.panelX, row.panelWidth),
                row.y - 2., metrics.processorValueWidth, 15.), style.value, kRightText, valueFont);
            const auto custom = [this, id] { if (id != kOutputParamId) markCustom(); };
            drag(bounds, [this, id, x, width](CPoint point) {
                queueGuiParamValue(p, id, uiValueFromNormalized(id, (point.x - x) / width));
            }, [this, id, custom] { queueGuiParamGestureBegin(p, id); custom(); },
               [this, id] { queueGuiParamGestureEnd(p, id); },
               [this, id, custom] {
                   queueGuiParamGesture(p, id, paramDef(id)->defaultValue); custom();
               });
        }
    }
    void pad(CRect bounds, const char* label, bool active, std::function<void()> action) {
        // Match the rendered Cocoa pad: NSFrameRect uses the current fill, so
        // its setStroke call does not produce a contrasting border. Values are
        // the calibrated-gray equivalents measured in the Cocoa reference.
        fill(bounds, color(active ? 0x3f3f3f : 0x1b1b1b));
        text(label, bounds, style.value, kCenterText);
        hit(bounds, std::move(action));
    }
    Plugin& p;
    int presetIndex = -1;
    std::string presetName;
    uint64_t observedRevision = 0;
    uint32_t randomState = 0;
    std::string fileError;
};
