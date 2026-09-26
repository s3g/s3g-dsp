namespace neon_midi_gui {
using namespace s3g::portable_gui::canvas;
namespace foundation = s3g::portable_gui::foundation;

class Editor final : public View {
public:
    explicit Editor(Plugin& instance) : View(kGuiWidth, kGuiHeight), p(instance) {}
    void change(clap_id id, double value) {
        using namespace s3g::clap_gui;
        const std::array<ParamEvent, 3u> edit {{
            {ParamEventKind::GestureBegin, id, 0.}, {ParamEventKind::Value, id, value},
            {ParamEventKind::GestureEnd, id, 0.}}};
        if (!p.guiParamEvents.pushBatch(edit.data(), static_cast<uint32_t>(edit.size()))) return;
        if (p.hostParams && p.hostParams->request_flush) p.hostParams->request_flush(p.host);
        if (p.host && p.host->request_process) p.host->request_process(p.host);
    }
    void choice(CRect bounds, clap_id id, const std::vector<std::string>& options, int selected) {
        foundation::drawMenuBox(*context, bounds, options[static_cast<size_t>(selected)], font);
        hit(bounds, [this, bounds, id, options, selected] {
            openPopup(bounds, options, selected,
                [this, id](int item) { change(id, id == kChannel ? item + 1. : item); }, 1, {}, 18.);
        });
    }
    void paint() override {
        foundation::drawPluginTitle(*context, kName, rect(16, 12, 370, 20), titleFont);
        button(rect(490, 12, 114, 20), "RELEASE HELD", [this] { change(kPanic, 1.); });
        panel(rect(16, 46, 588, 76), "NOTE ROUTING");
        const auto bank = static_cast<uint8_t>(getValue(p, kBank));
        const auto base = static_cast<uint8_t>(getValue(p, kBase));
        const auto channel = static_cast<int>(getValue(p, kChannel));
        text("BANK", rect(28, 82, 36, 20), style.label);
        for (uint8_t b = 0u; b < 4u; ++b)
            button(rect(68 + b * 34, 82, 26, 20), format("%c", 'A' + b),
                [this, b] { change(kBank, b); }, b == bank);
        text("BASE NOTE", rect(232, 82, 68, 20), style.label);
        const auto baseBox = rect(304, 84.5, 56, 15);
        fill(baseBox, style.cell);
        text(format("%u", base), baseBox, style.value, kCenterText);
        number(baseBox, base, [this](double v) { change(kBase, std::round(std::clamp(v, 0., 96.))); });
        text("OUT CH", rect(392, 82, 54, 20), style.label);
        std::vector<std::string> channels;
        for (int ch = 1; ch <= 16; ++ch) channels.push_back(format("CH %02d", ch));
        choice(rect(454, 84.5, 138, 15), kChannel, channels, channel - 1);

        static constexpr std::array<const char*, 8u> pages {{
            "SAMPLE / PERFORMANCE", "CHOP / CONTROLS", "EDIT / CONTROLS", "RESAMPLE / CONTROLS",
            "SAMPLE FX / CONTROLS", "CHOP TOOLS / CONTROLS", "EDIT TOOLS / CONTROLS", "RESAMPLE PADS / CONTROLS"}};
        text(pages[std::min<size_t>(p.page.load(), 7u)], rect(16, 130, 330, 18), style.dim);
        choice(rect(360, 131.5, 244, 15), kRoute,
            {"NOTES ONLY", "TRACKER + SAMPLE NEON"}, static_cast<int>(getValue(p, kRoute)));
        const auto held = p.heldCells.load();
        for (uint8_t pad = 0u; pad < 8u; ++pad) {
            const auto cell = static_cast<uint8_t>(bank * 8u + pad);
            const auto bounds = rect(16 + (pad % 4u) * 150, 158 + (pad / 4u) * 66, 138, 54);
            const bool active = (held & (uint32_t{1} << cell)) != 0u;
            fill(bounds, active ? style.buttonActive : style.cell);
            stroke(bounds, active ? style.accent : color(0x3a3a3a));
            text(format("%c%u", 'A' + bank, pad + 1u), rect(bounds.left + 10, bounds.top + 7, 45, 18), style.text);
            text(format("%u", base + cell), rect(bounds.left + 66, bounds.top + 7, 62, 18), style.value, kRightText);
            text(active ? "HELD" : "NOTE", rect(bounds.left + 10, bounds.top + 31, 118, 14), style.dim, kLeftText, smallFont);
        }
        line({16, 293}, {604, 293}, color(0x3a3a3a));
        const auto last = p.lastHit.load();
        const auto velocity = last & 0x7fu;
        text(last == UINT32_MAX ? "NOTE -- / VEL --- / VOL -.---"
            : format("NOTE %u / VEL %03u / VOL %.3f", last >> 8u, velocity, velocity / 127.0),
            rect(16, 300, 272, 20), style.value);
        const auto rejected = p.rejected.load();
        text(rejected ? format("HOST REJECTED %u EVENTS", rejected) : "SAMPLE LAYER 1 ONLY",
            rect(292, 300, 202, 20), style.dim, kCenterText);
        text("v0.1.1", rect(500, 300, 104, 20), style.dim, kRightText);
        text("SHIFT + SAMPLER: VELOCITY ON / OFF", rect(16, 322, 480, 16), style.label);
    }
private:
    Plugin& p;
};
} // namespace neon_midi_gui
s3g::portable_gui::foundation::ContentView* makePortableCanvas(Plugin& p) {
    return new neon_midi_gui::Editor(p);
}
