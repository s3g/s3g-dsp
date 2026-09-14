#pragma once
#include "s3g/tracker/editor_geometry.h"
#include "s3g_tracker_vstgui_drawing.h"
#include "s3g_vstgui_foundation.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/controls/itexteditlistener.h"
#include "vstgui/lib/cvstguitimer.h"

namespace s3g::tracker::editor {
class GeometryPageView final : public portable_gui::foundation::ContentView,
                               public VSTGUI::IControlListener,
                               public VSTGUI::ITextEditListener {
public:
  GeometryPageView(app::TrackerViewState &, app::WorkspaceCallbacks &,
                   bool bursts, GeometryServices, FontFactory = nullptr);
  ~GeometryPageView() override;
  void draw(VSTGUI::CDrawContext *) override;
  void onMouseDownEvent(VSTGUI::MouseDownEvent &) override;
  void onMouseMoveEvent(VSTGUI::MouseMoveEvent &) override;
  void onMouseUpEvent(VSTGUI::MouseUpEvent &) override;
  void onMouseCancelEvent(VSTGUI::MouseCancelEvent &) override;
  void onKeyboardEvent(VSTGUI::KeyboardEvent &) override;
  void valueChanged(VSTGUI::CControl *) override {}
  void onTextEditPlatformControlTookFocus(VSTGUI::CTextEdit *) override {}
  void onTextEditPlatformControlLostFocus(VSTGUI::CTextEdit *) override;
  void stopRefresh() override;
  void reloadModel();
  void refreshPlaybackDisplay();
  void resize(double, double);
  GeometryEditor &editor() { return *editor_; }
  uint64_t drawCount() const { return draws_; }

private:
  app::TrackerViewState &state_;
  GeometryServices services_;
  FontFactory fontFactory_;
  std::unique_ptr<GeometryEditor> editor_;
  VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> audition_;
  VSTGUI::CTextEdit *name_ = nullptr;
  bool dragging_ = false, closeName_ = false, cancelName_ = false;
  uint64_t draws_ = 0;
  std::string nameDraft_;
  std::size_t nameSlot_ = 0;
  AssetBankId nameBank_ = kProjectAssetBankId;
  void focus();
  void beginName();
  void finishName(bool cancel = false, bool save = false);
};
} // namespace s3g::tracker::editor
