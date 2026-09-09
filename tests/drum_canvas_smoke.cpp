// Actual editor input -> original CLAP event queue -> original synthesis/state.
#if defined(S3G_TEST_DRUM_SOURCE)
#include S3G_TEST_DRUM_SOURCE
#elif defined(S3G_TEST_DRUM_HI_HAT)
#include "../plugins/clap_drum_hi_hat/s3g_drum_hi_hat_clap.cpp"
#else
#include "../plugins/clap_drum_kick/s3g_drum_kick_clap.cpp"
#endif
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>

namespace drum_test {
using namespace VSTGUI;
namespace foundation = s3g::portable_gui::foundation;
constexpr clap_id kFirstToneParamId = kUiRows.front().id;
bool ok = true;
void expect(bool value, const std::string& message) {
    if (!value) { ok = false; std::cerr << message << '\n'; }
}
void checkSharedPluginTitles() {
    const std::pair<const char*, const char*> cases[] {
        {"s3g Sample Player 2", "s3g SAMPLE PLAYER 2"},
        {"s3g Sample Slicer 16", "s3g SAMPLE SLICER 16"},
        {"s3g Processor Ambi Grain 16", "s3g PROCESSOR AMBI GRAIN 16"},
        {"s3g DRUM HI-HAT", "s3g DRUM HI-HAT"},
        {"S3G Macro Delay 24ch", "s3g MACRO DELAY 24CH"},
        {"s3g Macro Shred Mono", "s3g MACRO SHRED MONO"},
        {"s3g", "s3g"}, {"", ""},
        {"s3g Caf\xc3\xa9 2", "s3g CAF\xc3\xa9 2"},
    };
    for (const auto& item : cases) {
        const std::string source = item.first;
        const auto title = foundation::pluginTitleText(source);
        expect(title == item.second, "shared GUI title casing");
        expect(source == item.first, "title formatting leaves source names unchanged");
        expect(foundation::pluginTitleText(title) == title, "title formatting is idempotent");
    }
    expect(foundation::palette().text == foundation::color(0xd3d3d3), "shared title gray");
    // Compare actual title pixels against explicit expected spelling/color at
    // both platform font sizes, including the existing title position.
    for (double size : {10.5, 11.5}) {
        const auto font = foundation::makeUiFont(size);
        const auto draw = [&](bool shared) {
            auto context = COffscreenContext::create({320., 40.});
            expect(bool(context), "title offscreen context");
            if (!context) return std::vector<uint8_t>{};
            context->beginDraw();
            context->setFillColor(foundation::palette().background);
            context->drawRect(foundation::rect(0., 0., 320., 40.), kDrawFilled);
            const auto bounds = foundation::rect(18., 10., 280., 15.);
            if (shared)
                foundation::drawPluginTitle(*context, "s3g Sample Slicer 16", bounds, font);
            else
                foundation::drawTextInRect(*context, "s3g SAMPLE SLICER 16", bounds,
                    foundation::color(0xd3d3d3), font, kLeftText);
            context->endDraw();
            return getPlatformFactory().createBitmapMemoryPNGRepresentation(
                context->getBitmap()->getPlatformBitmap());
        };
        const auto actual = draw(true);
        expect(!actual.empty() && actual == draw(false), "shared title rendering matches reference");
    }
}
struct Events {
    struct Event { uint16_t type; clap_id id; double value; };
    std::vector<Event> values;
    clap_output_events_t output{this, [](const clap_output_events_t* out, const clap_event_header_t* e) {
        auto& v = static_cast<Events*>(out->ctx)->values;
        if (e->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* p = reinterpret_cast<const clap_event_param_value_t*>(e);
            v.push_back({e->type, p->param_id, p->value});
        } else if (e->type == CLAP_EVENT_PARAM_GESTURE_BEGIN || e->type == CLAP_EVENT_PARAM_GESTURE_END) {
            v.push_back({e->type, reinterpret_cast<const clap_event_param_gesture_t*>(e)->param_id, 0.});
        }
        return true;
    }};
    void balanced() const {
        std::map<clap_id, int> active;
        for (const auto& e : values) {
            if (e.type == CLAP_EVENT_PARAM_GESTURE_BEGIN) expect(++active[e.id] == 1, "nested gesture");
            else if (e.type == CLAP_EVENT_PARAM_GESTURE_END) expect(--active[e.id] == 0, "unbalanced end");
            else expect(active[e.id] == 1, "value outside gesture");
        }
        for (auto a : active) expect(a.second == 0, "unterminated gesture");
    }
};
void click(drum_canvas::Editor& v, double x, double y, int count = 1) {
    MouseDownEvent down; down.mousePosition = {x, y}; down.clickCount = count;
    down.buttonState.add(MouseButton::Left); v.onMouseDownEvent(down);
    MouseUpEvent up; up.mousePosition = {x, y}; v.onMouseUpEvent(up);
}
void beginDrag(drum_canvas::Editor& v, CPoint point) {
    MouseDownEvent down; down.mousePosition = point;
    down.buttonState.add(MouseButton::Left); v.onMouseDownEvent(down);
}
void moveDrag(drum_canvas::Editor& v, CPoint point) {
    MouseMoveEvent move; move.mousePosition = point;
    move.buttonState.add(MouseButton::Left); v.onMouseMoveEvent(move);
}
void endDrag(drum_canvas::Editor& v) { MouseUpEvent up; v.onMouseUpEvent(up); }
std::filesystem::path captureDirectory() {
    const char* path = std::getenv("S3G_DRUM_CAPTURE_DIR");
    return path && path[0] ? foundation::pathFromUtf8(path) : std::filesystem::path{};
}
std::vector<uint8_t> render(drum_canvas::Editor& v, const std::string& name = "") {
    auto ctx = COffscreenContext::create({kGuiWidth, kGuiHeight});
    expect(bool(ctx), "offscreen context");
    if (!ctx) return {};
    ctx->beginDraw(); v.draw(ctx); ctx->endDraw();
    auto png = getPlatformFactory().createBitmapMemoryPNGRepresentation(ctx->getBitmap()->getPlatformBitmap());
    const auto directory = captureDirectory();
    if (!directory.empty() && !name.empty()) {
        std::filesystem::create_directories(directory);
        std::ofstream out(directory / (std::string(drum_canvas::pluginName) + "-" + name + ".png"), std::ios::binary);
        out.write(reinterpret_cast<const char*>(png.data()), png.size());
        expect(bool(out), "write reference PNG");
    }
    return png;
}
void captureCocoa(Plugin& p, const char* name) {
#if defined(__APPLE__)
    const auto directory = captureDirectory();
    if (directory.empty()) return;
#if defined(S3G_TEST_DRUM_COCOA_VIEW)
    auto* view = [[S3G_TEST_DRUM_COCOA_VIEW alloc] initWithPlugin:&p];
#elif defined(S3G_TEST_DRUM_HI_HAT)
    auto* view = [[S3GDrumHiHatView alloc] initWithPlugin:&p];
#else
    auto* view = [[S3GDrumKickView alloc] initWithPlugin:&p];
#endif
    const auto path = foundation::pathToUtf8(directory /
        (std::string(drum_canvas::pluginName) + "-cocoa-" + name + ".pdf"));
    expect([[view dataWithPDFInsideRect:[view bounds]]
        writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES], "write Cocoa reference");
    [view release];
#endif
}
struct Audio {
    std::array<std::array<float, 256>, 2> samples{};
    float* channels[2]{samples[0].data(), samples[1].data()};
    double run(const clap_plugin_t* plugin, const clap_input_events_t* events = nullptr) {
        clap_audio_buffer_t output{}; output.data32 = channels; output.channel_count = 2;
        clap_process_t block{}; block.frames_count = 256; block.audio_outputs = &output;
        block.audio_outputs_count = 1; block.in_events = events;
        Events captured; block.out_events = &captured.output;
        expect(plugin->process(plugin, &block) != CLAP_PROCESS_ERROR, "process audio");
        captured.balanced();
        double energy = 0.;
        for (auto& ch : samples) for (float sample : ch) { expect(std::isfinite(sample), "finite audio"); energy += sample * sample; }
        return energy;
    }
};
struct Note {
    clap_event_note_t note{};
    clap_input_events_t input{this,
        [](const clap_input_events_t*) -> uint32_t { return 1; },
        [](const clap_input_events_t* in, uint32_t) -> const clap_event_header_t* {
            return &static_cast<const Note*>(in->ctx)->note.header;
        }};
    Note(int channel, int key) {
        note.header.size = sizeof(note); note.header.type = CLAP_EVENT_NOTE_ON;
        note.note_id = -1; note.channel = channel; note.port_index = 0;
        note.key = key; note.velocity = .8;
    }
};
} // namespace drum_test

int main() {
    using namespace drum_test;
    if (!foundation::acquireRuntime()) return 2;
    const bool fallback = std::getenv("S3G_DRUM_EXPECT_FONT_FALLBACK") != nullptr;
    expect(foundation::usingBundledFont() != fallback, "expected bundled/fallback font source");
    expect(!foundation::selectedFontFamily().empty(), "usable font family");
    checkSharedPluginTitles();
    {
        auto context = COffscreenContext::create({kGuiWidth, kGuiHeight});
        context->beginDraw();
        // Cover both foundation font sizes even in the macOS test runner.
        for (double fontSize : {10., 11.}) {
            auto font = foundation::makeUiFont(fontSize);
            for (const char* original : {"1180.0 Hz", "8000.0 Hz", "0.125 s", "-36.0 dB", "+100%", "12 modes", "16 modes", "8 hands"}) {
                const auto fitted = drum_canvas::fittedDrumReadout(*context, font, original, 42.);
                const auto face = drum_canvas::fittedDrumLabelFont(*context, font, fitted.c_str(), 42.);
                context->setFont(face);
                expect(context->getStringWidth(fitted.c_str()) <= 42.1, "readout fits without clipping");
                const double value = std::strtod(original, nullptr);
                expect(std::abs(std::strtod(fitted.c_str(), nullptr) - value) <= std::max(.001, std::abs(value) * .01),
                    "fitted readout retains magnitude");
            }
            for (const auto& row : kUiRows) {
                const double width = s3g::gui_layout::processorControlX(row.panelX)
                    - s3g::gui_layout::processorLabelX(row.panelX) - 4.;
                const auto fitted = drum_canvas::fittedDrumLabelFont(*context, font, row.label, width);
                context->setFont(fitted);
                expect(context->getStringWidth(row.label) <= width + .1, "full label fits at Mac/Windows font size");
            }
        }
        context->endDraw();
    }
    clap_host_t host{}; host.clap_version = CLAP_VERSION_INIT;
    host.name = "Drum canvas parity"; host.vendor = "s3g"; host.url = ""; host.version = "1";
    host.get_extension = [](const clap_host_t*, const char*) -> const void* { return nullptr; };
    host.request_process = [](const clap_host_t*) {};
    host.request_callback = [](const clap_host_t*) {};
    const auto* plugin = create(&host);
    expect(plugin && plugin->init(plugin), "initialize plugin");
    if (!plugin) return 2;
    auto& p = *self(plugin);
    const auto directory = std::filesystem::temp_directory_path() /
        ("s3g-drum-parity-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    {
        drum_canvas::Editor v(p);
        const auto flush = [&] { Events e; paramsFlush(plugin, nullptr, &e.output); e.balanced(); v.service(); return e; };
        const auto idle = render(v, "idle");
        captureCocoa(p, "idle");
        p.visualActivity.store(.65f);
#if !defined(S3G_TEST_DRUM_KICK) && !defined(S3G_TEST_DRUM_SNARE)
        p.visualTriggerCode.store(2u);
#endif
        expect(render(v, "active") != idle, "DSP activity changes graphic and pad");
        captureCocoa(p, "active");
        p.visualActivity.store(0.f);

        // Every Cocoa row retains its hit map, logarithmic/linear transfer,
        // default reset, and one continuous gesture in both drag directions.
        for (const auto& row : kUiRows) {
            if (row.id == kMidiReceiveParamId) continue;
            render(v);
            const double x = s3g::gui_layout::processorControlX(row.panelX);
            const double width = s3g::gui_layout::processorTrackWidth(row.panelWidth);
            beginDrag(v, {x, row.y + 5.});
            expect(std::abs(paramValue(p, row.id) - paramDef(row.id)->minimum) < 1e-6, "slider minimum " + std::string(row.label));
            moveDrag(v, {x + width, row.y + 5.});
            expect(std::abs(paramValue(p, row.id) - paramDef(row.id)->maximum) < 1e-6, "slider maximum " + std::string(row.label));
            moveDrag(v, {x + width * .37, row.y + 5.}); endDrag(v);
            expect(std::abs(paramValue(p, row.id) - uiValueFromNormalized(row.id, .37)) < 1e-6, "slider transfer");
            const auto e = flush(); expect(e.values.size() == 5, "one begin/three values/end");
            click(v, x, row.y + 5., 2); flush();
            expect(std::abs(paramValue(p, row.id) - paramDef(row.id)->defaultValue) < 1e-6, "double-click default");
        }
        // MIDI's two-column popup and full-row click target, including CH 16.
        const auto midiRow = std::find_if(kUiRows.begin(), kUiRows.end(),
            [](const auto& row) { return row.id == kMidiReceiveParamId; });
        expect(midiRow != kUiRows.end(), "MIDI row exists");
        const double midiX = s3g::gui_layout::processorControlX(midiRow->panelX);
        const double midiWidth = s3g::gui_layout::processorMenuWidth(midiRow->panelWidth);
        double midiPopupY = midiRow->y + 16.;
        if (midiPopupY + 162. > kGuiHeight) midiPopupY = midiRow->y - 165.;
        render(v); click(v, midiX, midiRow->y + 5.); render(v, "midi-menu");
        // The original menu is row-major: CH 16 is the last left-column item.
        click(v, midiX + midiWidth * .25, midiPopupY + 8 * 18. + 9.); flush();
        expect(paramValue(p, kMidiReceiveParamId) == 16., "MIDI popup CH 16");
        render(v); click(v, midiX, midiRow->y + 5.); click(v, 40., 40.); flush();
        expect(paramValue(p, kMidiReceiveParamId) == 16., "outside click dismisses without changing MIDI");

        for (int i = 0; i < drum_canvas::factoryCount; ++i) {
            render(v); click(v, 350., 20.);
            if (i == 0) {
                const auto menu = render(v, "factory-menu");
                MouseMoveEvent hover; hover.mousePosition = {350., 57.};
                v.onMouseMoveEvent(hover);
                expect(render(v, "factory-hover") != menu, "custom menu hover feedback");
            }
            click(v, 350., 30. + 18. * i + 9.); flush();
            expect(v.presetIndex == i && v.presetName == drum_canvas::factoryName(i), "factory preset " + std::to_string(i));
            expect(paramValue(p, kMidiReceiveParamId) == 16., "factory preserves MIDI routing");
        }
        queueGuiParamGesture(p, kNoteTrackingParamId, .371);
        queueGuiParamGesture(p, kVelocityParamId, .427);
        queueGuiParamGesture(p, kOutputParamId, -17.25); flush();
        const double trackingBefore = paramValue(p, kNoteTrackingParamId);
        const double velocityBefore = paramValue(p, kVelocityParamId);
        render(v); click(v, 590., 20.); const auto randomized = flush();
        expect(randomized.values.size() == (kSavedParamCount - 4u) * 3u,
            "all timbral parameters randomized with complete gestures");
        expect(paramValue(p, kNoteTrackingParamId) == trackingBefore &&
            paramValue(p, kVelocityParamId) == velocityBefore &&
            paramValue(p, kOutputParamId) == -17.25 && paramValue(p, kMidiReceiveParamId) == 16.,
            "RANDOM preserves performance, output, routing");
        expect(v.presetIndex == -1, "RANDOM marks CUSTOM");
        // Queue saturation rejects the entire RANDOM publication.
        const double before = paramValue(p, kFirstToneParamId);
        while (p.guiParamEvents.push({s3g::clap_gui::ParamEventKind::Value, kFirstToneParamId, before})) {}
        v.randomize(); expect(paramValue(p, kFirstToneParamId) == before, "full queue rejects random atomically");
        s3g::clap_gui::ParamEvent pending{};
        while (p.guiParamEvents.peek(pending)) p.guiParamEvents.pop();

        const auto preset = foundation::pathToUtf8(directory / std::filesystem::u8path(u8"音 démo.s3gpreset"));
        v.selectPreset(1); flush();
        const double savedTune = paramValue(p, kFirstToneParamId);
        const double savedGain = paramValue(p, kOutputParamId);
        expect(v.presetFile(preset, true), "save Unicode preset");
        queueGuiParamGesture(p, kFirstToneParamId, paramDef(kFirstToneParamId)->maximum);
        queueGuiParamGesture(p, kOutputParamId, -21.); flush();
        expect(v.presetFile(preset, false), "load Unicode preset");
        expect(paramValue(p, kFirstToneParamId) == savedTune && paramValue(p, kOutputParamId) == -21., "preset LOAD restores voice but preserves output");
        expect(v.presetName == u8"音 démo", "user preset name");
        const auto bad = directory / "invalid.s3gpreset";
        { std::ofstream out(bad); out << "not a state"; }
        expect(!v.presetFile(foundation::pathToUtf8(bad), false) && paramValue(p, kFirstToneParamId) == savedTune,
            "invalid preset leaves voice intact");
        // Project recall does NOT preserve output trim, including after bad LOAD.
        auto* file = foundation::openFileUtf8(preset.c_str(), "rb");
        clap_istream_t stream{file, [](const clap_istream_t* in, void* dst, uint64_t n) -> int64_t {
            return std::fread(dst, 1, n, static_cast<FILE*>(in->ctx));
        }};
        expect(stateLoad(plugin, &stream), "original project-state reader"); std::fclose(file);
        expect(paramValue(p, kOutputParamId) == savedGain, "project recall restores saved gain");

        expect(plugin->activate(plugin, 48000., 256, 256) && plugin->start_processing(plugin), "activate synthesis");
        Audio audio;
        Note rejected(0, 36), accepted(15, 36);
        plugin->reset(plugin);
        expect(audio.run(plugin, &rejected.input) == 0., "MIDI channel rejection");
        expect(audio.run(plugin, &accepted.input) > 0., "MIDI channel acceptance");
        const int pads = static_cast<int>(paramDef(kTriggerParamId)->maximum);
        for (int pad = 0; pad < pads; ++pad) {
            plugin->reset(plugin); render(v);
#if defined(S3G_TEST_DRUM_CRASH)
            if (pad == 1) {
                queueGuiTrigger(p, 1u);
                expect(audio.run(plugin) > 0., "excite crash before choke");
            }
#endif
#if defined(S3G_TEST_DRUM_KICK) || defined(S3G_TEST_DRUM_HI_HAT)
            click(v, pads == 1 ? 687. : 500. + pad * ((kPanelWidth - 36.) / 3. + 6.), 492.);
#else
            const auto center = drum_canvas::performancePadRect(pad + 1u).getCenter();
            click(v, center.x, center.y);
#endif
            expect(audio.run(plugin) > 0. && p.visualActivity.load() > 0., "GUI pad produces audio and activity despite channel filter");
#if !defined(S3G_TEST_DRUM_KICK) && !defined(S3G_TEST_DRUM_SNARE)
            expect(p.visualTriggerCode.load() == uint32_t(pad + 1), "correct articulation indicator");
#endif
            render(v, "pad-" + std::to_string(pad));
        }
        plugin->stop_processing(plugin); plugin->deactivate(plugin);
        render(v); beginDrag(v, {160., 85.}); v.stopRefresh();
        flush(); // closing/hiding an editor mid-drag ends the gesture
    }
    plugin->destroy(plugin);
    std::filesystem::remove_all(directory); // unique test-owned directory only
    foundation::releaseRuntime();
    if (ok) std::cout << drum_canvas::pluginName << " canvas parity passed\n";
    return ok ? 0 : 1;
}
