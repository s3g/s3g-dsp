#pragma once
#include "s3g_environment_encoder_drawing.h"
#include "vstgui/lib/coffscreencontext.h"
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <random>
#include <thread>

namespace s3g::portable_gui::map_drawing {
using namespace environment_drawing;
inline double uptime() {
  return std::chrono::duration<double>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline CColor ambisonicOrientationGuideColor(double alpha) {
  return input_encoder_drawing::orientationGuideColor(alpha);
}
inline uint32_t multiColumnMenuRows(uint32_t count, uint32_t columns) {
  return columns ? (count + columns - 1u) / columns : 0u;
}
inline int multiColumnDropdownHitIndex(Point point, Rect bounds, double height,
    uint32_t count, uint32_t columns) {
  if (!contains(point, bounds) || !count || !columns || height <= 0.) return -1;
  const auto rows = multiColumnMenuRows(count, columns);
  const auto column = uint32_t((point.x - bounds.origin.x) / (bounds.size.width / columns));
  const auto row = uint32_t((point.y - bounds.origin.y) / height);
  const auto index = column * rows + row;
  return row < rows && index < count ? int(index) : -1;
}
inline void drawMultiColumnDropdownMenu(Rect bounds, double height,
    const String* items, uint32_t count, uint32_t columns, int selected,
    int hover, Attrs attrs, const Style& style) {
  const auto rows = multiColumnMenuRows(count, columns);
  for (uint32_t column = 0; rows && column < columns; ++column) {
    const auto first = column * rows;
    if (first >= count) break;
    const auto size = std::min(rows, count - first);
    const auto local = [&](int index) {
      return index >= int(first) && index < int(first + size) ? index - int(first) : -1;
    };
    decoder_drawing::drawDropdownMenu(makeRect(
        bounds.origin.x + column * bounds.size.width / columns, bounds.origin.y,
        bounds.size.width / columns, size * height), height, items + first, size,
        local(selected), local(hover), attrs, style);
  }
}

// Geometry is calculated on a worker without calling the platform drawing API.
// The GUI thread rasterizes these original, ordered paths once into a cache.
struct RenderPath {
  std::vector<VSTGUI::CPoint> points;
  CColor color{};
  double width = 1.;
  bool closed = false, fill = false;
};
struct RenderList {
  Size size{};
  std::vector<RenderPath> paths;
};
struct RecordingContext {
  RenderList result;
  RenderPath path;
  CColor fill{}, stroke{};
  double width = 1.;
  explicit RecordingContext(Size size) { result.size = size; }
  void begin() { path = {}; }
  void move(double x, double y) { path.points.push_back({x, y}); }
  void line(double x, double y) { path.points.push_back({x, y}); }
  void close() { path.closed = true; }
  void finish(bool filled) {
    path.fill = filled;
    path.color = filled ? fill : stroke;
    path.width = width;
    result.paths.push_back(std::move(path));
    path = {};
  }
};
inline VSTGUI::SharedPointer<VSTGUI::CBitmap> rasterize(const RenderList& list) {
  return VSTGUI::renderBitmapOffscreen(
      {list.size.width, list.size.height}, 2., [&](VSTGUI::CDrawContext& c) {
        c.setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);
        for (const auto& recorded : list.paths) {
          if (recorded.points.empty()) continue;
          auto path = VSTGUI::owned(c.createGraphicsPath());
          if (!path) continue;
          path->beginSubpath(recorded.points.front());
          for (size_t i = 1; i < recorded.points.size(); ++i)
            path->addLine(recorded.points[i]);
          if (recorded.closed) path->closeSubpath();
          c.setFillColor(recorded.color);
          c.setFrameColor(recorded.color);
          c.setLineWidth(recorded.width);
          c.drawGraphicsPath(path, recorded.fill
              ? VSTGUI::CDrawContext::kPathFilled
              : VSTGUI::CDrawContext::kPathStroked);
        }
      });
}

// One pending request and one result, with cancellation supplied by the owner.
// No detached callbacks can outlive the editor, and no audio thread participates.
template<class Request, class Result> class LatestWorker {
public:
  using Work = std::function<std::shared_ptr<Result>(const Request&)>;
  explicit LatestWorker(Work work) : work_(std::move(work)), thread_([this] { run(); }) {}
  ~LatestWorker() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
      pending_.reset();
    }
    condition_.notify_one();
    thread_.join();
  }
  void submit(Request request) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pending_ = std::move(request);
      result_.reset();
    }
    condition_.notify_one();
  }
  std::shared_ptr<Result> take() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(result_, {});
  }
private:
  void run() {
    for (;;) {
      Request request;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [&] { return stopping_ || pending_.has_value(); });
        if (stopping_) return;
        request = std::move(*pending_);
        pending_.reset();
      }
      auto result = work_(request);
      std::lock_guard<std::mutex> lock(mutex_);
      if (!stopping_ && !pending_) result_ = std::move(result);
    }
  }
  Work work_;
  std::mutex mutex_;
  std::condition_variable condition_;
  bool stopping_ = false;
  std::optional<Request> pending_;
  std::shared_ptr<Result> result_;
  std::thread thread_;
};
} // namespace s3g::portable_gui::map_drawing
