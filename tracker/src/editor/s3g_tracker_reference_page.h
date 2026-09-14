#pragma once
#include "s3g/tracker/editor_reference.h"
#include "s3g_tracker_tool_page.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/controls/itexteditlistener.h"
#include "vstgui/lib/cvstguitimer.h"

namespace s3g::tracker::editor {
struct ReferencePageServices {
    ToolPageServices tools;
    std::function<GridFont(const ReferenceStyle&)> font;
    std::function<GridFont(std::string_view, const ReferenceStyle&)> fallbackFont;
    std::function<void()> returnToTracker, closeHelp;
};
class ReferencePageView final : public ToolPageView,
                                public VSTGUI::IControlListener,
                                public VSTGUI::ITextEditListener {
public:
    // A null console selects the read-only Help document.
    ReferencePageView(std::shared_ptr<ConsoleModel>, ReferencePageServices);
    ~ReferencePageView() override;
    void drawPage() override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent&) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent&) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent&) override;
    void onMouseCancelEvent(VSTGUI::MouseCancelEvent&) override;
    void onMouseWheelEvent(VSTGUI::MouseWheelEvent&) override;
    void onKeyboardEvent(VSTGUI::KeyboardEvent&) override;
    void resize(double, double) override;
    void stopRefresh() override;
    void refresh();
    void focusInput();
    void valueChanged(VSTGUI::CControl*) override;
    void onTextEditPlatformControlTookFocus(VSTGUI::CTextEdit*) override { }
    void onTextEditPlatformControlLostFocus(VSTGUI::CTextEdit*) override;
    bool handleInputKey(VSTGUI::KeyboardEvent&);
    VSTGUI::CRect textViewport() const;
    VSTGUI::CRect inputBounds() const;
    const ReferenceLayout& textLayout() const { return layout_; }
    std::string selectedText() const;
    void select(std::size_t, std::size_t);
    void scrollTo(double);
    double scrollY() const { return scrollY_; }
    void find(std::string query, bool backwards = false);

private:
    std::shared_ptr<ConsoleModel> console_;
    ReferencePageServices referenceServices_;
    ReferenceDocument help_;
    ReferenceLayout layout_;
    uint64_t revision_ = UINT64_MAX;
    double layoutWidth_ = -1, scrollY_ = 0;
    bool followEnd_ = false, selecting_ = false, scrollbar_ = false, findOpen_ = false;
    bool closeInput_ = false, closeFind_ = false, returnFocus_ = false;
    double scrollOrigin_ = 0, scrollStart_ = 0;
    std::size_t anchor_ = 0, caret_ = 0;
    VSTGUI::CPoint selectionPoint_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> autoscroll_;
    VSTGUI::CTextEdit* input_ = nullptr;
    std::string historyDraft_, findQuery_, displayedDraft_;
    std::size_t historyIndex_ = 0;
    void layoutText();
    void paintText();
    void openInput(bool find);
    void finishInput();
    void updateInput(const std::string&);
    std::size_t hitText(VSTGUI::CPoint) const;
    void dragSelection(bool autoscroll);
    void revealCaret();
};
}
