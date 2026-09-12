inline VSTGUI::CColor parameterSurfaceCellColor(uint32_t index,
                                                double alpha = 0.36) {
  // Snapshot Surface uses a low-saturation HSV walk with an irrational-ish
  // hue increment so adjacent capture regions remain distinct without
  // turning the otherwise neutral editor into a rainbow.
  const double hue = std::fmod(static_cast<double>(index) * 0.137, 1.0);
  return drawing::calibratedHSV(hue, 0.58, 0.42, alpha);
}

inline std::vector<drawing::Point>
clipParameterSurfacePolygon(const std::vector<drawing::Point> &polygon,
                            drawing::Point site, drawing::Point other) {
  if (polygon.empty())
    return {};
  const double a = 2.0 * (other.x - site.x);
  const double b = 2.0 * (other.y - site.y);
  const double c =
      other.x * other.x + other.y * other.y - site.x * site.x - site.y * site.y;
  const auto inside = [=](drawing::Point point) {
    return a * point.x + b * point.y <= c + 0.0001;
  };
  const auto intersect = [=](drawing::Point first, drawing::Point second) {
    const double denominator =
        a * (second.x - first.x) + b * (second.y - first.y);
    if (std::abs(denominator) < 0.000001)
      return first;
    const double amount =
        std::clamp((c - a * first.x - b * first.y) / denominator, 0.0, 1.0);
    return drawing::makePoint(first.x + (second.x - first.x) * amount,
                              first.y + (second.y - first.y) * amount);
  };

  std::vector<drawing::Point> result;
  result.reserve(polygon.size() + 2u);
  drawing::Point previous = polygon.back();
  bool previousInside = inside(previous);
  for (const drawing::Point current : polygon) {
    const bool currentInside = inside(current);
    if (currentInside) {
      if (!previousInside)
        result.push_back(intersect(previous, current));
      result.push_back(current);
    } else if (previousInside) {
      result.push_back(intersect(previous, current));
    }
    previous = current;
    previousInside = currentInside;
  }
  return result;
}

inline drawing::Path
parameterSurfacePolygonPath(const std::vector<drawing::Point> &polygon) {
  if (polygon.empty())
    return {};
  drawing::Path path = drawing::Path::bezierPath();
  (path).moveToPoint(polygon.front());
  for (size_t index = 1u; index < polygon.size(); ++index) {
    (path).lineToPoint(polygon[index]);
  }
  (path).closePath();
  return path;
}

template <typename Surface>
inline void drawParameterSurfaceVoronoi(
    const Surface &surface, drawing::Rect plot, float cursorX, float cursorY,
    int selectedCell, drawing::Attrs labelAttrs,
    float targetX = std::numeric_limits<float>::quiet_NaN(),
    float targetY = std::numeric_limits<float>::quiet_NaN()) {
  const uint32_t count = std::min<uint32_t>(
      surface.cellCount, static_cast<uint32_t>(surface.cells.size()));
  const drawing::Point cursor = drawing::makePoint(
      plot.origin.x + std::clamp(cursorX, 0.0f, 1.0f) * plot.size.width,
      drawing::maxY(plot) - std::clamp(cursorY, 0.0f, 1.0f) * plot.size.height);
  const bool hasTarget = std::isfinite(targetX) && std::isfinite(targetY);
  const drawing::Point target =
      hasTarget ? drawing::makePoint(
                      plot.origin.x +
                          std::clamp(targetX, 0.0f, 1.0f) * plot.size.width,
                      drawing::maxY(plot) -
                          std::clamp(targetY, 0.0f, 1.0f) * plot.size.height)
                : cursor;
  std::vector<drawing::Point> sites;
  sites.reserve(count);
  for (uint32_t index = 0u; index < count; ++index) {
    const auto &cell = surface.cells[index];
    sites.push_back(
        drawing::makePoint(plot.origin.x + cell.x * plot.size.width,
                           drawing::maxY(plot) - cell.y * plot.size.height));
  }

  drawing::GraphicsScope graphics(*drawing::current->context);
  drawing::clipRect(drawing::inset(plot, 1.0, 1.0));
  drawing::setFill(drawing::calibratedRGB(0.035, 0.038, 0.041, 1.0));
  drawing::rectFill(plot);

  const std::vector<drawing::Point> bounds{
      drawing::makePoint(drawing::minX(plot), drawing::minY(plot)),
      drawing::makePoint(drawing::maxX(plot), drawing::minY(plot)),
      drawing::makePoint(drawing::maxX(plot), drawing::maxY(plot)),
      drawing::makePoint(drawing::minX(plot), drawing::maxY(plot)),
  };
  for (uint32_t index = 0u; index < count; ++index) {
    std::vector<drawing::Point> polygon = bounds;
    for (uint32_t other = 0u; other < count && !polygon.empty(); ++other) {
      if (other == index)
        continue;
      polygon =
          clipParameterSurfacePolygon(polygon, sites[index], sites[other]);
    }
    drawing::Path path = parameterSurfacePolygonPath(polygon);
    if (!path)
      continue;
    drawing::setFill(parameterSurfaceCellColor(index, 0.36));
    (path).fill();
    drawing::setStroke(drawing::calibratedRGB(0.48, 0.51, 0.53, 0.78));
    (path).setLineWidth(1.5);
    (path).stroke();
  }

  drawing::setStroke(drawing::calibratedRGB(0.52, 0.56, 0.58, 0.16));
  for (uint32_t division = 1u; division < 8u; ++division) {
    const double x =
        plot.origin.x + plot.size.width * static_cast<double>(division) / 8.0;
    const double y =
        plot.origin.y + plot.size.height * static_cast<double>(division) / 8.0;
    drawing::Path::strokeLineFromPoint_toPoint(
        drawing::makePoint(x, drawing::minY(plot)),
        drawing::makePoint(x, drawing::maxY(plot)));
    drawing::Path::strokeLineFromPoint_toPoint(
        drawing::makePoint(drawing::minX(plot), y),
        drawing::makePoint(drawing::maxX(plot), y));
  }

  for (uint32_t index = 0u; index < count; ++index) {
    const drawing::Point site = sites[index];
    const double distance = std::hypot(site.x - cursor.x, site.y - cursor.y);
    const double radius =
        std::clamp(42.0 / std::max(0.25, distance / 42.0 + 0.25), 8.0, 42.0);
    drawing::setFill(parameterSurfaceCellColor(index, 0.16));
    (drawing::Path::bezierPathWithOvalInRect(
         drawing::makeRect(site.x - radius * 1.9, site.y - radius * 1.9,
                           radius * 3.8, radius * 3.8)))
        .fill();
    drawing::setFill(parameterSurfaceCellColor(index, 0.30));
    (drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
         site.x - radius, site.y - radius, radius * 2.0, radius * 2.0)))
        .fill();

    const bool selected = static_cast<int>(index) == selectedCell;
    const double nodeRadius = selected ? 6.0 : 4.5;
    drawing::setFill(parameterSurfaceCellColor(index, 0.95));
    drawing::Path node = drawing::Path::bezierPathWithOvalInRect(
        drawing::makeRect(site.x - nodeRadius, site.y - nodeRadius,
                          nodeRadius * 2.0, nodeRadius * 2.0));
    (node).fill();
    drawing::setStroke(drawing::calibratedRGB((selected ? 0.86 : 0.35),
                                              (selected ? 0.90 : 0.38),
                                              (selected ? 0.92 : 0.40), 1.0));
    (node).setLineWidth(selected ? 2.0 : 1.0);
    (node).stroke();

    drawing::String label =
        surface.cells[index].name[0] != '\0'
            ? drawing::stringFromUtf8(surface.cells[index].name)
            : drawing::stringFormat("CELL %u", index + 1u);
    if ((label).size() > 18u) {
      label = ((label).substr(0, 16u) + "...");
    }
    drawing::drawAtPoint_withAttributes(
        label, drawing::makePoint(site.x + nodeRadius + 5.0, site.y - 7.0),
        labelAttrs);
  }

  drawing::setStroke(drawing::calibratedRGB(0.78, 0.84, 0.86, 1.0));
  drawing::Path cross = drawing::Path::bezierPath();
  (cross).moveToPoint(drawing::makePoint(cursor.x - 13.0, cursor.y));
  (cross).lineToPoint(drawing::makePoint(cursor.x + 13.0, cursor.y));
  (cross).moveToPoint(drawing::makePoint(cursor.x, cursor.y - 13.0));
  (cross).lineToPoint(drawing::makePoint(cursor.x, cursor.y + 13.0));
  (cross).setLineWidth(2.0);
  (cross).stroke();
  drawing::Path cursorBox = drawing::Path::bezierPathWithRect(
      drawing::makeRect(cursor.x - 5.0, cursor.y - 5.0, 10.0, 10.0));
  (cursorBox).setLineWidth(1.5);
  (cursorBox).stroke();
  if (hasTarget && std::hypot(target.x - cursor.x, target.y - cursor.y) > 2.0) {
    drawing::setStroke(drawing::calibratedRGB(0.62, 0.66, 0.68, 0.72));
    drawing::Path targetMarker = drawing::Path::bezierPath();
    (targetMarker).moveToPoint(drawing::makePoint(target.x, target.y - 6.0));
    (targetMarker).lineToPoint(drawing::makePoint(target.x + 6.0, target.y));
    (targetMarker).lineToPoint(drawing::makePoint(target.x, target.y + 6.0));
    (targetMarker).lineToPoint(drawing::makePoint(target.x - 6.0, target.y));
    (targetMarker).closePath();
    (targetMarker).setLineWidth(1.2);
    (targetMarker).stroke();
  }
  graphics.restore();

  drawing::setStroke(drawing::calibratedRGB(0.35, 0.38, 0.40, 1.0));
  drawing::Path border = drawing::Path::bezierPathWithRect(plot);
  (border).setLineWidth(1.4);
  (border).stroke();
}
