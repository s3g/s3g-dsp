#pragma once
#include "s3g/tracker/editor_warp.h"
#include "s3g_tracker_vstgui_drawing.h"
#include "s3g_vstgui_foundation.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/controls/itexteditlistener.h"
#include <optional>

namespace s3g::tracker::editor {
struct WarpPageServices {
    std::function<GridFont(double)> font;
    std::function<Color(uint32_t, double)> color;
    FontFactory fontFactory = nullptr;
    std::function<void()> requestNativeFocus, error;
};
class WarpPageView final : public portable_gui::foundation::ContentView,
                           public VSTGUI::IControlListener,
                           public VSTGUI::ITextEditListener {
public:
    WarpPageView(app::TrackerViewState&, app::WorkspaceCallbacks&, WarpPageServices = {});
    ~WarpPageView() override;
    void draw(VSTGUI::CDrawContext*) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent&) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent&) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent&) override;
    void onMouseExitEvent(VSTGUI::MouseExitEvent&) override;
    void onKeyboardEvent(VSTGUI::KeyboardEvent&) override;
    void valueChanged(VSTGUI::CControl*) override { }
    void onTextEditPlatformControlTookFocus(VSTGUI::CTextEdit*) override { }
    void onTextEditPlatformControlLostFocus(VSTGUI::CTextEdit*) override;
    void stopRefresh() override;
    void reloadModel();
    void refreshPlaybackDisplay(bool visible = true);
    void resize(double width, double height);
    WarpEditor& editor() { return editor_; }
    uint64_t drawCount() const { return draws_; }
    VSTGUI::CRect controlBounds(const std::string&) const;
    VSTGUI::CRect popupItemBounds(std::size_t) const;

private:
    void layoutAndDraw(VSTGUI::CDrawContext*);
    double getWidth() const { return getViewSize().getWidth(); }
    double getHeight() const { return getViewSize().getHeight(); }
    enum class HitKind { Button, Menu, Slider, Name };
    struct Hit {
        VSTGUI::CRect bounds;
        std::string id;
        HitKind kind;
        std::function<void()> action;
        WarpField field = WarpField::Cycle;
    };
    struct Popup {
        VSTGUI::CRect bounds;
        std::vector<std::string> items;
        std::function<void(std::size_t)> select;
        int hover = -1, selected = 0, rows = 1, columns = 1;
    };
    WarpPageServices services_;
    WarpEditor editor_;
    VSTGUI::CDrawContext* context_ = nullptr;
    std::vector<Hit> hits_;
    std::array<WarpFieldValue, 7> fields_ {};
    std::optional<Hit> pressed_, dragging_;
    std::optional<Popup> popup_;
    VSTGUI::CPoint hover_ { -1, -1 };
    VSTGUI::CTextEdit* text_ = nullptr;
    std::optional<WarpField> textField_;
    bool closeText_ = false, cancelText_ = false, saveText_ = false;
    std::string name_;
    TimingWarpKind kind_ = TimingWarpKind::Exponential;
    uint64_t draws_ = 0;
    Color color(uint32_t, double alpha = 1) const;
    void fill(VSTGUI::CRect, uint32_t, double alpha = 1);
    void stroke(VSTGUI::CRect, uint32_t);
    void label(std::string, VSTGUI::CRect, uint32_t = 0xa8a8a8, Alignment = Alignment::Left,
        double size = 10);
    void panel(VSTGUI::CRect, std::string);
    void button(VSTGUI::CRect, std::string id, std::string title, std::function<void()>,
        bool enabled = true, bool live = false);
    void menu(VSTGUI::CRect, std::string id, std::vector<std::string>, std::size_t selected,
        std::function<void(std::size_t)>, bool enabled = true);
    void slider(VSTGUI::CRect, WarpField);
    void drawPopup();
    bool popupPointer(VSTGUI::CPoint, bool click);
    void drag(VSTGUI::CPoint);
    void focus();
    void startText(VSTGUI::CRect, std::optional<WarpField>);
    void finishText(bool cancel = false, bool save = false);
    void result(bool);
    std::string fit(std::string, double);
};
}
