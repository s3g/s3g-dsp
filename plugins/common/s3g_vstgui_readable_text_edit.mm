#include "s3g_vstgui_readable_text_edit.h"
#include "vstgui/lib/platform/mac/cocoa/cocoatextedit.h"
#import <Cocoa/Cocoa.h>
#include <memory>

namespace s3g::portable_gui {
namespace {
// A window shares its field editor with other controls, including host UI.
// Observe only this VSTGUI field, and never change system/window defaults.
class SelectionScope {
public:
    explicit SelectionScope(NSTextField* field) : field_([field retain]) {
        auto* center = [NSNotificationCenter defaultCenter];
        begin_ = [center addObserverForName:NSControlTextDidBeginEditingNotification
            object:field_ queue:nil usingBlock:^(NSNotification*) { apply(); }];
        end_ = [center addObserverForName:NSControlTextDidEndEditingNotification
            object:field_ queue:nil usingBlock:^(NSNotification*) { restore(); }];
        // AppKit may not report "begin editing" until the first keystroke.
        // Initial click/select-all must already have readable highlighting.
        auto* shared = [[field_ window] fieldEditor:YES forObject:field_];
        if (shared) selectionChanged_ = [center addObserverForName:NSTextViewDidChangeSelectionNotification
            object:shared queue:nil usingBlock:^(NSNotification*) { apply(); }];
        apply(); // Also handles a platform field that already has focus.
    }
    ~SelectionScope() {
        auto* center = [NSNotificationCenter defaultCenter];
        [center removeObserver:begin_]; [center removeObserver:end_];
        if (selectionChanged_) [center removeObserver:selectionChanged_];
        restore(); [field_ release];
    }
private:
    void apply() {
        auto* current = [field_ currentEditor];
        if (![current isKindOfClass:[NSTextView class]]) return;
        auto* next = static_cast<NSTextView*>(current);
        if (editor_ == next) return;
        restore();
        editor_ = [next retain];
        previous_ = [[editor_ selectedTextAttributes] copy];
        // Preserve unrelated native attributes; make both sides of the
        // selection explicit so macOS accent colours cannot reduce contrast.
        auto* attributes = [previous_ mutableCopy];
        attributes[NSBackgroundColorAttributeName] = [NSColor colorWithSRGBRed:69./255. green:69./255. blue:69./255. alpha:1.];
        attributes[NSForegroundColorAttributeName] = [NSColor colorWithSRGBRed:227./255. green:227./255. blue:227./255. alpha:1.];
        applied_ = [attributes copy]; [attributes release];
        [editor_ setSelectedTextAttributes:applied_];
    }
    void restore() {
        // Do not overwrite a later style installed by another control.
        if (editor_ && [[editor_ selectedTextAttributes] isEqualToDictionary:applied_])
            [editor_ setSelectedTextAttributes:previous_];
        [editor_ release]; editor_ = nil;
        [previous_ release]; previous_ = nil;
        [applied_ release]; applied_ = nil;
    }
    NSTextField* field_ = nil;
    NSTextView* editor_ = nil;
    NSDictionary* previous_ = nil;
    NSDictionary* applied_ = nil;
    id begin_ = nil, end_ = nil, selectionChanged_ = nil;
};

class ReadableNumericTextEdit final : public VSTGUI::CTextEdit {
public:
    using CTextEdit::CTextEdit;
    void takeFocus() override {
        CTextEdit::takeFocus();
        // VSTGUI defers AppKit focus by one main-loop tick. The scoped
        // observer above applies the colours when native editing begins.
        if (!selection_) {
            auto native = getPlatformTextEdit();
            if (auto* cocoa = dynamic_cast<VSTGUI::CocoaTextEdit*>(native.get()))
                selection_ = std::make_unique<SelectionScope>(cocoa->getPlatformControl());
        }
    }
    void looseFocus() override {
        selection_.reset(); // Restore before VSTGUI removes the native field.
        CTextEdit::looseFocus();
    }
private:
    std::unique_ptr<SelectionScope> selection_;
};
} // namespace

VSTGUI::CTextEdit* makeReadableNumericTextEdit(const VSTGUI::CRect& bounds,
    VSTGUI::IControlListener* listener, int32_t tag, const char* text) {
    return new ReadableNumericTextEdit(bounds,listener,tag,text);
}
} // namespace s3g::portable_gui
