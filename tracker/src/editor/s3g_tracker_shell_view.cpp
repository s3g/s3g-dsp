#include "s3g_tracker_shell_view.h"
#include "vstgui/lib/events.h"
namespace s3g::tracker::editor {
using namespace VSTGUI;
namespace {
CRect cr(Rect r) { return toolRect(r.x, r.y, r.width, r.height); }
} // namespace
ShellView::ShellView(ShellController &model, ShellServices services)
    : ToolPageView(services.tools), model_(model),
      platform_(std::move(services)) {
  if (platform_.tabFont.name.empty())
    platform_.tabFont = services_.font(11.5);
  if (platform_.statusFont.name.empty())
    platform_.statusFont = services_.font(8.5);
  if (platform_.bpmFont.name.empty())
    platform_.bpmFont = services_.font(10);
  if (platform_.placeholderFont.name.empty())
    platform_.placeholderFont = services_.font(13);
}
void ShellView::text(const std::string &text, Rect r, const GridFont &f,
                     uint32_t rgb, Alignment align, bool center) {
  auto font = f.name.empty() ? services_.font(10) : f;
  DisplayList list;
  // Native action buttons center the line box with a half-point upward bias.
  double baseline = r.y + font.baseline +
                    (center ? (r.height - font.lineHeight) * .5 - .5 : 0);
  list.text(text, r, color(rgb), font.name, font.size, baseline, align);
  drawDisplayList(*context_, list, services_.fontFactory);
}
void ShellView::drawPage() {
  auto l = model_.layout(getWidth(), getHeight());
  fill(getViewSize(), 0x060606);
  for (std::size_t i = 0; i < kShellPageCount; ++i) {
    auto r = cr(l.tabs[i]);
    auto box = CRect(r).inset(.5, .5);
    bool live = model_.selected() == ShellPage(i);
    bool down = pressedTab_ == int(i) && toolContains(r, hover_);
    uint32_t rgb =
        down ? 0x424242
             : live ? 0x7fd7e8 : toolContains(r, hover_) ? 0x353535 : 0x262626;
    double alpha = live && !down ? .16 : 1;
    fill(box, rgb, alpha);
    // Cocoa NSFrameRect repaints its fill color, including alpha.
    fill(toolRect(box.left, box.top, box.getWidth(), 1), rgb, alpha);
    fill(toolRect(box.left, box.bottom - 1, box.getWidth(), 1), rgb, alpha);
    fill(toolRect(box.left, box.top + 1, 1, box.getHeight() - 2), rgb, alpha);
    fill(toolRect(box.right - 1, box.top + 1, 1, box.getHeight() - 2), rgb,
         alpha);
    if (live)
      fill(toolRect(box.left + 1, box.bottom - 3, box.getWidth() - 2, 2),
           0x7fd7e8);
    text(ShellController::title(ShellPage(i)), l.tabs[i], platform_.tabFont,
         live ? 0x7fd7e8 : 0xbababa, Alignment::Center, true);
    hit(r, ShellController::title(ShellPage(i)), [] {});
  }
  if (ShellController::canDetach(model_.selected()))
    button(cr(l.detach), "detach",
           model_.detached(model_.selected()) ? "↙" : "↗", [this] {
             if (platform_.toggleDetach)
               platform_.toggleDetach(model_.selected());
           });
  // Match native NSTextField padding and truncating-head status behavior.
  auto status = [&](std::string s, Rect r, GridFont f, uint32_t rgb,
                    bool head) {
    r.x += 2;
    r.width -= 4;
    auto font = services_.fontFactory
                    ? services_.fontFactory(f.name, f.size)
                    : portable_gui::foundation::makeUiFont(f.size);
    context_->setFont(font);
    if (head && context_->getStringWidth(s.c_str()) > r.width) {
      while (!s.empty() &&
             context_->getStringWidth(("…" + s).c_str()) > r.width) {
        std::size_t n = 1;
        while (n < s.size() && (uint8_t(s[n]) & 0xc0) == 0x80)
          ++n;
        s.erase(0, n);
      }
      s = "…" + s;
    }
    text(s, r, f, rgb, Alignment::Right);
  };
  status(model_.bpmText(), l.bpm, platform_.bpmFont, 0x929292, false);
  if (l.eventsVisible)
    status(model_.eventText(), l.events, platform_.statusFont, 0x878787, true);
  if (model_.detached(model_.selected()))
    text("THIS PAGE IS OPEN IN A DETACHED WINDOW", l.placeholder,
         platform_.placeholderFont, 0x878787, Alignment::Center);
}
void ShellView::onMouseDownEvent(MouseDownEvent &e) {
  if (e.buttonState.isLeft()) {
    auto l = model_.layout(getWidth(), getHeight());
    for (std::size_t i = 0; i < kShellPageCount; ++i)
      if (toolContains(cr(l.tabs[i]), e.mousePosition)) {
        pressedTab_ = int(i);
        doubleClick_ = e.clickCount >= 2;
        hover_ = e.mousePosition;
        invalid();
        e.consumed = true;
        return;
      }
  }
  ToolPageView::onMouseDownEvent(e);
}
void ShellView::onMouseUpEvent(MouseUpEvent &e) {
  if (pressedTab_ >= 0) {
    auto i = std::size_t(pressedTab_);
    pressedTab_ = -1;
    if (toolContains(cr(model_.layout(getWidth(), getHeight()).tabs[i]),
                     e.mousePosition) &&
        platform_.selectPage)
      platform_.selectPage(ShellPage(i), doubleClick_);
    invalid();
    e.consumed = true;
    return;
  }
  ToolPageView::onMouseUpEvent(e);
}
void ShellView::onMouseCancelEvent(MouseCancelEvent &e) {
  stopRefresh();
  e.consumed = true;
}
void ShellView::stopRefresh() {
  pressedTab_ = -1;
  doubleClick_ = false;
  ToolPageView::stopRefresh();
  invalid();
}
} // namespace s3g::tracker::editor
