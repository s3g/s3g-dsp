// Compile the real canvas into this harness so its native-independent event
// handlers can be tested without moving the user's mouse or opening dialogs.
#include "../plugins/common/s3g_sample_family_vstgui.cpp"
#include "vstgui/lib/coffscreencontext.h"
#include <iostream>

namespace {
using namespace s3g::portable_gui;
using namespace VSTGUI;
struct Fixture {
    uint32_t slots = 4, begins = 0, ends = 0, recalculated = 99, cleared = 99;
    uint32_t cueDeck = 99;
    float cuePosition = -1;
    std::vector<std::pair<uint32_t, std::string>> loaded;
    std::vector<SampleFamilyParameterInfo> parameters;
    std::vector<double> values;
    void add(const char* name, double value, double maximum = 1)
    {
        SampleFamilyParameterInfo p;
        p.id = static_cast<uint32_t>(parameters.size() + 1);
        p.maximum = maximum;
        std::snprintf(p.name, sizeof(p.name), "%s", name);
        parameters.push_back(p); values.push_back(value);
    }
    SampleFamilyEditorConfig config(SampleFamilyVisualization visualization)
    {
        SampleFamilyEditorConfig c; c.visualization = visualization;
        c.nativeWidth = 1356; c.nativeHeight = 844;
        auto& cb = c.callbacks; cb.context = this;
        cb.getParameterCount = [](void* p) { return static_cast<uint32_t>(static_cast<Fixture*>(p)->parameters.size()); };
        cb.getParameterInfo = [](void* p, uint32_t i, SampleFamilyParameterInfo* out) {
            *out = static_cast<Fixture*>(p)->parameters[i]; return true;
        };
        cb.getParam = [](void* p, uint32_t id) { return static_cast<Fixture*>(p)->values[id - 1]; };
        cb.setParam = [](void* p, uint32_t id, double value) { static_cast<Fixture*>(p)->values[id - 1] = value; };
        cb.beginParamEdit = [](void* p, uint32_t) { ++static_cast<Fixture*>(p)->begins; };
        cb.endParamEdit = [](void* p, uint32_t) { ++static_cast<Fixture*>(p)->ends; };
        cb.getSampleSlotCount = [](void* p) { return static_cast<Fixture*>(p)->slots; };
        cb.loadSample = [](void* p, uint32_t slot, const char* path) {
            static_cast<Fixture*>(p)->loaded.emplace_back(slot, path); return true;
        };
        cb.clearSample = [](void* p, uint32_t slot) { static_cast<Fixture*>(p)->cleared = slot; return true; };
        cb.recalculateSample = [](void* p, uint32_t slot) { static_cast<Fixture*>(p)->recalculated = slot; };
        return c;
    }
};
struct Files : IDataPackage {
    std::array<std::string, 3> paths {{u8"/tmp/音-a.wav", "/tmp/b.wav", "/tmp/c.wav"}};
    uint32_t getCount() const override { return 3; }
    uint32_t getDataSize(uint32_t i) const override { return static_cast<uint32_t>(paths[i].size() + 1); }
    Type getDataType(uint32_t) const override { return kFilePath; }
    uint32_t getData(uint32_t i, const void*& buffer, Type& type) const override
    { buffer = paths[i].c_str(); type = kFilePath; return getDataSize(i); }
};
void click(SampleFamilyView& view, double x, double y)
{
    MouseDownEvent down; down.mousePosition = {x, y}; down.buttonState.add(MouseButton::Left);
    view.onMouseDownEvent(down);
    MouseUpEvent up; up.mousePosition = {x, y}; view.onMouseUpEvent(up);
}
} // namespace

int main()
{
    if (!foundation::acquireRuntime()) return 2;
    bool ok = true;
    const auto expect = [&](bool value, const char* message) {
        if (!value) { ok = false; std::cerr << message << '\n'; }
    };
    {
        auto context = COffscreenContext::create(CPoint(200, 40));
        expect(bool(context), "value-fitting context unavailable");
        if (context) {
            auto font = foundation::makeUiFont(10);
            context->beginDraw();
            context->setFont(font);
            const double width = context->getStringWidth("90 BPM");
            expect(foundation::sliderValueTextToFit(*context, "90.00 BPM", width, font) == "90 BPM",
                "bounded BPM clipped its leading digit instead of reducing precision");
            expect(foundation::sliderValueTextToFit(*context, "0.000 BEATS",
                    context->getStringWidth("0 BEATS"), font) == "0 BEATS",
                "bounded beat value did not retain its unit");
            expect(foundation::sliderValueTextToFit(*context, "-6.00 DB",
                    context->getStringWidth("-6 DB"), font) == "-6 DB",
                "bounded signed value lost its sign");
            expect(foundation::sliderValueTextToFit(*context, "MIDI OFF", 20, font) == "MIDI OFF",
                "non-numeric inactive explanation was rewritten");
            context->endDraw();
        }
    }
    Files files;
    for (const auto family : {SampleFamilyVisualization::Lanes, SampleFamilyVisualization::Grains,
             SampleFamilyVisualization::Cutups, SampleFamilyVisualization::Rings,
             SampleFamilyVisualization::Circulator}) {
        Fixture f;
        if (family == SampleFamilyVisualization::Circulator) f.slots = 2;
        f.add("Selected Slot", 2, 3);
        SampleFamilyView view(f.config(family));
        if (family == SampleFamilyVisualization::Rings) click(view, 470, 702); // C badge
        const bool rings = family == SampleFamilyVisualization::Rings;
        expect(view.onDrop({&files, rings ? CPoint(428, 378) : CPoint(300, 221), {}}), "drop rejected");
        const uint32_t start = family == SampleFamilyVisualization::Circulator ? 0 : 2;
        expect(f.loaded.size() == 2 && f.loaded[0].first == start && f.loaded[1].first == start + 1
            && f.loaded[0].second == files.paths[0] && f.loaded[1].second == files.paths[1],
            "multi-drop lost order, UTF-8, selected slot, or capacity boundary");
        if (rings) {
            click(view, 1210, 639);
            expect(f.cleared == 2, "Rings CLEAR targeted a non-selected slot");
        }
    }
    {
        Fixture f; f.slots = 1;
        f.add("Motion", 0, 7); f.add("Travel", .4);
        SampleFamilyView view(f.config(SampleFamilyVisualization::Motion));
        click(view, 240, 660);
        expect(f.values[1] == .4 && f.begins == 0, "inactive Travel changed or emitted a gesture");
        f.values[0] = 2;
        click(view, 240, 660);
        expect(f.values[1] > .8 && f.begins == 1 && f.ends == 1, "active Travel did not edit with a balanced gesture");
    }
    {
        Fixture f;
        const SampleFamilyAction actions[] {{1, "PLAY"}, {2, "STOP"}, {4, "RECALC"}};
        auto c = f.config(SampleFamilyVisualization::Cutups);
        c.actions = actions; c.actionCount = 3;
        SampleFamilyView view(c);
        click(view, 65, 284); // D lane
        click(view, 1160, 68); // Recalc in the Cutups header.
        expect(f.recalculated == 3, "Cutups Recalc did not target the selected lane");
    }
    {
        Fixture f; f.slots = 1;
        auto c = f.config(SampleFamilyVisualization::Doubles);
        c.callbacks.getDoublesState = [](void*, SampleFamilyDoublesState* state) {
            state->cueMask = 3; state->cues[0] = .25f; state->cues[1] = .75f; return true;
        };
        c.callbacks.placeDoublesCue = [](void* p, uint32_t deck, float position) {
            auto& f = *static_cast<Fixture*>(p); f.cueDeck = deck; f.cuePosition = position;
        };
        SampleFamilyView view(c);
        MouseDownEvent down({275, 120}, MouseButton::Left); view.onMouseDownEvent(down);
        MouseMoveEvent move({618, 120}, MouseButton::Left); view.onMouseMoveEvent(move);
        MouseUpEvent up({618, 120}, MouseButton::Left); view.onMouseUpEvent(up);
        expect(f.cueDeck == 0 && std::abs(f.cuePosition - .6f) < .001f,
            "Doubles direct cue drag did not retain deck and normalized placement");
    }
    foundation::releaseRuntime();
    return ok ? 0 : 1;
}
