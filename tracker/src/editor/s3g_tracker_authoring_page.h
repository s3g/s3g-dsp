#pragma once
#include "s3g/tracker/editor_authoring.h"
#include "s3g/tracker/editor_palette.h"
#include "s3g_tracker_tool_page.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/controls/itexteditlistener.h"
#include "vstgui/lib/cvstguitimer.h"

namespace s3g::tracker::editor {
enum class AuthoringPage { Phrases, Assemble, Reshape };
struct AuthoringServices {
  ToolPageServices tools;
  GridPaintServices grid;
  std::function<void()> error;
  std::function<uint64_t()> clipboardRevision;
  std::function<double()> monotonicTime;
};
class AuthoringPageView final : public ToolPageView,
                                public VSTGUI::IControlListener,
                                public VSTGUI::ITextEditListener {
public:
  AuthoringPageView(app::TrackerViewState &, app::WorkspaceCallbacks &,
                    AuthoringPage, AuthoringServices);
  ~AuthoringPageView() override;
  PhraseEditor phrases;
  AssembleEditor assemble;
  ReshapeEditor reshape;
  AuthoringPage kind() const { return kind_; }
  void drawPage() override;
  void reloadModel();
  void refreshPlaybackDisplay();
  void stopRefresh() override;
  void resize(double, double) override;
  void onMouseDownEvent(VSTGUI::MouseDownEvent &) override;
  void onMouseMoveEvent(VSTGUI::MouseMoveEvent &) override;
  void onMouseUpEvent(VSTGUI::MouseUpEvent &) override;
  void onMouseCancelEvent(VSTGUI::MouseCancelEvent &) override;
  void onMouseWheelEvent(VSTGUI::MouseWheelEvent &) override;
  void onKeyboardEvent(VSTGUI::KeyboardEvent &) override;
  void valueChanged(VSTGUI::CControl *) override {}
  void onTextEditPlatformControlTookFocus(VSTGUI::CTextEdit *) override {}
  void onTextEditPlatformControlLostFocus(VSTGUI::CTextEdit *) override;
  bool textActive() const { return text_ != nullptr; }
  VSTGUI::CRect canvasBounds() const { return canvas_; }
  VSTGUI::CPoint scrollOffset() const { return scroll_; }
  VSTGUI::CRect cellBounds(std::size_t field, std::size_t row) const;
  int auditionRow() const { return auditionRow_; }
  void stopAudition();
  void startAudition();
  void auditionTick();

private:
  double getWidth() const { return getViewSize().getWidth(); }
  double getHeight() const { return getViewSize().getHeight(); }
  app::TrackerViewState &state_;
  app::WorkspaceCallbacks &callbacks_;
  AuthoringPage kind_;
  AuthoringServices platform_;
  VSTGUI::CRect canvas_;
  VSTGUI::CPoint scroll_;
  VSTGUI::CPoint content_;
  bool gridSelecting_ = false, blocksDragging_ = false;
  int scrollDrag_ = 0;
  double scrollGrab_ = 0;
  int dragBlock_ = -1, dropIndex_ = -1;
  std::vector<PhraseAssemblyBlock> dragBlocksSnapshot_;
  VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> auditionTimer_, autoscrollTimer_;
  AuthoringAudition auditionPlan_;
  int auditionRow_ = -1;
  uint32_t auditionToken_ = 0;
  AssetBankId auditionBank_ = 0;
  std::size_t auditionSlot_ = 0;
  bool auditionWhole_ = true;
  std::string nameDraft_, bpmDraft_;
  bool merge_ = false;
  uint64_t copiedRevision_ = 0;
  std::string copiedText_;
  VSTGUI::CTextEdit *text_ = nullptr;
  std::string textId_;
  VSTGUI::CRect textBounds_;
  std::function<bool(std::string)> textCommit_;
  bool closeText_ = false, cancelText_ = false;
  std::size_t textSlot_ = 0;
  AssetBankId textBank_ = 0;
  struct Slider {
    VSTGUI::CRect bounds;
    std::string id;
    float *value;
    double minimum, maximum;
  };
  std::vector<Slider> sliders_;
  std::optional<Slider> dragSlider_;
  double sliderOriginal_ = 0;
  double placeFlashUntil_ = 0;
  void drawPhrases();
  void drawAssemble();
  void drawReshape();
  void drawPhraseGrid();
  void drawAssembly();
  void drawProfile(VSTGUI::CRect);
  void beginCanvas(VSTGUI::CRect, double width, double height);
  void endCanvas();
  VSTGUI::CRect previousClip_;
  void clampScroll();
  VSTGUI::CRect scrollbar(bool horizontal) const;
  void moveScrollbar(VSTGUI::CPoint);
  void revealRow(double y, double h);
  void gridPointer(VSTGUI::CPoint, bool extend, bool rangeClick = false);
  void gridMenu(VSTGUI::CRect);
  void copyGrid();
  void pasteGrid();
  void beginCell(std::optional<std::string> initial = {});
  void startText(std::string, VSTGUI::CRect, std::string,
                 std::function<bool(std::string)>, bool grid = false);
  bool finishText(bool cancel = false);
  void textBox(VSTGUI::CRect, const std::string &, const std::string &,
               std::function<void()>);
  void slider(VSTGUI::CRect, const std::string &, float &, double, double);
  void moveSlider(VSTGUI::CPoint);
  void cancelGesture(bool restore);
  void auditionScroll();
  void error(bool ok);
  double now() const;
  void rowLabel(VSTGUI::CRect, int, const std::string &);
  void choice(VSTGUI::CRect, const std::string &, std::vector<std::string>, int,
              std::function<void(int)>, bool enabled = true,
              const std::string &display = {});
  void bodyText(std::string, VSTGUI::CRect, ThemeRole, double = 10,
                FontWeight = FontWeight::Regular, Alignment = Alignment::Left,
                double alpha = 1);
  void multiline(std::string, VSTGUI::CRect, double size = 8.5);
};
} // namespace s3g::tracker::editor
