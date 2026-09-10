#pragma once

// Small drawing/event primitives for literal ports of bespoke Cocoa canvases.
// Layout, state ownership, and DSP behavior remain in the individual editor.
#include "s3g_vstgui_foundation.h"
#include "vstgui/lib/cdrawmethods.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/controls/itexteditlistener.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/dragging.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/idatapackage.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iomanip>
#include <locale>
#include <sstream>
#include <vector>

namespace s3g::portable_gui::canvas {
using namespace VSTGUI;
using foundation::color;
using foundation::rect;
inline CColor rgba(double r, double g, double b, double a = 1) {
  return CColor(static_cast<uint8_t>(std::clamp(r, 0., 1.) * 255),
                static_cast<uint8_t>(std::clamp(g, 0., 1.) * 255),
                static_cast<uint8_t>(std::clamp(b, 0., 1.) * 255),
                static_cast<uint8_t>(std::clamp(a, 0., 1.) * 255));
}
template <class... Args> std::string format(const char *pattern, Args... args) {
  const int n = std::snprintf(nullptr, 0, pattern, args...);
  if (n <= 0)
    return {};
  std::vector<char> result(static_cast<size_t>(n) + 1);
  std::snprintf(result.data(), result.size(), pattern, args...);
  return {result.data(), static_cast<size_t>(n)};
}
inline std::string upper(std::string text) {
  for (auto &c : text)
    if (c >= 'a' && c <= 'z')
      c -= 'a' - 'A';
  return text;
}

class View : public foundation::ContentView,
             public VSTGUI::IDropTarget,
             public IControlListener,
             public ITextEditListener {
public:
  explicit View(double w, double h) : ContentView(rect(0, 0, w, h)) {
    font = foundation::makeUiFont(foundation::fontMetrics().body);
    titleFont = foundation::makeUiFont(foundation::fontMetrics().title);
    smallFont = foundation::makeUiFont(foundation::fontMetrics().channel);
  }
  ~View() override { stopRefresh(); }
  void draw(CDrawContext *ctx) final {
    context = ctx;
    hits.clear();
    ctx->saveGlobalState();
    ctx->setDrawMode(kAntiAliasing);
    fill(getViewSize(), style.background);
    paint();
    drawPopup();
    ctx->restoreGlobalState();
    context = nullptr;
    setDirty(false);
  }
  virtual void paint() = 0;
  virtual void service() {}
  virtual double refreshPeriodMilliseconds() const { return 33.; }
  virtual bool pointerDown(MouseDownEvent &) { return false; }
  virtual void pointerMove(MouseMoveEvent &) {}
  virtual void pointerUp(MouseUpEvent &) {}
  virtual void wheel(MouseWheelEvent &) {}
  virtual void popupDismissed() {}
  virtual bool dropped(const std::vector<std::string> &, CPoint) {
    return false;
  }
  void startRefresh() override {
    if (!timer)
      timer = makeOwned<CVSTGUITimer>(
          [this](CVSTGUITimer *tick) {
            if (closeNumeric)
              finishNumeric();
            service();
            invalid();
            tick->setFireTime(nextRefreshInterval());
          },
          nextRefreshInterval(), true);
  }
  void stopRefresh() override {
    timer = nullptr;
    finishNumeric();
    finishDrag();
    popupItems.clear();
  }
  void fill(CRect b, CColor c) {
    context->setFillColor(c);
    context->drawRect(b, kDrawFilled);
  }
  void stroke(CRect b, CColor c, double width = 1) {
    context->setFrameColor(c);
    context->setLineWidth(width);
    context->drawRect(b, kDrawStroked);
  }
  void line(CPoint a, CPoint b, CColor c, double width = 1) {
    context->setFrameColor(c);
    context->setLineWidth(width);
    context->drawLine(a, b);
  }
  void ellipse(CRect b, CColor c, bool filled = true, double width = 1) {
    context->setFillColor(c);
    context->setFrameColor(c);
    context->setLineWidth(width);
    context->drawEllipse(b, filled ? kDrawFilled : kDrawStroked);
  }
  void polygon(const std::vector<CPoint> &points, CColor c, bool filled = true,
               double width = 1) {
    if (points.empty())
      return;
    auto path = owned(context->createGraphicsPath());
    if (!path)
      return;
    path->beginSubpath(points[0]);
    for (size_t i = 1; i < points.size(); ++i)
      path->addLine(points[i]);
    if (filled)
      path->closeSubpath();
    context->setFillColor(c);
    context->setFrameColor(c);
    context->setLineWidth(width);
    context->drawGraphicsPath(path, filled ? CDrawContext::kPathFilled
                                           : CDrawContext::kPathStroked);
  }
  void text(std::string value, CRect b, CColor c,
            CHoriTxtAlign align = kLeftText, CFontRef face = nullptr) {
    foundation::drawTextInRect(*context, value, b, c, face ? face : font.get(),
                               align);
  }
  void panel(CRect b, const std::string &title) {
    foundation::drawPanel(*context, b, title, font);
  }
  void hit(CRect b, std::function<void()> action, bool enabled = true) {
    if (enabled)
      hits.push_back({b, std::move(action), {}, {}, {}, {}});
  }
  void drag(CRect b, std::function<void(CPoint)> move,
            std::function<void()> begin = {}, std::function<void()> end = {},
            std::function<void()> reset = {}, bool enabled = true) {
    if (enabled)
      hits.push_back({b,
                      {},
                      std::move(move),
                      std::move(begin),
                      std::move(end),
                      std::move(reset)});
  }
  void button(CRect b, const std::string &label, std::function<void()> action,
              bool active = false, bool enabled = true) {
    foundation::drawButton(*context, b, label, font, active);
    if (!enabled)
      text(label, b, color(0x686868), kCenterText);
    hit(b, std::move(action), enabled);
  }
  void menu(CRect b, const std::string &value, std::vector<std::string> items,
            int selected, std::function<void(int)> apply, bool enabled = true,
            int columns = 1) {
    auto s = style;
    if (!enabled)
      s.value = color(0x686868);
    foundation::drawMenuBox(*context, b, value, font, s);
    hit(
        b,
        [this, b, items = std::move(items), selected, apply = std::move(apply),
         columns] { openPopup(b, items, selected, apply, columns); },
        enabled);
  }
  void openPopup(CRect anchor, std::vector<std::string> items, int selected,
                 std::function<void(int)> apply, int columns = 1,
                 CRect desired = {}, double itemHeight = 20) {
    popupItems = std::move(items);
    popupApply = std::move(apply);
    popupItemHeight = itemHeight;
    popupSelected = selected;
    popupHover = -1;
    popupColumns = std::max(1, columns);
    popupRows =
        std::max(1, static_cast<int>((popupItems.size() + popupColumns - 1) /
                                     popupColumns));
    popupVisibleRows =
        std::min(popupRows, static_cast<int>((getViewSize().getHeight() - 12) /
                                             popupItemHeight));
    popupScroll = std::clamp(selected / popupColumns - popupVisibleRows / 2, 0,
                             popupRows - popupVisibleRows);
    const double w =
        std::min(getViewSize().getWidth() - 12,
                 desired.getWidth() > 0
                     ? desired.getWidth()
                     : popupColumns == 1 ? std::max(120., anchor.getWidth())
                                         : popupColumns * 86.);
    const double h = popupVisibleRows * popupItemHeight;
    const double top = anchor.bottom + h <= getViewSize().bottom - 6
                           ? anchor.bottom
                           : anchor.top - h;
    popupBounds = rect(std::clamp(anchor.left, 6., getViewSize().right - 6 - w),
                       std::clamp(top, 6., getViewSize().bottom - h - 6), w, h);
    if (desired.getWidth() > 0)
      popupBounds = rect(desired.left, desired.top, w, h);
    invalid();
  }
  void number(CRect b, double value, std::function<void(double)> apply,
              bool enabled = true, std::function<void()> begin = {},
              std::function<void()> end = {}) {
    stroke(b, color(0x1a1a1a));
    hit(
        b,
        [this, b, value, apply = std::move(apply), begin = std::move(begin),
         end = std::move(end)] {
          finishNumeric();
          if (!getFrame())
            return;
          numericApply = apply;
          numericEnd = end;
          if (begin)
            begin();
          numeric = new CTextEdit(b, this, 1, format("%.9g", value).c_str());
          numeric->setFont(font);
          numeric->setFontColor(style.value);
          numeric->setBackColor(style.cell);
          numeric->setFrameColor(style.grid);
          numeric->setHoriAlign(kRightText);
          numeric->registerTextEditListener(this);
          getFrame()->addView(numeric);
          numeric->takeFocus();
        },
        enabled);
  }
  void valueChanged(CControl *control) override {
    if (control != numeric || !numericApply)
      return;
    std::istringstream input(numeric->getText().getString());
    input.imbue(std::locale::classic());
    double value = 0;
    if (input >> value && std::isfinite(value)) {
      input >> std::ws;
      if (input.eof())
        numericApply(value);
    }
    invalid();
  }
  void onTextEditPlatformControlTookFocus(CTextEdit *) override {}
  void onTextEditPlatformControlLostFocus(CTextEdit *) override {
    closeNumeric = true;
  }
  void finishNumeric() {
    if (!numeric)
      return;
    numeric->looseFocus();
    auto *field = numeric;
    numeric = nullptr;
    field->unregisterTextEditListener(this);
    if (field->getFrame())
      field->getFrame()->removeView(field);
    if (numericEnd)
      numericEnd();
    numericApply = {};
    numericEnd = {};
    closeNumeric = false;
  }
  void onMouseDownEvent(MouseDownEvent &e) override {
    finishNumeric();
    if (!popupItems.empty()) {
      const int index = popupIndex(e.mousePosition);
      auto apply = popupApply;
      popupItems.clear();
      if (index >= 0 && e.buttonState.isLeft() && apply)
        apply(index);
      popupDismissed();
      invalid();
      e.consumed = true;
      return;
    }
    if (e.buttonState.isLeft())
      for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
        if (!foundation::contains(it->bounds, e.mousePosition))
          continue;
        Hit selected = *it;
        if (e.clickCount == 2 && selected.reset)
          selected.reset();
        else if (selected.action)
          selected.action();
        else if (selected.move) {
          finishDrag();
          currentDrag = std::move(selected);
          if (currentDrag.begin)
            currentDrag.begin();
          currentDrag.move(e.mousePosition);
        }
        invalid();
        e.consumed = true;
        return;
      }
    if (pointerDown(e)) {
      invalid();
      e.consumed = true;
    }
  }
  void onMouseMoveEvent(MouseMoveEvent &e) override {
    if (!popupItems.empty()) {
      popupHover = popupIndex(e.mousePosition);
      invalid();
      e.consumed = true;
    } else if (currentDrag.move) {
      currentDrag.move(e.mousePosition);
      invalid();
      e.consumed = true;
    } else
      pointerMove(e);
  }
  void onMouseUpEvent(MouseUpEvent &e) override {
    finishDrag();
    pointerUp(e);
    invalid();
    e.consumed = true;
  }
  void onMouseWheelEvent(MouseWheelEvent &e) override {
    if (!popupItems.empty()) {
      popupScroll = std::clamp(popupScroll + (e.deltaY < 0 ? 1 : -1), 0,
                               popupRows - popupVisibleRows);
      invalid();
      e.consumed = true;
    } else
      wheel(e);
  }
  SharedPointer<VSTGUI::IDropTarget> getDropTarget() override { return this; }
  DragOperation onDragEnter(DragEventData data) override {
    return files(data.drag).empty() ? DragOperation::None : DragOperation::Copy;
  }
  DragOperation onDragMove(DragEventData data) override {
    return onDragEnter(data);
  }
  void onDragLeave(DragEventData) override {}
  bool onDrop(DragEventData data) override {
    const bool result = dropped(files(data.drag), data.pos);
    invalid();
    return result;
  }
  static std::vector<std::string> files(IDataPackage *data) {
    std::vector<std::string> paths;
    if (!data)
      return paths;
    for (uint32_t i = 0; i < data->getCount(); ++i) {
      const void *bytes = nullptr;
      IDataPackage::Type type;
      const uint32_t n = data->getData(i, bytes, type);
      if (type != IDataPackage::kFilePath || !bytes || n == 0)
        continue;
      const auto *first = static_cast<const char *>(bytes);
      paths.emplace_back(first, std::find(first, first + n, '\0'));
    }
    return paths;
  }

protected:
  CDrawContext *context = nullptr;
  foundation::Palette style = foundation::palette();
  SharedPointer<CFontDesc> font, titleFont, smallFont;

private:
  uint32_t nextRefreshInterval() {
    refreshFraction += std::clamp(refreshPeriodMilliseconds(), 1., 1000.);
    const auto interval = static_cast<uint32_t>(refreshFraction);
    refreshFraction -= interval;
    return interval;
  }
  double refreshFraction = 0.;
  struct Hit {
    CRect bounds;
    std::function<void()> action;
    std::function<void(CPoint)> move;
    std::function<void()> begin, end, reset;
  };
  void finishDrag() {
    auto end = currentDrag.end;
    currentDrag = {};
    if (end)
      end();
  }
  int popupIndex(CPoint p) const {
    if (!foundation::contains(popupBounds, p))
      return -1;
    const int col =
        std::clamp(static_cast<int>((p.x - popupBounds.left) /
                                    (popupBounds.getWidth() / popupColumns)),
                   0, popupColumns - 1);
    const int row =
        std::clamp(static_cast<int>((p.y - popupBounds.top) / popupItemHeight),
                   0, popupVisibleRows - 1);
    const int i = (row + popupScroll) * popupColumns + col;
    return i < static_cast<int>(popupItems.size()) ? i : -1;
  }
  void drawPopup() {
    if (popupItems.empty())
      return;
    fill(CRect(popupBounds).inset(-2, -2), color(0x080808));
    fill(popupBounds, color(0x151515));
    stroke(popupBounds, color(0x6c6c6c));
    for (int row = 0; row < popupVisibleRows; ++row)
      for (int col = 0; col < popupColumns; ++col) {
        const int i = (row + popupScroll) * popupColumns + col;
        if (i >= static_cast<int>(popupItems.size()))
          continue;
        auto b =
            rect(popupBounds.left + col * popupBounds.getWidth() / popupColumns,
                 popupBounds.top + row * popupItemHeight,
                 popupBounds.getWidth() / popupColumns, popupItemHeight);
        if (i == popupHover || i == popupSelected)
          fill(CRect(b).inset(1, 1),
               color(i == popupHover ? 0x343434 : 0x292929));
        else if ((row + popupScroll) % 2)
          fill(CRect(b).inset(1, 1), style.strip);
        if (i == popupHover || i == popupSelected)
          fill(rect(b.left + 2, b.top + 2, 3, b.getHeight() - 4), style.fill);
        if (row)
          line({b.left, b.top}, {b.right, b.top}, color(0x3a3a3a));
        b.left += 9;
        b.right -= 5;
        text(popupItems[static_cast<size_t>(i)], b, style.label);
      }
  }
  std::vector<Hit> hits;
  Hit currentDrag;
  SharedPointer<CVSTGUITimer> timer;
  std::vector<std::string> popupItems;
  std::function<void(int)> popupApply;
  CRect popupBounds;
  int popupSelected = -1, popupHover = -1, popupColumns = 1, popupRows = 1,
      popupVisibleRows = 1, popupScroll = 0;
  double popupItemHeight = 20;
  CTextEdit *numeric = nullptr;
  std::function<void(double)> numericApply;
  std::function<void()> numericEnd;
  bool closeNumeric = false;
};
} // namespace s3g::portable_gui::canvas
