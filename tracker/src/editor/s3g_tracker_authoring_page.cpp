#include "s3g_tracker_authoring_page.h"
#include "editor_geometry_support.h"
#include "vstgui/lib/cclipboard.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/common/generictextedit.h"
#include <chrono>
#include <locale>
#include <sstream>
namespace s3g::tracker::editor {
using namespace VSTGUI;
using geometry_support::format;
namespace {
constexpr double columns[] = {0, 52, 222, 302, 402, 478, 578, 654, 800};
CColor cc(Color c) { return {c.red, c.green, c.blue, c.alpha}; }
class AuthoringText final : public CTextEdit {
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
AuthoringPageView::AuthoringPageView(app::TrackerViewState &s,
                                     app::WorkspaceCallbacks &c,
                                     AuthoringPage k, AuthoringServices p)
    : ToolPageView(p.tools), phrases(s, c), assemble(s, c), reshape(s, c),
      state_(s), callbacks_(c), kind_(k), platform_(std::move(p)) {
  reloadModel();
}
AuthoringPageView::~AuthoringPageView() { stopRefresh(); }
double AuthoringPageView::now() const {
  return platform_.monotonicTime
             ? platform_.monotonicTime()
             : std::chrono::duration<double>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count();
}
void AuthoringPageView::error(bool ok) {
  if (!ok && platform_.error)
    platform_.error();
  invalid();
}
void AuthoringPageView::reloadModel() {
  // Recall never commits stale text or a drag snapshot into a new document.
  finishText(true);
  cancelGesture(false);
  stopAudition();
  ToolPageView::stopRefresh();
  if (kind_ == AuthoringPage::Phrases) {
    phrases.reload();
    nameDraft_ = phrases.phrase().name;
    bpmDraft_ = phrases.phrase().recommendedBpm
                    ? format("%.15g", *phrases.phrase().recommendedBpm)
                    : "";
  } else if (kind_ == AuthoringPage::Assemble)
    assemble.reload();
  else
    reshape.reload();
  clampScroll();
  invalid();
}
void AuthoringPageView::refreshPlaybackDisplay() {
  if (closeText_)
    finishText(cancelText_);
  if (auditionRow_ >= 0 &&
      (state_.playing || state_.activePhraseBankId != auditionBank_ ||
       state_.selectedPhrase != auditionSlot_))
    stopAudition();
  // Only published playback state is observed here. No audition events are
  // scheduled from paint/refresh, regardless of host FPS or display stalls.
  invalid();
}
void AuthoringPageView::stopRefresh() {
  finishText(true);
  cancelGesture(false);
  stopAudition();
  if (kind_ == AuthoringPage::Reshape)
    reshape.clearPreview();
  ToolPageView::stopRefresh();
}
void AuthoringPageView::resize(double w, double h) {
  finishText();
  cancelGesture(true);
  ToolPageView::stopRefresh();
  auto r = toolRect(0, 0, w, h);
  setViewSize(r);
  setMouseableArea(r);
  clampScroll();
  invalid();
}
void AuthoringPageView::drawPage() {
  sliders_.clear();
  fill(getViewSize(), themeRGB(ThemeRole::Canvas));
  switch (kind_) {
  case AuthoringPage::Phrases:
    drawPhrases();
    break;
  case AuthoringPage::Assemble:
    drawAssemble();
    break;
  case AuthoringPage::Reshape:
    drawReshape();
    break;
  }
}
void AuthoringPageView::rowLabel(CRect panel, int row, const std::string &s) {
  const auto f = services_.font(10);
  label(s, toolRect(panel.left + 16,
                    panel.top + 35 + row * 26 +
                        std::floor((15 - f.lineHeight) * .5),
                    86, 15));
}
void AuthoringPageView::choice(CRect r, const std::string &id,
                               std::vector<std::string> titles, int current,
                               std::function<void(int)> select, bool enabled,
                               const std::string &display) {
  std::vector<ToolMenuItem> items;
  for (int i = 0; i < int(titles.size()); ++i)
    items.push_back(
        {titles[i], [select, i] { select(i); }, i == current, true});
  menu(r, id, std::move(items), enabled, display);
}
void AuthoringPageView::bodyText(std::string text, CRect r, ThemeRole role,
                                 double size, FontWeight weight,
                                 Alignment align, double alpha) {
  auto font = platform_.grid.font
                  ? platform_.grid.font(size, weight, false)
                  : GridFont{"IBM Plex Mono", size, size, size * 1.3};
  DisplayList list;
  list.text(std::move(text), toolLogical(r), color(themeRGB(role), alpha),
            font.name, font.size, r.top + font.baseline, align);
  drawDisplayList(*context_, list, services_.fontFactory);
}
void AuthoringPageView::multiline(std::string text, CRect r, double size) {
  // Native wrapping labels: explicit line breaks plus bounded word wrapping.
  auto font = services_.font(size);
  auto face = services_.fontFactory
                  ? services_.fontFactory(font.name, size)
                  : portable_gui::foundation::makeUiFont(size);
  context_->setFont(face);
  std::istringstream input(text);
  std::string line;
  double y = r.top;
  auto emit = [&](const std::string &s) {
    if (y + font.lineHeight <= r.bottom + 1)
      label(s, toolRect(r.left + 2, y, r.getWidth() - 4, font.lineHeight),
            0x929292, Alignment::Left, size);
    y += font.lineHeight;
  };
  while (std::getline(input, line)) {
    while (context_->getStringWidth(line.c_str()) > r.getWidth() - 4) {
      auto at = line.find_last_of(' ');
      while (at != std::string::npos &&
             context_->getStringWidth(line.substr(0, at).c_str()) >
                 r.getWidth() - 4)
        at = at ? line.find_last_of(' ', at - 1) : std::string::npos;
      if (at == std::string::npos)
        break;
      emit(line.substr(0, at));
      line.erase(0, at + 1);
    }
    emit(line);
  }
}
void AuthoringPageView::textBox(CRect r, const std::string &id,
                                const std::string &text,
                                std::function<void()> action) {
  fill(r, themeRGB(ThemeRole::Control));
  stroke(r, themeRGB(ThemeRole::Border));
  if (!text_ || textId_ != id)
    label(text,
          toolRect(r.left + 3, r.top + 5, r.getWidth() - 6, r.getHeight() - 5),
          themeRGB(ThemeRole::TextPrimary));
  hit(r, id, std::move(action), true);
}
void AuthoringPageView::startText(std::string id, CRect r, std::string value,
                                  std::function<bool(std::string)> commit,
                                  bool grid) {
  if (!getFrame() || !finishText())
    return;
  closeMenu();
  focus();
  textId_ = std::move(id);
  textBounds_ = r;
  textCommit_ = std::move(commit);
  textSlot_ = state_.selectedPhrase;
  textBank_ = state_.activePhraseBankId;
  auto *t = new AuthoringText(r, this, 0, value.c_str());
  text_ = t;
  auto font = grid && platform_.grid.font
                  ? platform_.grid.font(11, FontWeight::Semibold, false)
                  : services_.font(10);
  t->setFont(services_.fontFactory
                 ? services_.fontFactory(font.name, font.size)
                 : portable_gui::foundation::makeUiFont(font.size));
  t->setFontColor(cc(color(themeRGB(ThemeRole::TextPrimary))));
  t->setBackColor(cc(color(themeRGB(ThemeRole::Control))));
  t->setFrameColor(cc(color(themeRGB(ThemeRole::Border))));
  t->setHoriAlign(grid ? kCenterText : kLeftText);
  t->setTextInset({3, 0});
  t->registerTextEditListener(this);
  t->handle = [this](KeyboardEvent &e) {
    if (e.type != EventType::KeyDown)
      return false;
    if (e.virt != VirtualKey::Return && e.virt != VirtualKey::Escape &&
        e.virt != VirtualKey::Tab)
      return false;
    closeText_ = true;
    cancelText_ = e.virt == VirtualKey::Escape;
    return true;
  };
  getFrame()->addView(t);
  getFrame()->setFocusView(t);
  invalid();
}
void AuthoringPageView::onTextEditPlatformControlLostFocus(CTextEdit *) {
  closeText_ = true;
}
bool AuthoringPageView::finishText(bool cancel) {
  if (!text_)
    return true;
  auto *t = text_;
  text_ = nullptr;
  t->unregisterTextEditListener(this);
  static_cast<AuthoringText *>(t)->handle = {};
  if (getFrame() && getFrame()->getFocusView() == t)
    getFrame()->setFocusView(this);
  std::string value = t->getText().getString();
  auto commit = std::move(textCommit_);
  auto id = textId_;
  auto bounds = textBounds_;
  if (getFrame())
    getFrame()->removeView(t, true);
  closeText_ = cancelText_ = false;
  textId_.clear();
  if (!cancel && state_.selectedPhrase == textSlot_ &&
      state_.activePhraseBankId == textBank_ && commit && !commit(value)) {
    error(false);
    startText(id, bounds, value, std::move(commit), id == "cell");
    return false;
  }
  invalid();
  return true;
}
CRect AuthoringPageView::cellBounds(std::size_t f, std::size_t row) const {
  if (f >= 7)
    return {};
  return toolRect(canvas_.left + columns[f + 1] - scroll_.x,
                  canvas_.top + 30 + row * 22 - scroll_.y,
                  columns[f + 2] - columns[f + 1], 22);
}
void AuthoringPageView::beginCell(std::optional<std::string> initial) {
  phrases.resetSelection();
  auto r = cellBounds(phrases.field, phrases.row);
  r.inset(1, 1);
  auto row = phrases.row, field = phrases.field;
  startText(
      "cell", r,
      initial ? *initial : phraseGridCellText(phrases.phrase(), field, row),
      [this, row, field](std::string s) {
        phrases.row = row;
        phrases.field = field;
        return phrases.edit(s);
      },
      true);
  // GenericTextEdit initially selects all; native Tracker places the caret
  // after the seed character so subsequent digits append rather than replace.
  if (text_ && getFrame()) {
    KeyboardEvent end;
    end.type = EventType::KeyDown;
    end.virt = VirtualKey::Right;
    static_cast<IPlatformFrameCallback *>(getFrame())->platformOnEvent(end);
  }
}
void AuthoringPageView::copyGrid() {
  copiedText_ = phrases.copy();
  CClipboard::setString(copiedText_.c_str());
  copiedRevision_ =
      platform_.clipboardRevision ? platform_.clipboardRevision() : 0;
}
void AuthoringPageView::pasteGrid() {
  auto text = CClipboard::getString();
  std::string s = text ? text->getString() : "";
  bool same = platform_.clipboardRevision &&
              copiedRevision_ == platform_.clipboardRevision();
  error(phrases.paste(s, same && s == copiedText_));
}
void AuthoringPageView::slider(CRect r, const std::string &id, float &value,
                               double minimum, double maximum) {
  auto w = std::min(150., r.getWidth() - 50);
  auto track = toolRect(r.left, r.top + 9, w, 9);
  auto n =
      std::clamp((double(value) * 100 - minimum) / (maximum - minimum), 0., 1.);
  fill(track, 0x131313);
  fill(toolRect(track.left + 1, track.top + 1, std::max(1., (w - 2) * n), 7),
       0x7f7f7f);
  fill(toolRect(track.left + std::clamp(w * n - 1.5, 1., w - 4), track.top - 2,
                3, 13),
       0xc9c9c9);
  label(format("%.0f%%", double(value) * 100),
        toolRect(r.right - 42, r.top + 6, 42, 15), 0x929292, Alignment::Right);
  sliders_.push_back({r, id, &value, minimum, maximum});
  hit(r, id, [] {}); // Public bounds for automation/parity tests.
}
void AuthoringPageView::moveSlider(CPoint p) {
  if (!dragSlider_)
    return;
  auto &s = *dragSlider_;
  auto w = std::min(150., s.bounds.getWidth() - 50);
  auto n = std::clamp((p.x - s.bounds.left) / w, 0., 1.);
  auto v = float(std::round(s.minimum + n * (s.maximum - s.minimum)) / 100.);
  if (*s.value != v) {
    *s.value = v;
    reshape.refreshResult();
    invalid();
  }
}
void AuthoringPageView::cancelGesture(bool restore) {
  if (restore && dragSlider_) {
    *dragSlider_->value = float(sliderOriginal_);
    reshape.refreshResult();
  }
  dragSlider_.reset();
  gridSelecting_ = blocksDragging_ = false;
  scrollDrag_ = 0;
  dragBlock_ = dropIndex_ = -1;
  dragBlocksSnapshot_.clear();
  autoscrollTimer_ = nullptr;
}
void AuthoringPageView::gridPointer(CPoint p, bool extend, bool rangeClick) {
  double x = std::clamp(p.x - canvas_.left + scroll_.x, 52., 799.);
  double y = p.y - canvas_.top + scroll_.y;
  auto row = std::size_t(
      std::clamp(int((y - 30) / 22), 0, int(phrases.phrase().length) - 1));
  std::size_t f = 0;
  for (std::size_t i = 1; i < 7; ++i)
    if (x >= columns[i + 1])
      f = i;
  if (rangeClick && f != phrases.field)
    extend = false;
  if (!extend) {
    phrases.row = row;
    phrases.field = f;
    phrases.resetSelection();
  } else {
    phrases.selection.focusRow = row;
    phrases.selection.focusField = f;
    phrases.selection.active = !phrases.selection.isSingleCell();
    phrases.row = row;
    phrases.field = f;
  }
  invalid();
}
void AuthoringPageView::onMouseDownEvent(MouseDownEvent &e) {
  if (menuOpen()) {
    ToolPageView::onMouseDownEvent(e);
    return;
  }
  if (!finishText()) {
    e.consumed = true;
    return;
  }
  if (e.buttonState.isLeft())
    for (auto &s : sliders_)
      if (toolContains(s.bounds, e.mousePosition) &&
          e.mousePosition.x <=
              s.bounds.left + std::min(150., s.bounds.getWidth() - 50)) {
        focus();
        dragSlider_ = s;
        sliderOriginal_ = *s.value;
        moveSlider(e.mousePosition);
        e.consumed = true;
        return;
      }
  if (toolContains(canvas_, e.mousePosition) &&
      kind_ != AuthoringPage::Reshape) {
    focus();
    auto p = e.mousePosition;
    double x = p.x - canvas_.left + scroll_.x,
           y = p.y - canvas_.top + scroll_.y;
    if (e.buttonState.isLeft())
      for (int axis = 1; axis <= 2; ++axis) {
        bool horizontal = axis == 2;
        auto thumb = scrollbar(horizontal);
        if (thumb.isEmpty())
          continue;
        auto lane = horizontal ? toolRect(canvas_.left, canvas_.bottom - 8,
                                          canvas_.getWidth(), 8)
                               : toolRect(canvas_.right - 8, canvas_.top, 8,
                                          canvas_.getHeight());
        if (toolContains(lane, p)) {
          scrollDrag_ = axis;
          scrollGrab_ =
              toolContains(thumb, p)
                  ? (horizontal ? p.x - thumb.left : p.y - thumb.top)
                  : (horizontal ? thumb.getWidth() : thumb.getHeight()) * .5;
          moveScrollbar(p);
          e.consumed = true;
          return;
        }
      }
    if (kind_ == AuthoringPage::Phrases && x >= 52 && x < 800 && y >= 30 &&
        y < 30 + phrases.phrase().length * 22) {
      bool preserve = e.buttonState.isRight() && phrases.selection.active;
      if (preserve) {
        auto old = phrases.selection;
        gridPointer(p, false);
        if (old.range().contains(0, 0, phrases.field, phrases.row))
          phrases.selection = old;
      } else
        gridPointer(p, e.modifiers.has(ModifierKey::Shift), true);
      if (e.buttonState.isRight())
        gridMenu(cellBounds(phrases.field, phrases.row));
      else {
        gridSelecting_ = !e.modifiers.has(ModifierKey::Shift);
        if (e.clickCount >= 2) {
          gridSelecting_ = false;
          beginCell();
        }
      }
      e.consumed = true;
    } else if (kind_ == AuthoringPage::Assemble && e.buttonState.isLeft()) {
      auto i = assemble.blockAt(y, false);
      if (i < 0)
        assemble.selected.clear();
      else {
        if (e.modifiers.has(ModifierKey::Shift)) {
          if (!assemble.selected.erase(std::size_t(i)))
            assemble.selected.insert(std::size_t(i));
        } else if (!assemble.selected.count(std::size_t(i)))
          assemble.selected = {std::size_t(i)};
        dragBlock_ = dropIndex_ = i;
        dragBlocksSnapshot_ = state_.assembly.blocks;
      }
      e.consumed = true;
      invalid();
    }
    if (e.consumed)
      return;
  }
  ToolPageView::onMouseDownEvent(e);
}
void AuthoringPageView::onMouseMoveEvent(MouseMoveEvent &e) {
  hover_ = e.mousePosition;
  if (scrollDrag_) {
    moveScrollbar(hover_);
    e.consumed = true;
    return;
  }
  if (dragSlider_) {
    moveSlider(hover_);
    e.consumed = true;
    return;
  }
  if (gridSelecting_ || dragBlock_ >= 0) {
    if (gridSelecting_)
      gridPointer(hover_, true);
    else {
      blocksDragging_ = true;
      dropIndex_ = assemble.blockAt(hover_.y - canvas_.top + scroll_.y, true);
    }
    if (!autoscrollTimer_)
      autoscrollTimer_ = makeOwned<CVSTGUITimer>(
          [this](CVSTGUITimer *) {
            double d = hover_.y < canvas_.top
                           ? -12
                           : hover_.y > canvas_.bottom ? 12 : 0;
            if (!d)
              return;
            scroll_.y += d;
            clampScroll();
            if (gridSelecting_)
              gridPointer(hover_, true);
            else
              dropIndex_ =
                  assemble.blockAt(hover_.y - canvas_.top + scroll_.y, true);
            invalid();
          },
          30);
    e.consumed = true;
    invalid();
    return;
  }
  ToolPageView::onMouseMoveEvent(e);
}
void AuthoringPageView::onMouseUpEvent(MouseUpEvent &e) {
  if (dragSlider_ || gridSelecting_ || dragBlock_ >= 0 || scrollDrag_) {
    const bool same =
        state_.assembly.blocks.size() == dragBlocksSnapshot_.size() &&
        std::equal(state_.assembly.blocks.begin(), state_.assembly.blocks.end(),
                   dragBlocksSnapshot_.begin(),
                   [](const auto &a, const auto &b) {
                     return a.phraseBankId == b.phraseBankId &&
                            a.phraseSlot == b.phraseSlot &&
                            a.repeats == b.repeats;
                   });
    if (blocksDragging_ && dropIndex_ >= 0 && same) {
      stopAudition();
      error(assemble.move(std::size_t(dropIndex_),
                          e.modifiers.has(ModifierKey::Alt)));
    }
    cancelGesture(false);
    e.consumed = true;
    invalid();
    return;
  }
  ToolPageView::onMouseUpEvent(e);
}
void AuthoringPageView::onMouseCancelEvent(MouseCancelEvent &e) {
  cancelGesture(true);
  ToolPageView::stopRefresh();
  e.consumed = true;
  invalid();
}
void AuthoringPageView::onMouseWheelEvent(MouseWheelEvent &e) {
  if (menuOpen() || !toolContains(canvas_, e.mousePosition) ||
      kind_ == AuthoringPage::Reshape)
    return;
  if (!finishText()) {
    e.consumed = true;
    return;
  }
  scroll_.x -= e.deltaX * 44;
  scroll_.y -= e.deltaY * 44;
  clampScroll();
  invalid();
  e.consumed = true;
}
void AuthoringPageView::onKeyboardEvent(KeyboardEvent &e) {
  if (menuOpen()) {
    ToolPageView::onKeyboardEvent(e);
    return;
  }
  if (e.type != EventType::KeyDown || text_)
    return;
  if (e.virt == VirtualKey::Escape &&
      (dragSlider_ || gridSelecting_ || dragBlock_ >= 0)) {
    cancelGesture(true);
    e.consumed = true;
    return;
  }
  auto control = e.modifiers.has(ModifierKey::Control),
       shift = e.modifiers.has(ModifierKey::Shift);
  auto ch = char(e.character);
  if (ch >= 'A' && ch <= 'Z')
    ch = char(ch + 32);
  if (kind_ == AuthoringPage::Assemble) {
    if (e.modifiers.empty() && ch == 'a') {
      stopAudition();
      error(assemble.append());
    } else if (e.modifiers.empty() &&
               (e.virt == VirtualKey::Space || ch == ' ')) {
      if (auditionRow_ >= 0)
        stopAudition();
      else
        startAudition();
    } else if (e.virt == VirtualKey::Back || e.virt == VirtualKey::Delete) {
      stopAudition();
      assemble.erase();
    } else
      return;
    e.consumed = true;
    invalid();
    return;
  }
  if (kind_ != AuthoringPage::Phrases)
    return;
  if (control && !shift) {
    if (ch == 'a')
      phrases.selectAll();
    else if (ch == 'c')
      copyGrid();
    else if (ch == 'x') {
      copyGrid();
      phrases.clear();
    } else if (ch == 'v')
      pasteGrid();
    else
      return;
  } else if (e.virt == VirtualKey::Return)
    beginCell();
  else if (e.virt == VirtualKey::Back || e.virt == VirtualKey::Delete)
    phrases.clear();
  else if (e.virt == VirtualKey::Up || e.virt == VirtualKey::Down ||
           e.virt == VirtualKey::Left || e.virt == VirtualKey::Right ||
           e.virt == VirtualKey::Tab) {
    bool extend = control && shift;
    if (extend && !phrases.selection.active)
      phrases.resetSelection();
    if (e.virt == VirtualKey::Up) {
      if (phrases.row)
        --phrases.row;
    } else if (e.virt == VirtualKey::Down)
      phrases.row = std::min(phrases.row + 1, phrases.phrase().length - 1);
    else {
      bool back =
          e.virt == VirtualKey::Left || (e.virt == VirtualKey::Tab && shift);
      phrases.field =
          back ? (phrases.field ? phrases.field - 1 : extend ? 0 : 6)
               : (extend ? std::min<std::size_t>(6, phrases.field + 1)
                         : (phrases.field + 1) % 7);
    }
    if (extend) {
      phrases.selection.focusRow = phrases.row;
      phrases.selection.focusField = phrases.field;
      phrases.selection.active = !phrases.selection.isSingleCell();
    } else
      phrases.resetSelection();
    revealRow(30 + phrases.row * 22, 22);
  } else if (!control && !e.modifiers.has(ModifierKey::Super) &&
             !e.modifiers.has(ModifierKey::Alt) && e.character < 128) {
    bool digit = ch >= '0' && ch <= '9';
    auto f = phrases.field;
    bool begin =
        f == 0 ? digit || (ch >= 'a' && ch <= 'g') || ch == 'r' || ch == 'h' ||
                     ch == 'k' || ch == '-'
               : f == 1 ? digit || ch == '.' || ch == 'd' || ch == 'p'
                        : f == 6 ? digit || ch == '.' || ch == 'd' || ch == 't'
                                 : f % 2 == 0
                                       ? (ch >= 'a' && ch <= 'z') || ch == '-'
                                       : digit || ch == '.' || ch == 'p';
    if (!begin)
      return;
    beginCell(std::string(1, ch));
  } else
    return;
  e.consumed = true;
  invalid();
}
void AuthoringPageView::clampScroll() {
  scroll_.x =
      std::clamp(scroll_.x, 0., std::max(0., content_.x - canvas_.getWidth()));
  scroll_.y =
      std::clamp(scroll_.y, 0., std::max(0., content_.y - canvas_.getHeight()));
}
CRect AuthoringPageView::scrollbar(bool horizontal) const {
  double view = horizontal ? canvas_.getWidth() : canvas_.getHeight(),
         content = horizontal ? content_.x : content_.y,
         offset = horizontal ? scroll_.x : scroll_.y;
  if (content <= view)
    return {};
  double length = std::max(18., view * view / content),
         at = (view - length) * offset / (content - view);
  return horizontal ? toolRect(canvas_.left + at, canvas_.bottom - 5, length, 4)
                    : toolRect(canvas_.right - 5, canvas_.top + at, 4, length);
}
void AuthoringPageView::moveScrollbar(CPoint p) {
  bool horizontal = scrollDrag_ == 2;
  auto thumb = scrollbar(horizontal);
  double view = horizontal ? canvas_.getWidth() : canvas_.getHeight(),
         content = horizontal ? content_.x : content_.y,
         length = horizontal ? thumb.getWidth() : thumb.getHeight();
  double pos =
      (horizontal ? p.x - canvas_.left : p.y - canvas_.top) - scrollGrab_;
  double value = pos / std::max(1., view - length) * (content - view);
  if (horizontal)
    scroll_.x = value;
  else
    scroll_.y = value;
  clampScroll();
  invalid();
}
void AuthoringPageView::revealRow(double y, double h) {
  if (y < scroll_.y)
    scroll_.y = y;
  else if (y + h > scroll_.y + canvas_.getHeight())
    scroll_.y = y + h - canvas_.getHeight();
  clampScroll();
}
void AuthoringPageView::beginCanvas(CRect r, double w, double h) {
  canvas_ = r;
  content_ = {std::max(w, r.getWidth()), std::max(h, r.getHeight())};
  clampScroll();
  context_->saveGlobalState();
  context_->getClipRect(previousClip_);
  auto clip = r;
  clip.bound(previousClip_);
  context_->setClipRect(clip);
}
void AuthoringPageView::endCanvas() {
  context_->restoreGlobalState();
  // Portable overlay scrollers retain access to the full native document.
  for (bool horizontal : {false, true}) {
    auto thumb = scrollbar(horizontal);
    if (!thumb.isEmpty())
      fill(thumb, 0x737373, .75);
  }
}
void AuthoringPageView::stopAudition() {
  auditionTimer_ = nullptr;
  if (auditionToken_ && callbacks_.stopAuthoringPreview)
    callbacks_.stopAuthoringPreview(auditionToken_);
  auditionToken_ = 0;
  auditionRow_ = -1;
  invalid();
}
void AuthoringPageView::startAudition() {
  if (state_.playing || !callbacks_.startAuthoringPreview ||
      !callbacks_.stopAuthoringPreview || !callbacks_.authoringPreviewPosition)
    return;
  auto plan = kind_ == AuthoringPage::Phrases
                  ? phraseAudition(state_, phrases.phrase())
                  : assemble.audition();
  if (plan.events.empty()) {
    error(false);
    return;
  }
  stopAudition();
  auditionPlan_ = std::move(plan);
  auditionRow_ = int(auditionPlan_.firstRow);
  auditionBank_ = state_.activePhraseBankId;
  auditionSlot_ = state_.selectedPhrase;
  auditionWhole_ = assemble.wholePreview;
  const bool loop = kind_ == AuthoringPage::Phrases ? state_.phraseLoopPreview
                                                   : state_.assembly.loopPreview;
  auditionToken_ = callbacks_.startAuthoringPreview(
      auditionPlan_.events, auditionPlan_.channel, auditionPlan_.bpm,
      auditionPlan_.ticksPerBeat, uint32_t(auditionPlan_.lastRow + 1), loop);
  if (!auditionToken_) {
    stopAudition();
    return;
  }
  auditionScroll();
  auditionTimer_ = makeOwned<CVSTGUITimer>(
      [this](CVSTGUITimer *) { auditionTick(); },
      33); // Display only; the audio thread owns every repeat boundary.
}
void AuthoringPageView::auditionScroll() {
  if (kind_ == AuthoringPage::Phrases)
    revealRow(30 + auditionRow_ * 22, 22);
  else if (auditionWhole_)
    revealRow(assemble.pixelYForRow(std::size_t(auditionRow_)), 6);
  invalid();
}
void AuthoringPageView::auditionTick() {
  if (auditionRow_ < 0)
    return;
  if (state_.playing || state_.activePhraseBankId != auditionBank_ ||
      state_.selectedPhrase != auditionSlot_) {
    stopAudition();
    return;
  }
  const auto row = callbacks_.authoringPreviewPosition(auditionToken_);
  if (row < 0) {
    stopAudition();
    return;
  }
  auditionRow_ = int(row);
  auditionScroll();
}
} // namespace s3g::tracker::editor
