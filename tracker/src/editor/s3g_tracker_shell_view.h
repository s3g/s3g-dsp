#pragma once
#include "s3g/tracker/editor_shell.h"
#include "s3g_tracker_tool_page.h"
namespace s3g::tracker::editor {
struct ShellServices {
  ToolPageServices tools;
  // Separate native grid/status faces; keep the original header typography.
  GridFont tabFont, statusFont, bpmFont, placeholderFont;
  std::function<void(ShellPage, bool)> selectPage;
  std::function<void(ShellPage)> toggleDetach;
};
class ShellView final : public ToolPageView {
public:
  ShellView(ShellController &, ShellServices);
  void drawPage() override;
  void onMouseDownEvent(VSTGUI::MouseDownEvent &) override;
  void onMouseUpEvent(VSTGUI::MouseUpEvent &) override;
  void onMouseCancelEvent(VSTGUI::MouseCancelEvent &) override;
  void stopRefresh() override;

private:
  ShellController &model_;
  ShellServices platform_;
  int pressedTab_ = -1;
  bool doubleClick_ = false;
  void text(const std::string &, Rect, const GridFont &, uint32_t, Alignment,
            bool center = false);
};
} // namespace s3g::tracker::editor
