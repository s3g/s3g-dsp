#pragma once
#include "s3g/tracker/editor_grid_painter.h"
#include "s3g_tracker_vstgui_drawing.h"
#include "s3g_vstgui_foundation.h"
#include <optional>

namespace s3g::tracker::editor {
inline VSTGUI::CRect toolRect(double x, double y, double w, double h)
{
    return { x, y, x + w, y + h };
}
inline Rect toolLogical(VSTGUI::CRect r) { return { r.left, r.top, r.getWidth(), r.getHeight() }; }
inline bool toolContains(VSTGUI::CRect r, VSTGUI::CPoint p)
{
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}
struct ToolPageServices {
    std::function<GridFont(double)> font;
    std::function<Color(uint32_t, double)> color;
    FontFactory fontFactory = nullptr;
    std::function<void()> requestNativeFocus;
};
struct ToolMenuItem {
    std::string title;
    std::function<void()> action;
    bool checked = false, enabled = true;
};
// Canvas controls for subsequent Tracker page ports. These retain the existing
// suite paint and menu rules, including per-item separators and centered caps.
class ToolPageView : public portable_gui::foundation::ContentView {
public:
    explicit ToolPageView(ToolPageServices);
    void draw(VSTGUI::CDrawContext*) final;
    virtual void drawPage() = 0;
    void onMouseDownEvent(VSTGUI::MouseDownEvent&) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent&) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent&) override;
    void onMouseExitEvent(VSTGUI::MouseExitEvent&) override;
    void onKeyboardEvent(VSTGUI::KeyboardEvent&) override;
    void stopRefresh() override;
    virtual void resize(double width, double height);
    VSTGUI::CRect controlBounds(const std::string&) const;
    VSTGUI::CRect popupItemBounds(std::size_t) const;
    uint64_t drawCount() const { return draws_; }

protected:
    ToolPageServices services_;
    VSTGUI::CDrawContext* context_ = nullptr;
    VSTGUI::CPoint hover_ { -1, -1 };
    VSTGUI::CRect hitClip_;
    void fill(VSTGUI::CRect, uint32_t rgb, double alpha = 1);
    void stroke(VSTGUI::CRect, uint32_t rgb, double alpha = 1);
    void label(std::string, VSTGUI::CRect, uint32_t rgb = 0xa8a8a8, Alignment = Alignment::Left,
        double size = 10, double alpha = 1);
    void panel(VSTGUI::CRect, std::string);
    // state 1=Live, 2=Success, -1=off binary status.
    void button(VSTGUI::CRect, std::string id, std::string title, std::function<void()>,
        bool enabled = true, int state = 0);
    void menu(VSTGUI::CRect, std::string id, std::vector<ToolMenuItem>, bool enabled = true,
        const std::string& display = {});
    void focus();
    void openToolMenu(VSTGUI::CRect, std::vector<ToolMenuItem>, std::size_t selected = 0);
    bool menuOpen() const { return popup_.has_value(); }
    void closeMenu()
    {
        popup_.reset();
        invalid();
    }
    std::string fit(std::string, double width, double size = 10);
    Color color(uint32_t rgb, double alpha = 1) const;
    void hit(VSTGUI::CRect, std::string id, std::function<void()>, bool onDown = false);

private:
    struct Hit {
        VSTGUI::CRect bounds;
        std::string id;
        std::function<void()> action;
        bool onDown = false;
    };
    struct Popup {
        VSTGUI::CRect bounds;
        std::vector<ToolMenuItem> items;
        int rows = 1, columns = 1, hover = -1, selected = 0;
    };
    std::vector<Hit> hits_;
    std::optional<Hit> pressed_;
    std::optional<Popup> popup_;
    uint64_t draws_ = 0;
    bool popupPointer(VSTGUI::CPoint, bool click);
    void drawPopup();
};
}
