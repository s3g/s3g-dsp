#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace s3g::tracker::editor {

// Logical, top-left-origin coordinates. No AppKit or VSTGUI types cross this
// boundary. The Mac pilot records the authoritative editor's drawing commands;
// later page ports can produce the same list from their C++ presenters.
struct Point { double x = 0.0, y = 0.0; };
struct Rect { double x = 0.0, y = 0.0, width = 0.0, height = 0.0; };
struct Color { uint8_t red = 0, green = 0, blue = 0, alpha = 255; };
enum class Alignment { Left, Center, Right };
enum class Primitive { FillRect, StrokeRect, FillEllipse, Polyline, Text };

inline double alignedTextX(Rect rect, double textWidth, Alignment alignment)
{
    // Cocoa clips an over-wide single line from its start, even for a
    // right/center-aligned paragraph. Do not move the beginning off-screen.
    const double extra = std::max(0.0, rect.width - textWidth);
    return rect.x + (alignment == Alignment::Right ? extra
        : alignment == Alignment::Center ? extra * 0.5 : 0.0);
}

struct DrawCommand {
    Primitive primitive = Primitive::FillRect;
    Rect rect;
    Color color;
    double lineWidth = 1.0;
    std::vector<Point> points;
    std::string text;
    std::string fontName;
    double fontSize = 12.0;
    // Absolute baseline, computed by the source presenter, never guessed by
    // the backend. The text rectangle supplies horizontal alignment/clipping.
    double baseline = 0.0;
    Alignment alignment = Alignment::Left;
};

class DisplayList {
public:
    void shape(Primitive primitive, Rect rect, Color color, double width = 1.0)
    {
        DrawCommand command;
        command.primitive = primitive;
        command.rect = rect;
        command.color = color;
        command.lineWidth = width;
        commands_.push_back(std::move(command));
    }

    void polyline(std::vector<Point> points, Color color, double width)
    {
        if (points.size() < 2u) return;
        DrawCommand command;
        command.primitive = Primitive::Polyline;
        command.points = std::move(points);
        command.color = color;
        command.lineWidth = width;
        commands_.push_back(std::move(command));
    }

    void text(std::string text, Rect rect, Color color, std::string fontName,
        double fontSize, double baseline, Alignment alignment)
    {
        if (text.empty() || rect.width <= 0.0 || rect.height <= 0.0) return;
        DrawCommand command;
        command.primitive = Primitive::Text;
        command.text = std::move(text);
        command.rect = rect;
        command.color = color;
        command.fontName = std::move(fontName);
        command.fontSize = fontSize;
        command.baseline = baseline;
        command.alignment = alignment;
        commands_.push_back(std::move(command));
    }

    const std::vector<DrawCommand>& commands() const noexcept { return commands_; }

private:
    std::vector<DrawCommand> commands_;
};

} // namespace s3g::tracker::editor
