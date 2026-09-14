#include "s3g_tracker_vstgui_drawing.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"

#include <map>

namespace s3g::tracker::editor {
namespace {
VSTGUI::CRect bounds(Rect rect)
{
    return {rect.x, rect.y, rect.x + rect.width, rect.y + rect.height};
}
VSTGUI::CColor color(Color value)
{
    return {value.red, value.green, value.blue, value.alpha};
}
}

void drawDisplayList(VSTGUI::CDrawContext& context, const DisplayList& list,
    FontFactory fontFactory)
{
    using namespace VSTGUI;
    context.saveGlobalState();
    // Preserve sub-pixel lines and the grid's independent magnification.
    context.setDrawMode(kAntiAliasing | kNonIntegralMode);
    context.setLineStyle(kLineSolid);
    std::map<std::pair<std::string, double>, SharedPointer<CFontDesc>> fonts;
    for (const auto& command : list.commands()) {
        const auto rect = bounds(command.rect);
        context.setFillColor(color(command.color));
        context.setFrameColor(color(command.color));
        context.setLineWidth(command.lineWidth);
        context.setLineStyle(command.dashes.empty() && !command.roundCaps ? kLineSolid
            : CLineStyle(command.roundCaps ? CLineStyle::kLineCapRound : CLineStyle::kLineCapButt,
                CLineStyle::kLineJoinMiter, 0,
                command.dashes));
        switch (command.primitive) {
        case Primitive::FillRect:
            context.drawRect(rect, kDrawFilled);
            break;
        case Primitive::StrokeEllipse:
        case Primitive::StrokeRect: {
            // CDrawContext::drawRect shrinks stroked rectangles by one point.
            // A path preserves the Cocoa rectangle's exact centerline.
            auto path = owned(context.createGraphicsPath());
            if (path) {
                if (command.primitive == Primitive::StrokeEllipse) path->addEllipse(rect);
                else path->addRect(rect);
                context.drawGraphicsPath(path, CDrawContext::kPathStroked);
            }
            break;
        }
        case Primitive::FillEllipse:
            context.drawEllipse(rect, kDrawFilled);
            break;
        case Primitive::FillPolygon:
        case Primitive::Polyline: {
            CDrawContext::PointList points;
            points.reserve(command.points.size());
            for (auto point : command.points) points.emplace_back(point.x, point.y);
            context.drawPolygon(points, command.primitive == Primitive::FillPolygon ? kDrawFilled : kDrawStroked);
            break;
        }
        case Primitive::Text: {
            auto& font = fonts[{command.fontName, command.fontSize}];
            if (!font) font = fontFactory
                ? fontFactory(command.fontName, command.fontSize)
                : makeOwned<CFontDesc>(command.fontName.c_str(), command.fontSize);
            context.setFont(font);
            context.setFontColor(color(command.color));
            const double x = alignedTextX(command.rect,
                context.getStringWidth(command.text.c_str()), command.alignment);
            CRect clip;
            context.getClipRect(clip);
            auto textClip = rect;
            textClip.bound(clip);
            context.setClipRect(textClip);
            context.drawString(command.text.c_str(), CPoint(x, command.baseline));
            context.setClipRect(clip);
            break;
        }
        }
    }
    context.restoreGlobalState();
}

} // namespace s3g::tracker::editor
