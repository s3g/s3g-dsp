#pragma once

#include "s3g_sample_cursor_clock.h"
#include "s3g_vstgui_foundation.h"
#include <array>
#include <memory>
#include <string>

namespace s3g::portable_gui {

struct SampleCursorVisual {
    SampleCursorTrajectory trajectory;
    VSTGUI::CColor color;
    std::string label;
    double top = 0, height = 0, flagTop = 18;
};

// Native compositor owns the motion after submission. Neither AppKit/WM_PAINT
// nor a VSTGUI timer needs to execute for an installed trajectory to advance.
class SampleCursorPresenter {
public:
    static std::unique_ptr<SampleCursorPresenter> create(VSTGUI::CFrame* frame);
    virtual ~SampleCursorPresenter() = default;
    virtual bool update(const VSTGUI::CRect& wave,
        const VSTGUI::CRect& occlusion, double viewStart, double viewSpan,
        const std::array<SampleCursorVisual, 64>& cursors, uint32_t count) = 0;
};

bool sampleCursorScreenDraw();
double sampleCursorTime();

} // namespace s3g::portable_gui
