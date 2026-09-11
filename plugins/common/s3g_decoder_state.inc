bool stateSave(const clap_plugin_t *plugin, const clap_ostream_t *stream) {
  const auto &p = *self(plugin);
  auto candidate = std::make_unique<Plugin>();
  candidate->plugin.plugin_data = candidate.get();
  candidate->params = snapshotDecoderParams(p);
  // Recompute the original decoder's hidden selected-speaker/layout fields
  // on this isolated main-thread candidate before encoding legacy state.
  refreshPortableDecoder(*candidate);
#if S3G_DECODER_CAMERA_STATE
  candidate->guiViewMode = p.guiViewMode;
  candidate->guiViewAzDeg = p.guiViewAzDeg;
  candidate->guiViewElDeg = p.guiViewElDeg;
  candidate->guiViewZoom = p.guiViewZoom;
#endif
  return rawDecoderStateSave(&candidate->plugin, stream);
}
bool stateLoad(const clap_plugin_t *plugin, const clap_istream_t *stream) {
  auto &p = *self(plugin);
  auto candidate = std::make_unique<Plugin>();
  candidate->plugin.plugin_data = candidate.get();
  if (!rawDecoderStateLoad(&candidate->plugin, stream))
    return false;
  for (clap_id id = 1; decoderIsParam(id); ++id)
    if (!std::isfinite(readDecoderScalar(candidate->params, id)))
      return false;
#if S3G_DECODER_CAMERA_STATE
  if (!std::isfinite(candidate->guiViewAzDeg) ||
      !std::isfinite(candidate->guiViewElDeg) ||
      !std::isfinite(candidate->guiViewZoom))
    return false;
  p.guiViewMode = candidate->guiViewMode;
  p.guiViewAzDeg = candidate->guiViewAzDeg;
  p.guiViewElDeg = candidate->guiViewElDeg;
  p.guiViewZoom = candidate->guiViewZoom;
#endif
  for (clap_id id = 1; decoderIsParam(id); ++id)
    p.decoderValues[id].store(readDecoderScalar(candidate->params, id),
                              std::memory_order_release);
  p.decoderLayoutCount.store(decoderHiddenCount(candidate->params),
                             std::memory_order_release);
  publishDecoderStateBase(p, candidate->params);
  if (!p.decoderActive)
    syncDecoderParams(p);
  return true;
}
