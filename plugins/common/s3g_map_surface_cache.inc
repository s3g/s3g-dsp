void requestSurfaceImageForParams(const s3g::AmbiTerrainNavigatorParams& params) {
  if (!_surfaceWorker) return;
  TerrainSurfaceRenderRequest request{};
  request.params = params;
  request.size = fieldRect().size;
  request.viewAzimuthDeg = _viewAzDeg;
  request.viewElevationDeg = _viewElDeg;
  request.viewZoom = _viewZoom;
  request.generation = _surfaceRenderCoordinator->generation.fetch_add(1u) + 1u;
  _surfaceWorker->submit(request);
}
void acceptSurfaceImage() {
  if (auto result = _surfaceWorker->take()) {
    auto bitmap = drawing::rasterize(*result);
    if (!bitmap) return;
    _previousSurfaceImage = _surfaceImageCache;
    _surfaceImageCache = std::move(bitmap);
    _surfaceImageCacheValid = true;
    _surfaceCrossfadeStartTime = drawing::uptime();
    invalid();
  }
}
void drawTerrainShellInRect(drawing::Rect rect) {
  if (!_surfaceImageCache || !_surfaceImageCacheValid) return;
  const double transition = _previousSurfaceImage
      ? std::clamp((drawing::uptime() - _surfaceCrossfadeStartTime) / .12, 0., 1.)
      : 1.;
  if (_previousSurfaceImage)
    _previousSurfaceImage->draw(context, rect, {0, 0}, float(1. - transition));
  _surfaceImageCache->draw(context, rect, {0, 0}, float(transition));
  if (transition >= 1.) _previousSurfaceImage = nullptr;
}
