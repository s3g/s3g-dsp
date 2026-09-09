// Literal counterpart of S3GDrumHiHatView::drawRect, including the two cymbals,
// stand, eight rays, clutch, and DSP-driven articulation pad illumination.
namespace drum_canvas {
using namespace s3g::portable_gui::canvas;
namespace foundation = s3g::portable_gui::foundation;
constexpr const char* pluginName = "Drum Hi-Hat";
constexpr const char* pluginTitle = "s3g DRUM HI-HAT";
constexpr int factoryCount = s3g::kDrumHiHatFactoryPresetCount;
const char* factoryName(int index) { return s3g::drumHiHatFactoryPresetInfo(index).name; }
int factoryIndex(const Plugin& p) { return s3g::drumHiHatFactoryPresetIndex(publishedParamsSnapshot(p)); }
bool randomizeVoice(Plugin& p, uint32_t& seed) {
    return queueGuiSafeRandomParamSet(p,
        s3g::drumHiHatSafeRandomParams(publishedParamsSnapshot(p), seed));
}
bool loadUserPreset(Plugin& p, const clap_istream_t* stream) {
    p.preservedOutputValue.store(paramValue(p, kOutputParamId), std::memory_order_release);
    p.preserveOutputOnNextStateLoad.store(true, std::memory_order_release);
    const bool loaded = stateLoad(&p.plugin, stream);
    // Invalid files must not change how a later host project state is restored.
    p.preserveOutputOnNextStateLoad.store(false, std::memory_order_release);
    return loaded;
}
#include "../common/s3g_drum_canvas.inc"
class Editor final : public DrumEditor {
public:
    explicit Editor(Plugin& p) : DrumEditor(p) {}
    void paint() override {
        const float activity = std::clamp(p.visualActivity.load(std::memory_order_relaxed), 0.f, 1.f);
        const uint32_t visualTrigger = p.visualTriggerCode.load(std::memory_order_relaxed);
        paintTitle(activity);
        panel(rect(kLeftPanelX, 50., kPanelWidth, 262.), "METAL / WASH");
        panel(rect(kRightPanelX, 50., kPanelWidth, 262.), "ARTICULATION");
        panel(rect(kLeftPanelX, 344., kPanelWidth, 218.), "SHARED DRUM CHARACTER");
        panel(rect(kRightPanelX, 344., kPanelWidth, 320.), "PERFORMANCE / OUTPUT");
        paintRows();
        const double padWidth = (kPanelWidth - 36.) / 3.;
        const char* labels[] = {"CLOSED", "OPEN", "PEDAL"};
        for (uint32_t i = 0; i < 3; ++i) {
            pad(rect(kRightPanelX + 12. + i * (padWidth + 6.), 480., padWidth, 24.),
                labels[i], visualTrigger == i + 1 && activity > .02f,
                [this, i] { queueGuiTrigger(p, i + 1); });
        }
        const CPoint center(687., 568.);
        for (uint32_t cymbal = 0; cymbal < 2; ++cymbal) {
            const double width = 116. + activity * (18. - cymbal * 4.);
            const double height = 22. + activity * 7.;
            const double offset = cymbal == 0 ? -12. - activity * 5. : 12. + activity * 5.;
            ellipse(rect(center.x - width * .5, center.y + offset - height * .5, width, height),
                rgba(.30 + activity * .38, .62 + activity * .24, .66 + activity * .20,
                    .30 + activity * .52), false, 1. + activity * 2.);
        }
        const auto metal = rgba(.72, .86, .82, .45 + activity * .45);
        line({center.x, center.y - 55.}, {center.x, center.y + 62.}, metal, 1.2);
        for (uint32_t ray = 0; ray < 8; ++ray) {
            const double angle = ray * s3g::kPi / 4.;
            const double radius = 65. + activity * 13.;
            line(center, {center.x + std::cos(angle) * radius,
                center.y + std::sin(angle) * radius * .35}, metal, .7 + activity * .7);
        }
        const double clutchRadius = 4. + activity * 4.;
        ellipse(rect(center.x - clutchRadius, center.y - clutchRadius,
            clutchRadius * 2., clutchRadius * 2.), rgba(.90, .90, .90, .85));
        text("SYNTHESIZED CLOSED  //  OPEN  //  PEDAL",
            rect(kRightPanelX + 64., 644., kPanelWidth - 64., 15.), style.label);
    }
};
} // namespace drum_canvas
s3g::portable_gui::foundation::ContentView* makePortableCanvas(Plugin& p) {
    return new drum_canvas::Editor(p);
}
