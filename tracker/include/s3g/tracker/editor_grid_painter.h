#pragma once
#include "s3g/tracker/editor_grid.h"
#include <functional>

namespace s3g::tracker::editor {
enum class FontWeight { Regular, Medium, Semibold };
struct GridFont {
    std::string name = "IBM Plex Mono";
    double size = 12, baseline = 12, lineHeight = 16;
};
struct GridPaintServices {
    // The layout is portable; only font metrics/color-space conversion are
    // backend services. The Mac reference supplies its exact resolved faces.
    std::function<GridFont(double, FontWeight, bool)> font;
    std::function<Color(uint32_t, double)> color;
};
class GridPainter {
public:
    GridPainter(const app::TrackerViewState& model, const app::GridSelection& selection,
        GridPaintServices services);
    DisplayList grid(Rect bounds, Rect dirty);
    DisplayList gutter(Rect bounds, double scrollY, double zoom, bool wholeRows);
    DisplayList envelope(Rect bounds);
    DisplayList envelopePlayback(Rect bounds);

private:
    const app::TrackerViewState* trackerState;
    const app::GridSelection& selection_;
    GridPaintServices services_;
    DisplayList list_;
    Rect bounds_;
    Color literal(uint32_t rgb, double alpha = 1.0) const;
    Color trackerColor(uint32_t rgb, double alpha = 1.0) const;
    void fillRect(Rect bounds, Color color);
    void strokeRect(Rect bounds, Color color, double width = 1);
    void fillTrackerEllipse(Rect bounds, Color color);
    void strokeTrackerPolyline(const std::vector<Point>& points, Color color, double width);
    void drawText(const std::string& text, Rect bounds, Color color, double size,
        FontWeight weight = FontWeight::Regular, Alignment align = Alignment::Left);
    void drawCenteredText(const std::string& text, Rect bounds, Color color, double size,
        FontWeight weight = FontWeight::Regular, Alignment align = Alignment::Center);
    void paintGrid(Rect dirtyRect);
    void paintEnvelope();
    void paintEnvelopePlayback();
};
}
