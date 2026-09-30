namespace neon_midi_gui {
using namespace s3g::portable_gui::canvas;
namespace foundation = s3g::portable_gui::foundation;

class Editor final : public View {
public:
    explicit Editor(Plugin& instance) : View(kGuiWidth, kGuiHeight, true), p(instance) {}
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
        foundation::drawPluginTitle(*context, kName, rect(16, 12, 248, 20), titleFont);
        button(rect(272,12,92,20),"KEYBOARD",[this] { keyboardOpen = !keyboardOpen; noteEditor.open = false; },keyboardOpen);
        button(rect(490, 12, 114, 20), "RELEASE HELD", [this] { change(kPanic, 1.); });
        s3g::controller::neon_midi::NoteMap map; p.noteMap.read(map);
        button(rect(372, 12, 110, 20), "PAD NOTE MAP", [this, map] {
            noteEditor.begin(map, static_cast<unsigned>(getValue(p,kBase)));
        }, noteEditor.open && !noteEditor.keyboard);
        if (noteEditor.open) {
            panel(rect(16,46,588,386), noteEditor.keyboard ? format("KEYBOARD / UNIT %u / MANUAL NOTES",manualUnit+1) : "PAD NOTE MAP / MIDI 0-127");
            noteEditor.paint(*this,32,82,556,[this](auto value) {
                if (noteEditor.keyboard) {
                    value.custom=true; p.keyboardMaps[manualUnit].store(value);
                    change(manualUnit ? kLayout2 : kLayout,2);
                } else { p.noteMap.store(value); p.panicRequested.store(true); }
                ::notify(p);
                if (p.host && p.host->request_process) p.host->request_process(p.host);
            });
            return;
        }
        if (keyboardOpen) { paintKeyboard(); return; }
        panel(rect(16, 46, 588, 76), "NOTE ROUTING");
        const auto unit = static_cast<unsigned>(getValue(p, kUnit));
        const auto bankId = unit ? kBank2 : kBank;
        const auto bank = static_cast<uint8_t>(getValue(p, bankId));
        const auto base = static_cast<uint8_t>(getValue(p, kBase));
        const auto channel = static_cast<int>(getValue(p, kChannel));
        const bool keyboard = getValue(p,unit ? kRole2 : kRole) > .5;
        const auto keys = resolvedKeys(unit);
        text("BANK", rect(28, 82, 36, 20), style.label);
        for (uint8_t b = 0u; b < 4u; ++b)
            button(rect(68 + b * 34, 82, 26, 20), format("%c", 'A' + b),
                [this, b, bankId] { change(bankId, b); }, b == bank);
        text("BASE NOTE", rect(232, 82, 68, 20), style.label);
        const auto baseBox = rect(304, 84.5, 56, 15);
        fill(baseBox, style.cell);
        text(map.custom ? "CUSTOM" : format("%u", base), baseBox, style.value, kCenterText);
        number(baseBox, base, [this](double v) { change(kBase, std::round(std::clamp(v, 0., 96.))); }, !map.custom);
        text("OUT CH", rect(392, 82, 54, 20), style.label);
        std::vector<std::string> channels;
        if (unit) channels.push_back("FOLLOW UNIT 1");
        for (int ch = 1; ch <= 16; ++ch) channels.push_back(format("CH %02d", ch));
        choice(rect(454, 84.5, 138, 15), unit ? kChannel2 : kChannel, channels,
            unit ? static_cast<int>(getValue(p,kChannel2)) : channel - 1);

        static constexpr std::array<const char*, 8u> pages {{
            "SAMPLE / PERFORMANCE", "CHOP / CONTROLS", "EDIT / CONTROLS", "RESAMPLE / CONTROLS",
            "SAMPLE FX / CONTROLS", "CHOP TOOLS / CONTROLS", "EDIT TOOLS / CONTROLS", "RESAMPLE PADS / CONTROLS"}};
        text(pages[std::min<size_t>(p.page[unit].load(), 7u)], rect(16, 130, 330, 18), style.dim);
        choice(rect(360, 131.5, 244, 15), kRoute,
            {"NOTES ONLY", "TRACKER + SAMPLE NEON"}, static_cast<int>(getValue(p, kRoute)));
        const auto held = p.heldCells[unit].load();
        for (uint8_t pad = 0u; pad < 8u; ++pad) {
            const auto cell = static_cast<uint8_t>(bank * 8u + pad);
            const auto bounds = rect(16 + (pad % 4u) * 150, 158 + (pad / 4u) * 66, 138, 54);
            const bool active = (held & (uint32_t{1} << cell)) != 0u;
            fill(bounds, active ? style.buttonActive : style.cell);
            stroke(bounds, active ? style.accent : color(0x3a3a3a));
            text(format("%c%u", 'A' + bank, pad + 1u), rect(bounds.left + 10, bounds.top + 7, 45, 18), style.text);
            const auto key = keyboard ? keys[cell] : map.resolved(base)[cell];
            text(key>127 ? "OFF" : format("%u",key), rect(bounds.left + 66, bounds.top + 7, 62, 18), style.value, kRightText);
            text(active ? "HELD" : "NOTE", rect(bounds.left + 10, bounds.top + 31, 118, 14), style.dim, kLeftText, smallFont);
        }
        line({16, 293}, {604, 293}, color(0x3a3a3a));
        const auto last = p.lastHit[unit].load();
        const auto velocity = last & 0x7fu;
        text(last == UINT32_MAX ? "NOTE -- / VEL ---"
            : format("NOTE %u / VEL %03u", (last >> 8u) & 0x7fu, velocity),
            rect(16, 300, 152, 20), style.value);
        text(last == UINT32_MAX ? "AT ---" : format("AT %03u", (last >> 16u) & 0x7fu),
            rect(176, 300, 56, 20), style.value);
        text(last == UINT32_MAX ? "VOL -.---" : format("VOL %.3f", velocity / 127.0),
            rect(240, 300, 80, 20), style.value);
        const auto rejected = p.rejected.load();
        text(rejected ? format("HOST REJECTED %u EVENTS", rejected)
            : p.usb.errors.load() ? format("USB DROPS / ERR %u", p.usb.errors.load()) : "SAMPLE LAYER 1 ONLY",
            rect(332, 300, 206, 20), style.dim, kCenterText);
        text("v0.5.1", rect(552, 300, 52, 20), style.dim, kRightText);
        text("SHIFT + SAMPLER: VELOCITY ON / OFF", rect(16, 322, 480, 16), style.label);
        panel(rect(16, 346, 588, 86), "CONTROLLERS");
        text("INPUT", rect(32, 377, 54, 20), style.label);
        choice(rect(90, 379.5, 214, 15), kInput,
            {"HOST MIDI", "HOST DUAL PORTS", "USB ONE NEON", "USB TWO NEONS"}, static_cast<int>(getValue(p, kInput)));
        text("VIEW", rect(322, 377, 42, 20), style.label);
        choice(rect(370, 379.5, 218, 15), kUnit, {"UNIT 1", "UNIT 2"}, static_cast<int>(unit));
        const auto uid = p.usb.source[unit].load();
        const auto input = static_cast<int>(getValue(p, kInput));
        text(input < 2 ? format("UNIT %u / HOST PORT %u", unit+1, unit+1)
            : format("U%u %s / %d", unit+1, p.usb.connected[unit].load() ? "USB" : "WAIT", uid),
            rect(32, 405, 342, 18), style.value);
        button(rect(388, 403, 92, 18), "SWAP USB", [this] { p.usb.assignmentRequest.store(1); ::notify(p); });
        button(rect(488, 403, 100, 18), "RELEARN USB", [this] { p.usb.assignmentRequest.store(2); ::notify(p); });
    }
private:
    s3g::controller::neon_midi::PadNotes resolvedKeys(unsigned unit) const {
        s3g::controller::neon_midi::NoteMap manual; p.keyboardMaps[unit].read(manual);
        return s3g::controller::neon_midi::keyboardNotes(static_cast<unsigned>(getValue(p,unit ? kLayout2 : kLayout)),
            static_cast<unsigned>(getValue(p,unit ? kFirstKey2 : kFirstKey)),
            static_cast<unsigned>(getValue(p,unit ? kScale2 : kScale)),manual.notes);
    }
    void paintKeyboard() {
        // Compact 24px rows, shared 15px menus; four bank rows show all keys.
        panel(rect(16,46,588,386),"KEYBOARD / INDEPENDENT UNIT ROLES");
        const unsigned unit = static_cast<unsigned>(getValue(p,kUnit));
        const auto role = unit ? kRole2 : kRole, first = unit ? kFirstKey2 : kFirstKey;
        const auto layoutId = unit ? kLayout2 : kLayout, scaleId = unit ? kScale2 : kScale;
        const unsigned layout=static_cast<unsigned>(getValue(p,layoutId)), scale=static_cast<unsigned>(getValue(p,scaleId));
        text("UNIT",rect(32,82,90,20),style.label);
        choice(rect(134,84.5,454,15),kUnit,{"UNIT 1","UNIT 2"},unit);
        text("PAD ROLE",rect(32,106,90,20),style.label);
        choice(rect(134,108.5,454,15),role,{"PAD CELLS / CUSTOM MAP","KEYBOARD"},static_cast<int>(getValue(p,role)));
        text("LAYOUT",rect(32,130,90,20),style.label);
        choice(rect(134,132.5,454,15),layoutId,{"CHROMATIC","SCALE","MANUAL"},layout);
        text(layout==1 ? "ROOT NOTE" : "FIRST NOTE",rect(32,154,90,20),style.label);
        const auto box = rect(134,156.5,454,15);
        fill(box,style.cell); text(format("%.0f",getValue(p,first)),box,layout==2 ? style.dim : style.value);
        number(box,getValue(p,first),[this,first](double n) { change(first,std::clamp(std::round(n),0.,96.)); },layout!=2);
        text("SCALE",rect(32,178,90,20),style.label);
        const auto scaleBox=rect(134,180.5,454,15);
        auto scaleStyle=style; if(layout!=1) scaleStyle.value=color(0x686868);
        foundation::drawMenuBox(*context,scaleBox,s3g::musicalScaleDefinition(scale).name,font,scaleStyle);
        hit(scaleBox,[this,scaleBox,scale,scaleId] {
            std::vector<std::string> names;
            for(unsigned n=0;n<s3g::kMusicalScaleCount;++n) names.push_back(s3g::musicalScaleDefinition(s3g::musicalScaleValueForMenuIndex(n)).name);
            openPopup(scaleBox,std::move(names),s3g::musicalScaleMenuIndexForValue(scale),
                [this,scaleId](int n) { change(scaleId,s3g::musicalScaleValueForMenuIndex(n)); },2,rect(32,6,556,432),18);
        },layout==1);
        text("OUTPUT CH",rect(32,202,90,20),style.label);
        std::vector<std::string> channels;
        if (unit) channels.push_back("FOLLOW UNIT 1");
        for (unsigned n = 1; n <= 16; ++n) channels.push_back(format("CH %02u",n));
        choice(rect(134,204.5,454,15),unit ? kChannel2 : kChannel,channels,
            static_cast<int>(getValue(p,unit ? kChannel2 : kChannel)) - (unit ? 0 : 1));
        button(rect(32,230,556,20),"EDIT MANUAL NOTES",[this,unit,first,scale] {
            manualUnit=unit;
            s3g::controller::neon_midi::NoteMap manual; p.keyboardMaps[unit].read(manual);
            const auto source=s3g::controller::neon_midi::keyboardNotes(1,static_cast<unsigned>(getValue(p,first)),scale);
            noteEditor.begin(manual,static_cast<unsigned>(getValue(p,first)),&source);
        });
        const auto keys=resolvedKeys(unit);
        for(unsigned bank=0;bank<4;++bank) {
            text(format("%c",'A'+bank),rect(32,262+bank*18,24,18),style.label);
            for(unsigned pad=0;pad<8;++pad) {
                const auto key=keys[bank*8+pad];
                text(key>127 ? "OFF" : format("%u",key),rect(62+pad*66,262+bank*18,62,18),style.value,kCenterText);
            }
        }
        text(layout==2 ? "MANUAL NOTES IGNORE FIRST NOTE AND SCALE." : "A-D CONTINUE THE SCALE; NOTES ABOVE 127 ARE OFF.",rect(32,342,556,18),style.label);
        text("NEON: PIN THIS OUTPUT CHANNEL TO A CHROMATIC PAD.",rect(32,366,556,18),style.label);
        button(rect(388,396,200,20),"BACK TO MONITOR",[this] { keyboardOpen = false; });
    }
#if defined(__APPLE__)
    CTextEdit* makeNumericTextEdit(CRect bounds, const char* text) override {
        return noteEditor.open || keyboardOpen ? s3g::portable_gui::makeReadableNumericTextEdit(bounds,this,1,text)
            : View::makeNumericTextEdit(bounds,text);
    }
#endif
    Plugin& p;
    NeonNoteMapEditor noteEditor;
    bool keyboardOpen = false;
    unsigned manualUnit = 0;
};
} // namespace neon_midi_gui
s3g::portable_gui::foundation::ContentView* makePortableCanvas(Plugin& p) {
    return new neon_midi_gui::Editor(p);
}
