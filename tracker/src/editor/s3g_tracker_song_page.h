#pragma once
#include "s3g/tracker/editor_song.h"
#include "s3g_tracker_tool_page.h"
#include "vstgui/lib/cvstguitimer.h"
#include <array>

namespace s3g::tracker::editor {
class SongPageView final : public ToolPageView {
public:
    explicit SongPageView(ToolPageServices services = {});
    ~SongPageView() override;
    SongEditor editor;
    void drawPage() override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent&) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent&) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent&) override;
    void onMouseCancelEvent(VSTGUI::MouseCancelEvent&) override;
    void onMouseWheelEvent(VSTGUI::MouseWheelEvent&) override;
    void onKeyboardEvent(VSTGUI::KeyboardEvent&) override;
    void stopRefresh() override;
    void resize(double width, double height) override;
    void modelChanged();
    void playbackChanged() { invalid(); }
    void scrollTo(double x, double y);
    VSTGUI::CRect tableViewport() const;
    VSTGUI::CRect cellBounds(std::size_t row, int column) const;
    VSTGUI::CRect swingBounds(std::size_t row) const;
    double scrollX() const { return scrollX_; }
    double scrollY() const { return scrollY_; }

private:
    double scrollX_ = 0, scrollY_ = 0;
    std::array<double, 11> columnWidths() const;
    double tableWidth() const;
    std::optional<std::size_t> swingRow_, dragRow_;
    uint32_t gestureIdentity_ = 0;
    double swingPreview_ = 56, scrollRemainder_ = 0;
    std::optional<std::size_t> scrollSwingRow_;
    bool swingMoved_ = false, dragMoved_ = false, copyDrag_ = false;
    std::size_t dropRow_ = 0;
    VSTGUI::CPoint dragOrigin_;
    VSTGUI::CPoint dragPoint_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> autoscroll_;
    void updateDrop(bool scroll);
    int scrollbarDrag_ = 0;
    double scrollbarOrigin_ = 0, scrollStart_ = 0;
    void rowMenu(std::size_t, SongField, VSTGUI::CRect, bool enabled = true);
    void drawSwing(std::size_t, VSTGUI::CRect);
    void revealSelection();
    void stageSwing(double x);
    void finishSwing(bool cancel = false);
    void trackScrollbar(VSTGUI::CPoint);
};
}
