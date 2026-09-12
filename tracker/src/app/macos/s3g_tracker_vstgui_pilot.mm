#include "s3g_tracker_vstgui_pilot.h"
#include "../../editor/s3g_tracker_vstgui_drawing.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/lib/platform/mac/coregraphicsdevicecontext.h"
#include "vstgui/lib/platform/mac/cfontmac.h"
#include "vstgui/lib/platform/mac/macglobals.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <tuple>

namespace s3g::tracker::editor {
namespace {
thread_local DisplayList* currentList = nullptr;
std::array<std::atomic<uint64_t>, static_cast<unsigned>(PilotSurface::Count)> draws {};

// Pinned VSTGUI resolves CFontDesc names as *families* on macOS. The source
// editor supplies an exact face (including its existing safe fallback), so
// retain that face while using VSTGUI's normal CoreText font painter. Otherwise
// a PostScript face name can silently become Helvetica or lose its weight.
class ExactMacFont final : public VSTGUI::CoreTextFont {
public:
    ExactMacFont(const std::string& name, double size)
        : CoreTextFont("Menlo", size, 0)
    {
        NSString* face = [NSString stringWithUTF8String:name.c_str()];
        NSFont* source = [NSFont fontWithName:face size:size];
        if (!source) source = [NSFont monospacedSystemFontOfSize:size
            weight:NSFontWeightRegular];
        if (fontRef) CFRelease(fontRef);
        fontRef = CTFontCreateCopyWithAttributes((__bridge CTFontRef)source,
            size, nullptr, nullptr);
        ascent = CTFontGetAscent(fontRef);
        descent = CTFontGetDescent(fontRef);
        leading = CTFontGetLeading(fontRef);
        capHeight = CTFontGetCapHeight(fontRef);
    }
};

class ExactMacFontDesc final : public VSTGUI::CFontDesc {
public:
    ExactMacFontDesc(const std::string& name, double size) : CFontDesc(name.c_str(), size)
    {
        platformFont = VSTGUI::makeOwned<ExactMacFont>(name, size);
    }
};

VSTGUI::SharedPointer<VSTGUI::CFontDesc> makeExactFont(const std::string& name, double size)
{
    return VSTGUI::makeOwned<ExactMacFontDesc>(name, size);
}
}

Rect logicalRect(NSRect r) { return {r.origin.x, r.origin.y, r.size.width, r.size.height}; }

Color resolvedColor(NSColor* source)
{
    // VSTGUI's pinned Mac backend interprets CColor in the main display's
    // ColorSync space, not Generic RGB. Convert before quantizing or the
    // original Night Tracker grays and semantic colors visibly darken.
    NSColorSpace* space = [[NSColorSpace alloc]
        initWithCGColorSpace:VSTGUI::GetCGColorSpace()];
    NSColor* rgb = [source colorUsingColorSpace:space];
    auto channel = [](CGFloat value) {
        return static_cast<uint8_t>(std::round(std::clamp<double>(value, 0.0, 1.0) * 255.0));
    };
    return {channel(rgb.redComponent), channel(rgb.greenComponent),
        channel(rgb.blueComponent), channel(rgb.alphaComponent)};
}

DisplayList* activeDisplayList() { return currentList; }

void recordText(NSString* text, NSRect rect, NSColor* color, NSFont* font,
    NSTextAlignment alignment, bool fixedLineHeight)
{
    if (!currentList) return;
    // Preserve NSString's integral placement, including the legacy typesetter
    // metrics of AppKit's monospaced UI fallback. Modern multiline TextKit
    // adds leading differently for those aliases. The bundled face uses its
    // ordinary font baseline. Both paths are checked against Cocoa PDF glyph
    // positions, not tuned with a blanket pixel offset.
    using Key = std::tuple<std::string, double, bool>;
    static thread_local std::map<Key, double> baselines;
    Key key {font.fontName.UTF8String ?: "Menlo", font.pointSize, fixedLineHeight};
    auto found = baselines.find(key);
    if (found == baselines.end()) {
        NSLayoutManager* layout = [[NSLayoutManager alloc] init];
        double offset = [layout defaultBaselineOffsetForFont:font];
        if (fixedLineHeight && [font.fontName hasPrefix:@".AppleSystemUIFont"]) {
            NSMutableParagraphStyle* style = [[NSMutableParagraphStyle alloc] init];
            style.minimumLineHeight = style.maximumLineHeight =
                std::ceil(font.ascender - font.descender + font.leading);
            NSTextStorage* storage = [[NSTextStorage alloc] initWithString:@"H"
                attributes:@{NSFontAttributeName:font, NSParagraphStyleAttributeName:style}];
            layout.typesetterBehavior = NSTypesetterBehavior_10_2_WithCompatibility;
            NSTextContainer* container = [[NSTextContainer alloc]
                initWithContainerSize:NSMakeSize(1000.0, 1000.0)];
            container.lineFragmentPadding = 0.0;
            [storage addLayoutManager:layout];
            [layout addTextContainer:container];
            [layout ensureLayoutForTextContainer:container];
            offset = [layout locationForGlyphAtIndex:0].y;
        }
        found = baselines.emplace(key, offset).first;
    }
    const double baseline = NSMinY(rect) + found->second;
    currentList->text(text.UTF8String ?: "", logicalRect(rect), resolvedColor(color),
        font.fontName.UTF8String ?: "Menlo", font.pointSize, baseline,
        alignment == NSTextAlignmentRight ? Alignment::Right
        : alignment == NSTextAlignmentCenter ? Alignment::Center : Alignment::Left);
}

struct MacDrawScope::Impl {
    DisplayList list;
    DisplayList* previous = currentList;
    VSTGUI::PlatformGraphicsDeviceContextPtr deviceContext;
    std::unique_ptr<VSTGUI::CDrawContext> context;
    PilotSurface surface;

    Impl(NSView* view, NSRect dirtyRect, PilotSurface which) : surface(which)
    {
        using namespace VSTGUI;
        CGContextRef cg = NSGraphicsContext.currentContext.CGContext;
        if (!cg) return;
        auto device = getPlatformFactory().getGraphicsDeviceFactory()
            .getDeviceForScreen(DefaultScreenIdentifier);
        if (!device) return;
        auto cgDevice = std::static_pointer_cast<CoreGraphicsDevice>(device);
        deviceContext = std::make_shared<CoreGraphicsDeviceContext>(*cgDevice, cg);
        auto r = logicalRect(view.bounds);
        context = std::make_unique<CDrawContext>(deviceContext,
            CRect(r.x, r.y, r.x + r.width, r.y + r.height),
            view.window.backingScaleFactor ?: 1.0);
        auto clip = logicalRect(NSIntersectionRect(view.bounds, dirtyRect));
        context->setClipRect(CRect(clip.x, clip.y,
            clip.x + clip.width, clip.y + clip.height));
        currentList = &list;
    }

    ~Impl()
    {
        currentList = previous;
        if (!context) return; // Cocoa was used if no drawing device was available.
        deviceContext->beginDraw();
        drawDisplayList(*context, list, makeExactFont);
        deviceContext->endDraw();
        draws[static_cast<unsigned>(surface)].fetch_add(1u, std::memory_order_relaxed);
    }
};

MacDrawScope::MacDrawScope(NSView* view, NSRect dirtyRect, PilotSurface surface)
    : impl_(std::make_unique<Impl>(view, dirtyRect, surface)) {}
MacDrawScope::~MacDrawScope() = default;

} // namespace s3g::tracker::editor

// Read-only smoke-test probe: prove that the opt-in build really drew each
// surface with VSTGUI, rather than silently exercising the Cocoa fallback.
extern "C" uint64_t s3g_tracker_vstgui_pilot_draw_count(unsigned surface)
{
    using namespace s3g::tracker::editor;
    return surface < draws.size() ? draws[surface].load(std::memory_order_relaxed) : 0u;
}
