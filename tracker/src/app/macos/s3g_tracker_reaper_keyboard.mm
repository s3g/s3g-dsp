#include "s3g_tracker_reaper_keyboard.h"
#include "s3g_tracker_vstgui_pilot.h"
#include <algorithm>
#include <cstring>
#include <vector>

namespace s3g::tracker::editor {
struct MacReaperTextInput::Impl {
    Register reg;
    NSArray<NSView*>* pages;
    ReaperKeyboardAccelerator accelerator;
    bool acceleratorRegistered = false, infoRegistered = false;
    static std::vector<Impl*>& instances()
    {
        static std::vector<Impl*> list;
        return list;
    }
    static bool ownsTarget(NSView* page, void* target)
    {
        // Only compare known Cocoa objects with the opaque host HWND; never
        // send messages to it. SWELL may address an ancestor of the frame.
        for (NSView* view = (NSView*)page.window.firstResponder; view; view = view.superview)
            if ((__bridge void*)view == target) return true;
        return (__bridge void*)page.window == target;
    }
    NSView* focusedPage(void* target) const
    {
        if (!target) return nil;
        for (NSView* page in pages) {
            if ([page respondsToSelector:@selector(s3gTrackerHasFocusedTextInput)]
                && [(id<S3GTrackerTextInputOwner>)page s3gTrackerHasFocusedTextInput]
                && ownsTarget(page, target))
                return page;
        }
        return nil;
    }
    static int translate(void* message, ReaperKeyboardAccelerator* context)
    {
        if (!message || !context) return 0;
        const auto* self = static_cast<Impl*>(context->user);
        // MSG begins with HWND. Copy that prefix without aliasing the host's
        // full SDK struct as our smaller independently declared type.
        ReaperKeyboardMessage target;
        std::memcpy(&target, message, sizeof(target));
        // -10 asks REAPER to process the original macOS event raw. Do not
        // synthesize keys or toggle transport here: AppKit's text input context
        // must retain selection, IME, repeat, clipboard and text undo behavior.
        return self && self->focusedPage(target.window) ? -10 : 0;
    }
    static int hwndInfo(void* hwnd, intptr_t type)
    {
        if (!hwnd || (type != 0 && type != 1)) return 0;
        for (const auto* self : instances()) {
            if (self->focusedPage(hwnd)) return 1;
        }
        return 0;
    }
    Impl(Register registration, NSArray<NSView*>* views) : reg(registration), pages([views copy])
    {
        if (!reg) return;
        bool shared = std::any_of(instances().begin(), instances().end(),
            [this](const auto* other) { return other->reg == reg && other->infoRegistered; });
        // type 0 identifies a text field; type 1 keeps global shortcuts out of
        // this context too. Other pages/instances/windows keep the host policy.
        infoRegistered = shared || reg("hwnd_info", reinterpret_cast<void*>(hwndInfo)) != 0;
        accelerator.translate = translate;
        accelerator.user = this;
        instances().push_back(this);
        // Ordinary accelerator registration is too late for FX-chain Space:
        // REAPER's built-in FX shortcut handler has already claimed it. The
        // SDK's '<' prefix puts this text-only hook ahead of that handler.
        acceleratorRegistered = reg("<accelerator", &accelerator) != 0;
        if (!acceleratorRegistered)
            acceleratorRegistered = reg("accelerator", &accelerator) != 0;
    }
    ~Impl()
    {
        if (!reg) return;
        if (acceleratorRegistered) reg("-accelerator", &accelerator);
        auto& list = instances();
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
        if (infoRegistered && std::none_of(list.begin(), list.end(),
                [this](const auto* other) { return other->reg == reg && other->infoRegistered; }))
            reg("-hwnd_info", reinterpret_cast<void*>(hwndInfo));
    }
};
MacReaperTextInput::MacReaperTextInput(Register reg, NSArray<NSView*>* pages)
    : impl_(std::make_unique<Impl>(reg, pages)) { }
MacReaperTextInput::~MacReaperTextInput() = default;
}
