#pragma once
#include "s3g/tracker/editor_grid_painter.h"
#include "s3g/tracker/editor_state.h"
#include <functional>

namespace s3g::tracker::editor {
enum class WarpField { Cycle, Primary, Pulses, Mix, Begin, End, Repeats };
struct WarpFieldValue {
    double value = 0, minimum = 0, maximum = 1;
    unsigned digits = 2;
    bool enabled = false, visible = true;
};
// UI-thread authoring only. Audio continues to consume its prepared immutable
// stack through the existing transportChanged publication path.
class WarpEditor {
public:
    WarpEditor(app::TrackerViewState& state, std::function<void()> changed)
        : state(state)
        , changed(std::move(changed))
    {
    }
    app::TrackerViewState& state;
    std::size_t slot = 0, selected = 0;
    void reconcile();
    bool selectSlot(std::size_t);
    bool save(std::string name);
    bool erase();
    void toggle();
    bool add(TimingWarpKind);
    bool remove();
    void clear();
    bool setKind(TimingWarpKind);
    bool set(WarpField, double);
    WarpFieldValue field(WarpField) const;
    std::vector<std::string> slots() const;
    std::vector<std::string> transforms() const;
    std::string name() const;
    std::string status() const;
    std::string playbackDescription() const;

private:
    std::function<void()> changed;
    void publish();
    bool replace(TimingWarpTransform);
};
// Geometry and sampling are a literal port of S3GTrackerWarpCurveView. The
// coordinator owns display holdback; drawing never advances musical time.
DisplayList paintWarpCurve(const app::TrackerViewState&, Rect bounds, GridFont font,
    std::function<Color(uint32_t, double)> color = {});
}
