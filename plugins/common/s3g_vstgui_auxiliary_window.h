#pragma once
#include "s3g_vstgui_foundation.h"
#include <memory>

namespace s3g::portable_gui::foundation {
// Main-thread-only, independent native window for a secondary canvas.
// Closing hides the window; ownership remains with the main editor.
class AuxiliaryWindow {
public:
  AuxiliaryWindow(const char *title, uint32_t width, uint32_t height);
  ~AuxiliaryWindow();
  AuxiliaryWindow(const AuxiliaryWindow &) = delete;
  AuxiliaryWindow &operator=(const AuxiliaryWindow &) = delete;
  bool attach(ContentView *view);
  bool show(void *adjacentNativeView = nullptr);
  void hide();
  bool visible() const;
  ContentView *contentView() const;
  void resized(uint32_t width, uint32_t height);
  void closed();

private:
  struct Native;
  std::unique_ptr<Native> native_;
  std::unique_ptr<EditorHost> editor_;
  uint32_t width_, height_;
};
} // namespace s3g::portable_gui::foundation
