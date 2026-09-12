#include "s3g_vstgui_auxiliary_window.h"
#include <algorithm>
#include <cmath>
#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
@interface S3GVstguiAuxiliaryDelegate : NSObject <NSWindowDelegate> {
@public
  s3g::portable_gui::foundation::AuxiliaryWindow *owner;
}
@end
@implementation S3GVstguiAuxiliaryDelegate
- (BOOL)windowShouldClose:(NSWindow *)sender {
  (void)sender;
  if (owner)
    owner->closed();
  return NO;
}
- (void)windowDidResize:(NSNotification *)notification {
  NSWindow *window = [notification object];
  NSSize size = [[window contentView] bounds].size;
  if (owner)
    owner->resized(static_cast<uint32_t>(std::lround(size.width)),
                   static_cast<uint32_t>(std::lround(size.height)));
}
@end
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace s3g::portable_gui::foundation {
#if defined(__APPLE__)
struct AuxiliaryWindow::Native {
  NSPanel *panel = nil;
  S3GVstguiAuxiliaryDelegate *delegate = nil;
  ~Native() {
    if (delegate)
      delegate->owner = nullptr;
    [panel setDelegate:nil];
    [[panel parentWindow] removeChildWindow:panel];
    [panel orderOut:nil];
    [panel close];
    [panel release];
    [delegate release];
  }
};
#elif defined(_WIN32)
struct AuxiliaryWindow::Native {
  HWND window = nullptr;
  HMODULE module = nullptr;
  bool classUser = false;
  inline static unsigned classUsers = 0;
  uint32_t nativeWidth = 0, nativeHeight = 0;
  ~Native() {
    if (window)
      DestroyWindow(window);
    if (classUser && --classUsers == 0)
      UnregisterClassW(L"S3GVstguiAuxiliaryWindow", module);
  }
  static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w,
                                    LPARAM l) {
    auto *owner = reinterpret_cast<AuxiliaryWindow *>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      owner = static_cast<AuxiliaryWindow *>(
          reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
      SetWindowLongPtrW(window, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(owner));
    }
    if (owner && owner->native_) {
      auto &native = *owner->native_;
      if (message == WM_CLOSE) {
        owner->closed();
        return 0;
      }
      if (message == WM_SIZE && w != SIZE_MINIMIZED) {
        owner->resized(LOWORD(l), HIWORD(l));
        return 0;
      }
      if (message == WM_GETMINMAXINFO) {
        auto *limits = reinterpret_cast<MINMAXINFO *>(l);
        RECT minimum{
            0, 0, LONG(std::ceil(native.nativeWidth * kMinimumEditorScale)),
            LONG(std::ceil(native.nativeHeight * kMinimumEditorScale))};
        RECT maximum{0, 0, LONG(native.nativeWidth * kMaximumEditorScale),
                     LONG(native.nativeHeight * kMaximumEditorScale)};
        const DWORD flags = WS_OVERLAPPEDWINDOW;
        AdjustWindowRectEx(&minimum, flags, FALSE, WS_EX_TOOLWINDOW);
        AdjustWindowRectEx(&maximum, flags, FALSE, WS_EX_TOOLWINDOW);
        limits->ptMinTrackSize = {minimum.right - minimum.left,
                                  minimum.bottom - minimum.top};
        limits->ptMaxTrackSize = {maximum.right - maximum.left,
                                  maximum.bottom - maximum.top};
        return 0;
      }
      if (message == WM_SIZING) {
        auto *bounds = reinterpret_cast<RECT *>(l);
        RECT frame{0, 0, 0, 0};
        AdjustWindowRectEx(&frame, WS_OVERLAPPEDWINDOW, FALSE,
                           WS_EX_TOOLWINDOW);
        const LONG extraW = frame.right - frame.left,
                   extraH = frame.bottom - frame.top;
        const double ratio = double(native.nativeWidth) / native.nativeHeight;
        if (w == WMSZ_TOP || w == WMSZ_BOTTOM) {
          bounds->right = bounds->left + extraW +
                          LONG(std::lround(
                              (bounds->bottom - bounds->top - extraH) * ratio));
        } else {
          const LONG height =
              extraH + LONG(std::lround(
                           (bounds->right - bounds->left - extraW) / ratio));
          if (w == WMSZ_TOPLEFT || w == WMSZ_TOPRIGHT)
            bounds->top = bounds->bottom - height;
          else
            bounds->bottom = bounds->top + height;
        }
        return TRUE;
      }
      if (message == WM_DPICHANGED) {
        const RECT *suggested = reinterpret_cast<const RECT *>(l);
        SetWindowPos(window, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        return 0;
      }
    }
    return DefWindowProcW(window, message, w, l);
  }
};
#else
struct AuxiliaryWindow::Native {};
#endif

AuxiliaryWindow::AuxiliaryWindow(const char *title, uint32_t width,
                                 uint32_t height)
    : native_(std::make_unique<Native>()), width_(width), height_(height) {
#if defined(__APPLE__)
  native_->panel = [[NSPanel alloc]
      initWithContentRect:NSMakeRect(0, 0, width, height)
                styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                           NSWindowStyleMaskMiniaturizable |
                           NSWindowStyleMaskResizable)
                  backing:NSBackingStoreBuffered
                    defer:NO];
  [native_->panel setReleasedWhenClosed:NO];
  [native_->panel setHidesOnDeactivate:NO];
  [native_->panel setTitle:[NSString stringWithUTF8String:title]];
  [native_->panel
      setContentMinSize:NSMakeSize(std::ceil(width * kMinimumEditorScale),
                                   std::ceil(height * kMinimumEditorScale))];
  [native_->panel setContentMaxSize:NSMakeSize(width * kMaximumEditorScale,
                                               height * kMaximumEditorScale)];
  [native_->panel setContentAspectRatio:NSMakeSize(width, height)];
  native_->delegate = [[S3GVstguiAuxiliaryDelegate alloc] init];
  native_->delegate->owner = this;
  [native_->panel setDelegate:native_->delegate];
#elif defined(_WIN32)
  native_->nativeWidth = width;
  native_->nativeHeight = height;
  HMODULE module = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(&Native::procedure), &module);
  const wchar_t *className = L"S3GVstguiAuxiliaryWindow";
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = Native::procedure;
  wc.hInstance = module;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = className;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return;
  native_->module = module;
  native_->classUser = true;
  ++Native::classUsers;
  const auto wide = pathFromUtf8(title).wstring();
  RECT bounds{0, 0, LONG(width), LONG(height)};
  AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_TOOLWINDOW);
  native_->window = CreateWindowExW(
      WS_EX_TOOLWINDOW, className, wide.c_str(), WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left,
      bounds.bottom - bounds.top, nullptr, nullptr, module, this);
#else
  (void)title;
#endif
}
AuxiliaryWindow::~AuxiliaryWindow() {
  hide();
  // Close VSTGUI before releasing its native parent.
  editor_.reset();
  native_.reset();
}
bool AuxiliaryWindow::attach(ContentView *view) {
  if (!view || editor_)
    return false;
  void *parent = nullptr;
#if defined(__APPLE__)
  parent = [native_->panel contentView];
#elif defined(_WIN32)
  parent = native_->window;
#endif
  if (!parent) {
    view->forget();
    return false;
  }
  editor_ = std::make_unique<EditorHost>(width_, height_, width_, height_);
  if (!editor_->ready()) {
    view->forget();
    editor_.reset();
    return false;
  }
  if (!editor_->attach(view) || !editor_->setParent(parent)) {
    editor_.reset();
    return false;
  }
  return true;
}
bool AuxiliaryWindow::show(void *adjacentNativeView) {
  if (!editor_)
    return false;
#if defined(__APPLE__)
  NSView *adjacent = static_cast<NSView *>(adjacentNativeView);
  NSWindow *parent = [adjacent window];
  if (parent == native_->panel)
    parent = nil;
  NSScreen *screen = [parent screen] ?: [NSScreen mainScreen];
  const NSRect visible = [screen visibleFrame];
  NSRect frame = [native_->panel frame];
  const NSRect nearby = parent ? [parent frame] : visible;
  frame.origin.x =
      std::clamp(NSMaxX(nearby) + 12.0, NSMinX(visible),
                 std::max(NSMinX(visible), NSMaxX(visible) - frame.size.width));
  frame.origin.y = std::clamp(
      NSMaxY(nearby) - frame.size.height, NSMinY(visible),
      std::max(NSMinY(visible), NSMaxY(visible) - frame.size.height));
  [native_->panel setFrame:frame display:NO];
  // REAPER can host the editor in a floating FX window. Raising an unrelated
  // normal-level panel cannot put it above that host. Keep the pop-out above
  // its actual containing window, without imposing a global topmost level.
  NSWindow *previousParent = [native_->panel parentWindow];
  if (previousParent != parent)
    [previousParent removeChildWindow:native_->panel];
  [native_->panel setLevel:parent ? [parent level] : NSNormalWindowLevel];
  if (parent && previousParent != parent)
    [parent addChildWindow:native_->panel ordered:NSWindowAbove];
  [native_->panel makeKeyAndOrderFront:nil];
#elif defined(_WIN32)
  HWND adjacent = static_cast<HWND>(adjacentNativeView);
  if (adjacent) {
    // An owned utility window remains above its REAPER FX window without
    // becoming globally topmost over unrelated applications.
    SetWindowLongPtrW(native_->window, GWLP_HWNDPARENT,
        reinterpret_cast<LONG_PTR>(GetAncestor(adjacent, GA_ROOT)));
    RECT nearby{}, frame{};
    GetWindowRect(GetAncestor(adjacent, GA_ROOT), &nearby);
    GetWindowRect(native_->window, &frame);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(adjacent, MONITOR_DEFAULTTONEAREST),
                    &monitor);
    const LONG x =
        std::clamp(nearby.right + 12L, monitor.rcWork.left,
                   std::max(monitor.rcWork.left,
                            monitor.rcWork.right - (frame.right - frame.left)));
    const LONG y = std::clamp(
        nearby.top, monitor.rcWork.top,
        std::max(monitor.rcWork.top,
                 monitor.rcWork.bottom - (frame.bottom - frame.top)));
    SetWindowPos(native_->window, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER);
  }
  ShowWindow(native_->window, SW_SHOWNORMAL);
#else
  (void)adjacentNativeView;
#endif
  return editor_->setVisible(true);
}
void AuxiliaryWindow::setTitle(const std::string& title) {
#if defined(__APPLE__)
  [native_->panel setTitle:[NSString stringWithUTF8String:title.c_str()]];
#elif defined(_WIN32)
  const auto wide=pathFromUtf8(title.c_str()).wstring();
  SetWindowTextW(native_->window,wide.c_str());
#else
  (void)title;
#endif
}
void AuxiliaryWindow::hide() {
  if (editor_)
    editor_->setVisible(false);
#if defined(__APPLE__)
  [[native_->panel parentWindow] removeChildWindow:native_->panel];
  [native_->panel orderOut:nil];
#elif defined(_WIN32)
  if (native_->window) {
    ShowWindow(native_->window, SW_HIDE);
    SetWindowLongPtrW(native_->window, GWLP_HWNDPARENT, 0);
  }
#endif
}
bool AuxiliaryWindow::visible() const {
#if defined(__APPLE__)
  return [native_->panel isVisible];
#elif defined(_WIN32)
  return native_->window && IsWindowVisible(native_->window);
#else
  return false;
#endif
}
ContentView *AuxiliaryWindow::contentView() const {
  return editor_ ? editor_->contentView() : nullptr;
}
void AuxiliaryWindow::resized(uint32_t width, uint32_t height) {
  if (editor_ && width && height)
    editor_->setSize(width, height);
}
void AuxiliaryWindow::closed() { hide(); }
} // namespace s3g::portable_gui::foundation
