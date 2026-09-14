#pragma once
#include "s3g/tracker/editor_grid_controller.h"
#include "s3g/tracker/editor_grid_painter.h"
#include "s3g_tracker_vstgui_drawing.h"
#include "s3g_vstgui_foundation.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/controls/itexteditlistener.h"
#include <memory>

namespace s3g::tracker::editor {
struct MainPageServices {
    GridPaintServices paint;
    FontFactory fontFactory = nullptr;
    std::function<GridFont(double)> suiteFont;
    std::function<void()> requestNativeFocus;
    std::function<void(const std::string&)> submitConsole, consoleDraftChanged;
    std::function<void(const std::string&)> consoleMessage;
    std::function<std::string()> consoleDraft;
    std::function<std::vector<std::string>()> consoleHistory;
    GridServices grid;
    std::function<void(PitchContour, std::size_t, std::size_t)> pitchContour;
    std::function<void(std::size_t, std::size_t)> openPitchMap;
    std::function<void(std::size_t)> editBurst;
};
struct MainMenuItem {
    std::string title;
    std::function<void()> action;
    std::vector<MainMenuItem> children;
    bool enabled = true, checked = false;
};

// A real VSTGUI-owned page, not an NSView drawing bridge. Only the surrounding
// page shell and still-unconverted tools use native services on Mac.
class MainPageView final : public portable_gui::foundation::ContentView,
                           public VSTGUI::IControlListener,
                           public VSTGUI::ITextEditListener {
public:
    MainPageView(app::TrackerViewState& state, app::WorkspaceCallbacks& callbacks,
        MainPageServices services);
    ~MainPageView() override;
    void draw(VSTGUI::CDrawContext*) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent&) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent&) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent&) override;
    void onMouseWheelEvent(VSTGUI::MouseWheelEvent&) override;
    void onKeyboardEvent(VSTGUI::KeyboardEvent&) override;
    void onZoomGestureEvent(VSTGUI::ZoomGestureEvent&) override;
    void valueChanged(VSTGUI::CControl*) override;
    void onTextEditPlatformControlTookFocus(VSTGUI::CTextEdit*) override { }
    void onTextEditPlatformControlLostFocus(VSTGUI::CTextEdit*) override;
    void stopRefresh() override;
    void reloadModel();
    void refreshPlaybackDisplay();
    void focusTracker();
    void focusConsole();
    void setGridZoom(double zoom);
    double gridZoom() const { return zoom_; }
    void scrollTo(double x, double y);
    void setFollowMode(TrackerFollowMode mode);
    void setFollowSource(bool selectedLane, std::size_t lane);
    void resumeFollowing();
    bool followingPaused() const { return followPaused_; }
    double gridScrollY() const { return scrollY_; }
    std::size_t followPageRows() const { return followLayout().pageRows; }
    std::optional<std::size_t> followLane() const;
    VSTGUI::CRect playbackGuideRect() const;
    GridController& controller() { return grid_; }
    uint64_t drawCount() const { return draws_; }
    VSTGUI::CRect gridViewport() const;
    std::vector<MainMenuItem> contextMenu(GridAddress address, bool rowMenu = false);
    VSTGUI::CRect popupBounds(std::size_t level) const;
    bool handleTextKey(VSTGUI::KeyboardEvent&);

private:
    struct Hit {
        VSTGUI::CRect bounds;
        std::function<void()> action;
        std::string name;
        bool activateOnDown = true;
    };
    struct Popup {
        VSTGUI::CRect bounds;
        std::vector<MainMenuItem> items;
        int hover = -1, scroll = 0, columns = 1, rows = 1;
    };
    app::TrackerViewState& state_;
    app::WorkspaceCallbacks& callbacks_;
    MainPageServices services_;
    GridController grid_;
    VSTGUI::CDrawContext* context_ = nullptr;
    std::vector<Hit> hits_;
    std::vector<Popup> popups_;
    VSTGUI::CPoint hover_ { -1, -1 };
    VSTGUI::CRect pendingButtonBounds_;
    std::function<void()> pendingButton_;
    VSTGUI::CTextEdit* text_ = nullptr;
    std::optional<GridTextEdit> edit_;
    std::string consoleText_, consoleDraft_, displayedPattern_;
    std::vector<std::string> history_;
    int historyIndex_ = -1;
    bool closeText_ = false, cancelText_ = false, textError_ = false, inTextCallback_ = false;
    bool textCommitted_ = false;
    bool returnTextFocus_ = false;
    bool dragSelection_ = false, dragValueCandidate_ = false, dragValue_ = false,
         dragEnvelope_ = false;
    bool momentaryFill_ = false, oldFill_ = false, dragSwing_ = false;
    int scrollDrag_ = 0;
    double zoom_ = 1, scrollX_ = 0, scrollY_ = 0, swingStart_ = .5;
    Point dragOrigin_;
    std::optional<GridAddress> dragAddress_;
    std::optional<std::size_t> loopAnchor_;
    bool wholeRowDrag_ = false, displayedSongFollow_ = false;
    std::array<std::array<std::size_t, 7>, kMaximumTrackCount> presented_ {};
    bool presentedPlaying_ = false, primed_ = false;
    uint32_t presentedMutedTracks_ = 0;
    uint64_t draws_ = 0;
    bool followPaused_ = false, selectingHeader_ = false;
    TrackerFollowSettings observedFollow_;
    uint64_t observedFollowRevision_ = 0;
    uint64_t observedResumeRevision_ = 0;
    bool observedExpanded_ = false;
    std::optional<std::size_t> presentedFollowLane_, presentedFollowRow_;
    bool pinnedHeader() const { return state_.trackerFollow.mode != TrackerFollowMode::Static; }
    TrackerFollowLayout followLayout() const;
    void pauseFollowing();
    void updateFollowing();
    void syncViewCommands();
    void applyGridZoom(double zoom);
    void followPreferencesChanged();
    void setViewport(double x, double y);
    GridServices gridServices();
    void fill(VSTGUI::CRect, uint32_t rgb, double alpha = 1);
    void stroke(VSTGUI::CRect, uint32_t rgb, double width = 1);
    void label(const std::string&, VSTGUI::CRect, uint32_t rgb = 0xb7b7b7,
        VSTGUI::CHoriTxtAlign align = VSTGUI::kLeftText, double size = 10);
    void panel(VSTGUI::CRect, const std::string&);
    void button(VSTGUI::CRect, const std::string&, std::function<void()>, bool enabled = true,
        int state = 0, bool danger = false);
    void menu(VSTGUI::CRect, const std::string&, std::vector<MainMenuItem>, bool enabled = true);
    void openMenu(VSTGUI::CRect anchor, std::vector<MainMenuItem> items, bool child = false);
    void drawMenus();
    std::string fitMenuText(std::string text, double width);
    bool menuPointer(VSTGUI::CPoint, bool click);
    void activateMenu(std::size_t level, int row);
    void paintControls();
    void paintScrollbars();
    void appendSelectionMenu(std::vector<MainMenuItem>& items);
    void appendBurstMenu(std::vector<MainMenuItem>& items, GridAddress address);
    void burstAction(const std::string& kind, GridAddress address, AssetBankId bank,
        std::size_t slot, std::size_t count = 0);
    void startText(GridTextEdit edit);
    void startConsole();
    void setConsoleText(const std::string& text);
    bool commitText();
    void finishText(bool cancel = false);
    void resizeText();
    Point gridPoint(VSTGUI::CPoint point) const;
    VSTGUI::CRect pageRect(Rect logical) const;
    VSTGUI::CRect envelopeRect() const;
    void reveal(Rect rect);
    void changedTransport();
    void command(const std::string& command);
};
} // namespace s3g::tracker::editor
