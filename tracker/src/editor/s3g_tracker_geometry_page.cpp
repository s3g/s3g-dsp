#include "s3g_tracker_geometry_page.h"
#include "editor_geometry_support.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/common/generictextedit.h"

namespace s3g::tracker::editor {
using namespace VSTGUI;
namespace f = portable_gui::foundation;
namespace {
CRect vr(Rect r) { return {r.x, r.y, r.x + r.width, r.y + r.height}; }
CColor vc(Color c) { return {c.red, c.green, c.blue, c.alpha}; }
uint32_t modifiers(Modifiers m) {
  return (m.has(ModifierKey::Shift) ? Shift : 0u) |
         (m.has(ModifierKey::Alt) ? Alt : 0u) |
#if defined(__APPLE__)
         (m.has(ModifierKey::Control) ? Command : 0u) |
         (m.has(ModifierKey::Super) ? Control : 0u);
#else
         (m.has(ModifierKey::Control) ? Control : 0u) |
         (m.has(ModifierKey::Super) ? Command : 0u);
#endif
}
GeometryInput input(const MouseEvent &e) {
  return {{e.mousePosition.x, e.mousePosition.y},
          1,
          modifiers(e.modifiers),
          GridKey::None,
          {}};
}
class GeometryNameEdit : public CTextEdit {
public:
  using CTextEdit::CTextEdit;
  std::function<bool(KeyboardEvent &)> handle;
  void takeFocus() override {
    if (!getFrame() || platformControl)
      return;
    platformControl = makeOwned<GenericTextEdit>(this);
    CTextLabel::takeFocus();
    invalid();
  }
  void platformOnKeyboardEvent(KeyboardEvent &e) override {
    if (handle && handle(e)) {
      e.consumed = true;
      return;
    }
    CTextEdit::platformOnKeyboardEvent(e);
  }
};
} // namespace
GeometryPageView::GeometryPageView(app::TrackerViewState &state,
                                   app::WorkspaceCallbacks &callbacks,
                                   bool bursts, GeometryServices services,
                                   FontFactory fontFactory)
    : ContentView({0, 0, 1320, 820}), state_(state),
      services_(std::move(services)), fontFactory_(fontFactory) {
  auto editorServices = services_;
  editorServices.invalidate = [this] { invalid(); };
  editorServices.focus = [this] { focus(); };
  editorServices.startAuditionTimer = [this](double seconds) {
    audition_ = owned(new CVSTGUITimer(
        [this](CVSTGUITimer *) { editor_->auditionTick(); },
        static_cast<uint32_t>(std::max(1., std::round(seconds * 1000.)))));
  };
  editorServices.stopAuditionTimer = [this] {
    if (audition_)
      audition_->stop();
    audition_ = nullptr;
  };
  editor_ = std::make_unique<GeometryEditor>(state, callbacks, bursts,
                                             std::move(editorServices));
  setWantsFocus(true);
}
GeometryPageView::~GeometryPageView() { stopRefresh(); }
void GeometryPageView::focus() {
  if (services_.focus)
    services_.focus();
  if (getFrame() && !name_)
    getFrame()->setFocusView(this);
}
void GeometryPageView::resize(double width, double height) {
  finishName(true);
  setViewSize({0, 0, width, height});
  setMouseableArea(getViewSize());
  editor_->resize(width, height);
}
void GeometryPageView::draw(CDrawContext *context) {
  const auto list = editor_->paint();
  drawDisplayList(*context, list, fontFactory_);
  ++draws_;
  if (editor_->geometryViewMode == GeometryModeBurst &&
      editor_->openMenu() == GeometryMenuNone) {
    const auto slot = editor_->selectedBurstSlot();
    const auto &burst = state_.session.burstLibrary.bursts[slot];
    auto r = editor_->burstNameBoxRect();
    r.y -= 7;
    r.height = gui_layout::kStandardMetrics.hitHeight;
    auto font = services_.suiteFont ? services_.suiteFont(10)
                                    : GridFont{"Menlo", 10, 10, 14};
    auto color = [&](uint32_t rgb) {
      return services_.paint.color
                 ? services_.paint.color(rgb, 1)
                 : Color{uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb),
                         255};
    };
    DisplayList overlay;
    overlay.shape(Primitive::FillRect, r, color(themeRGB(ThemeRole::Control)));
    overlay.shape(Primitive::StrokeRect,
                  {r.x + .5, r.y + .5, r.width - 1, r.height - 1},
                  color(themeRGB(ThemeRole::Border)));
    const bool draft = nameSlot_ == slot &&
                       nameBank_ == state_.activeBurstBankId &&
                       !nameDraft_.empty();
    overlay.text(draft ? nameDraft_ : burst.name,
                 {r.x + 4, r.y, r.width - 8, r.height},
                 color(editor_->textEnabled() ? themeRGB(ThemeRole::TextPrimary)
                                              : 0x656565),
                 font.name, font.size,
                 r.y + (r.height - font.lineHeight) * .5 + font.baseline,
                 Alignment::Left);
    auto save = editor_->burstRenameHeaderButtonRect();
    overlay.shape(Primitive::FillRect, save, color(0x292929));
    const auto cap =
        services_.capHeight ? services_.capHeight(font) : font.size * .72;
    overlay.text("SAVE", save,
                 color(editor_->textEnabled() ? 0x929292 : 0x656565), font.name,
                 font.size, centeredCapsBaseline(save, cap), Alignment::Center);
    drawDisplayList(*context, overlay, fontFactory_);
  }
  setDirty(false);
}
void GeometryPageView::onMouseDownEvent(MouseDownEvent &e) {
  if (!e.buttonState.isLeft())
    return;
  const Point p{e.mousePosition.x, e.mousePosition.y};
  if (editor_->geometryViewMode == GeometryModeBurst &&
      editor_->openMenu() == GeometryMenuNone) {
    auto r = editor_->burstNameBoxRect();
    r.y -= 7;
    r.height = gui_layout::kStandardMetrics.hitHeight;
    if (geometry_support::rContains(p, r)) {
      if (editor_->textEnabled())
        beginName();
      e.consumed = true;
      return;
    }
    if (geometry_support::rContains(p,
                                    editor_->burstRenameHeaderButtonRect())) {
      if (name_)
        finishName(false, true);
      e.consumed = true;
      return;
    }
  }
  finishName(true);
  focus();
  auto pointer = input(e);
  pointer.clickCount = e.clickCount;
  editor_->pointerDown(pointer);
  dragging_ = true;
  e.consumed = true;
}
void GeometryPageView::onMouseMoveEvent(MouseMoveEvent &e) {
  editor_->pointerMove(input(e), dragging_);
  e.consumed = true;
}
void GeometryPageView::onMouseUpEvent(MouseUpEvent &e) {
  editor_->pointerUp(input(e));
  dragging_ = false;
  e.consumed = true;
}
void GeometryPageView::onMouseCancelEvent(MouseCancelEvent &e) {
  editor_->cancelGesture();
  dragging_ = false;
  e.consumed = true;
}
void GeometryPageView::onKeyboardEvent(KeyboardEvent &e) {
  if (e.type != EventType::KeyDown || name_)
    return;
  if (e.virt == VirtualKey::Escape) {
    editor_->suspend();
    dragging_ = false;
    e.consumed = true;
    return;
  }
  GeometryInput key;
  key.modifiers = modifiers(e.modifiers);
  switch (e.virt) {
  case VirtualKey::Left:
    key.key = GridKey::Left;
    break;
  case VirtualKey::Right:
    key.key = GridKey::Right;
    break;
  case VirtualKey::Up:
    key.key = GridKey::Up;
    break;
  case VirtualKey::Down:
    key.key = GridKey::Down;
    break;
  case VirtualKey::Tab:
    key.key = GridKey::Tab;
    break;
  default:
    break;
  }
  if (editor_->menuKey(key.key, e.virt == VirtualKey::Return)) {
    e.consumed = true;
    return;
  }
  if (e.character > 0 && e.character < 128)
    key.text = std::string(1, static_cast<char>(e.character));
  if (key.key == GridKey::None && key.text != " " && key.text != "s" &&
      key.text != "p" && key.text != "e" && key.text != "v" &&
      key.text != "+" && key.text != "=" && key.text != "-")
    return;
  if (key.modifiers & (Control | Alt | Command))
    return;
  editor_->keyDown(key);
  e.consumed = true;
}
void GeometryPageView::beginName() {
  if (name_ || !getFrame())
    return;
  focus();
  nameSlot_ = editor_->selectedBurstSlot();
  nameBank_ = state_.activeBurstBankId;
  nameDraft_ = state_.session.burstLibrary.bursts[nameSlot_].name;
  auto r = editor_->burstNameBoxRect();
  r.y -= 7;
  r.height = gui_layout::kStandardMetrics.hitHeight;
  auto *name = new GeometryNameEdit(vr(r), this, 0, nameDraft_.c_str());
  name->handle = [this](KeyboardEvent &e) {
    if (e.type != EventType::KeyDown ||
        (e.virt != VirtualKey::Return && e.virt != VirtualKey::Escape &&
         e.virt != VirtualKey::Tab))
      return false;
    closeName_ = true;
    cancelName_ = e.virt == VirtualKey::Escape;
    return true;
  };
  const auto font = services_.suiteFont ? services_.suiteFont(10)
                                        : GridFont{"Menlo", 10, 10, 14};
  name->setFont(fontFactory_ ? fontFactory_(font.name, font.size)
                             : f::makeUiFont(10));
  auto c = [&](uint32_t rgb) {
    return vc(
        services_.paint.color
            ? services_.paint.color(rgb, 1)
            : Color{uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb), 255});
  };
  name->setFontColor(c(themeRGB(ThemeRole::TextPrimary)));
  name->setBackColor(c(themeRGB(ThemeRole::Control)));
  name->setFrameColor(c(themeRGB(ThemeRole::Border)));
  name->setHoriAlign(kLeftText);
  name->setTextInset({4, 0});
  name->registerTextEditListener(this);
  name_ = name;
  getFrame()->addView(name);
  getFrame()->setFocusView(name);
  invalid();
}
void GeometryPageView::finishName(bool cancel, bool save) {
  if (name_) {
    auto *name = name_;
    name_ = nullptr;
    name->unregisterTextEditListener(this);
    if (getFrame() && getFrame()->getFocusView() == name)
      getFrame()->setFocusView(this);
    if (!cancel)
      nameDraft_ = name->getText().getString();
    if (getFrame())
      getFrame()->removeView(name, true);
  }
  if (cancel || nameSlot_ != editor_->selectedBurstSlot() ||
      nameBank_ != state_.activeBurstBankId)
    nameDraft_.clear();
  else if (save) {
    editor_->saveBurstName(nameDraft_);
    nameDraft_.clear();
  }
  closeName_ = cancelName_ = false;
  invalid();
}
void GeometryPageView::onTextEditPlatformControlLostFocus(CTextEdit *) {
  closeName_ = true;
}
void GeometryPageView::stopRefresh() {
  finishName(true);
  dragging_ = false;
  editor_->suspend();
}
void GeometryPageView::reloadModel() {
  finishName(true);
  dragging_ = false;
  editor_->reloadModel();
}
void GeometryPageView::refreshPlaybackDisplay() {
  if (name_ &&
      (nameSlot_ != editor_->selectedBurstSlot() ||
       nameBank_ != state_.activeBurstBankId || !editor_->textEnabled()))
    finishName(true);
  if (closeName_)
    finishName(cancelName_, !cancelName_);
  editor_->refreshPlaybackDisplay();
  invalid();
}
} // namespace s3g::tracker::editor
