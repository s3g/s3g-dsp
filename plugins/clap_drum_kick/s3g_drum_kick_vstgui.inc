// Literal counterpart of S3GDrumKickView::drawRect. Keep its four rings,
// core, activity color/alpha/radius formulas, panel bounds, and trigger intact.
namespace drum_canvas {
using namespace s3g::portable_gui::canvas;
namespace foundation = s3g::portable_gui::foundation;
constexpr const char* pluginName = "Drum Kick";
constexpr const char* pluginTitle = "s3g DRUM KICK";
constexpr int factoryCount = s3g::kDrumKickFactoryPresetCount;
const char* factoryName(int index) { return s3g::drumKickFactoryPresetInfo(index).name; }
int factoryIndex(const Plugin& p) { return s3g::drumKickFactoryPresetIndex(publishedParamsSnapshot(p)); }
bool randomizeVoice(Plugin& p, uint32_t& seed) {
    s3g::DrumRandom random(seed);
    const auto params = s3g::drumKickSafeRandomParams(publishedParamsSnapshot(p), random);
    seed = random.state();
    return queueGuiSafeRandomParams(p, params);
}
bool loadUserPreset(Plugin& p, const clap_istream_t* stream) {
    const double output = paramValue(p, kOutputParamId);
    if (!stateLoad(&p.plugin, stream)) return false;
    // Same output-trim preservation as Cocoa's preset LOAD. Host project
    // restoration still goes through the unmodified stateLoad entry point.
    applyParam(p, kOutputParamId, output);
    return true;
}
#include "../common/s3g_drum_canvas.inc"
class Editor final : public DrumEditor {
public:
    explicit Editor(Plugin& p) : DrumEditor(p) {}
    void paint() override {
        const float activity = std::clamp(p.visualActivity.load(std::memory_order_relaxed), 0.f, 1.f);
        paintTitle(activity);
        panel(rect(kLeftPanelX, 50., kPanelWidth, 262.), "VOICE / PITCH");
        panel(rect(kRightPanelX, 50., kPanelWidth, 262.), "ATTACK / TEXTURE");
        panel(rect(kLeftPanelX, 344., kPanelWidth, 218.), "SHARED DRUM CHARACTER");
        panel(rect(kRightPanelX, 344., kPanelWidth, 320.), "PERFORMANCE / OUTPUT");
        paintRows();
        pad(rect(kRightPanelX + 12., 480., kPanelWidth - 24., 24.),
            "TRIGGER", activity > .02f, [this] { queueGuiTrigger(p); });
        const CPoint center(687., 568.);
        for (uint32_t ring = 0; ring < 4; ++ring) {
            const double radius = 22. + ring * 17. + activity * (26. - ring * 4.);
            const double alpha = .16 + activity * (.65 - ring * .08);
            ellipse(rect(center.x - radius, center.y - radius, radius * 2., radius * 2.),
                rgba(.30 + activity * .38, .62 + activity * .24, .66 + activity * .20, alpha),
                false, 1. + activity * 2.);
        }
        const double coreRadius = 5. + activity * 12.;
        ellipse(rect(center.x - coreRadius, center.y - coreRadius, coreRadius * 2., coreRadius * 2.),
            rgba(.90, .90, .90, .85));
        text("SYNTHESIZED VOICE  //  CENTERED LOW BODY",
            rect(kRightPanelX + 78., 644., kPanelWidth - 78., 15.), style.label);
    }
};
} // namespace drum_canvas
s3g::portable_gui::foundation::ContentView* makePortableCanvas(Plugin& p) {
    return new drum_canvas::Editor(p);
}
