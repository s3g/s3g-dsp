#pragma once

#import <Cocoa/Cocoa.h>
#include "s3g/tracker/editor_drawing.h"
#include <memory>

namespace s3g::tracker::editor {

enum class PilotSurface : unsigned { Grid, Gutter, Envelope, PlaybackOverlay, Count };

// Only the four audited Tracker-page draw paths create a scope. Outside one,
// the workspace helpers continue to draw with Cocoa. Events, scroll/zoom,
// inline text editing and the other pages remain native in this first pilot.
class MacDrawScope {
public:
    MacDrawScope(NSView* view, NSRect dirtyRect, PilotSurface surface);
    ~MacDrawScope();
    MacDrawScope(const MacDrawScope&) = delete;
    MacDrawScope& operator=(const MacDrawScope&) = delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

DisplayList* activeDisplayList();
Rect logicalRect(NSRect rect);
Color resolvedColor(NSColor* color);
void recordText(NSString* text, NSRect rect, NSColor* color, NSFont* font,
    NSTextAlignment alignment, bool fixedLineHeight);

} // namespace s3g::tracker::editor
