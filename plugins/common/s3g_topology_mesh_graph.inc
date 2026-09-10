void Editor::graph(CRect f) {
#if defined(S3G_TOPOLOGY_WAVE_GEOMETRY_PORT)
  const auto &settings = p.settings;
#else
  const auto settings = publishedSettings(p);
#endif
  const auto state = settings.topology;
  const auto controls = s3g::topologyControlsFromState(state);
  const auto b = rect(f.left + 30., f.top + 28., f.getWidth() - 60., 348.);
  fill(b, gray(0x10));
  heatmap(state, kChannelCount,
          rect(f.left + 30., f.top + 394., f.getWidth() - 60., 180.));
  std::array<CPoint, kChannelCount> nodes{};
  for (uint32_t i = 0; i < kChannelCount; ++i)
    nodes[i] =
        project(s3g::topologyPointForLane(i, kChannelCount, controls), b);
#if defined(S3G_TOPOLOGY_WAVE_GEOMETRY_PORT)
  std::array<float, kChannelCount> nodeEnergy{};
  if (settings.mesh.coupling > .0001f && state.centroidAmount > .0001) {
    const auto center = project(s3g::TopologyPoint{}, b);
    float centerEnergy = 0.f;
    for (uint32_t i = 0; i < kChannelCount; ++i) {
      float e = 0.f;
      for (uint32_t j = 0; j < kChannelCount; ++j)
        if (i != j)
          e = std::max(
              {e, p.meshEdgeEnergy[i][j].load(std::memory_order_relaxed),
               p.meshEdgeEnergy[j][i].load(std::memory_order_relaxed)});
      e = std::clamp(e, 0.f, 1.f);
      nodeEnergy[i] = std::max(nodeEnergy[i], e);
      centerEnergy = std::max(centerEnergy, e);
      line(center, nodes[i],
           gray(0x78, .10 + float(state.centroidAmount) *
                                (.12 + std::sqrt(e) * .34)));
    }
    fill(square(center, 5. + std::sqrt(centerEnergy) * 8.),
         gray(0xc0, .28 + std::sqrt(centerEnergy) * .52));
  }
#endif
  bool drawn[kChannelCount][kChannelCount]{};
  for (uint32_t i = 0; i < kChannelCount; ++i) {
    const auto neighbors =
        s3g::nearestTopologyNeighbors(state, i, kChannelCount);
    for (uint32_t n = 0; n < std::min(state.neighborCount, 3u); ++n) {
      const int neighbor = neighbors[n];
      if (neighbor < 0 || uint32_t(neighbor) >= kChannelCount ||
          neighbor == int(i))
        continue;
      const uint32_t j = uint32_t(neighbor), a = std::min(i, j),
                     d = std::max(i, j);
      if (drawn[a][d])
        continue;
      drawn[a][d] = true;
#if defined(S3G_TOPOLOGY_WAVE_GEOMETRY_PORT)
      const float ea = std::clamp(
          p.meshEdgeEnergy[a][d].load(std::memory_order_relaxed), 0.f, 1.f);
      const float eb = std::clamp(
          p.meshEdgeEnergy[d][a].load(std::memory_order_relaxed), 0.f, 1.f);
      const float level = std::sqrt(std::max(ea, eb));
      nodeEnergy[a] = std::max(nodeEnergy[a], std::max(ea, eb));
      nodeEnergy[d] = std::max(nodeEnergy[d], std::max(ea, eb));
      const auto c = settings.mesh.coupling <= .0001f
                         ? gray(0xb8, .60)
                         : gray(0x58 + static_cast<unsigned>(level * 0x60),
                                .42 + level * .48);
      line(nodes[a], nodes[d], c);
      auto marker = [&](uint32_t source, uint32_t dest, float energy) {
        if (settings.mesh.coupling <= .0001f || energy <= .0004f)
          return;
        const float phase = std::clamp(
            p.meshEdgePhase[source][dest].load(std::memory_order_relaxed), 0.f,
            1.f);
        const float level = std::sqrt(energy);
        const CPoint pt{
            nodes[source].x + (nodes[dest].x - nodes[source].x) * phase,
            nodes[source].y + (nodes[dest].y - nodes[source].y) * phase};
        fill(square(pt, 2. + level * 4.), gray(0xd0, .30 + level * .70));
      };
      marker(a, d, ea);
      marker(d, a, eb);
#else
      const float activity =
          std::clamp(std::max(p.processor.edgeActivity(i, j),
                              p.processor.edgeActivity(j, i)),
                     0.f, 1.f);
      const double energy = std::sqrt(activity);
      line(nodes[i], nodes[j], gray(0xb8, .20 + energy * .78),
           .65 + energy * 2.35);
      auto marker = [&](uint32_t source, uint32_t dest) {
        const float e =
            std::clamp(p.processor.edgeActivity(source, dest), 0.f, 1.f);
        if (e <= .0001f)
          return;
        const float pulse = p.processor.edgePulsePosition(source, dest);
        if (!std::isfinite(pulse))
          return;
        const double phase = std::clamp(pulse, 0.f, 1.f), level = std::sqrt(e);
        const CPoint pt{
            nodes[source].x + (nodes[dest].x - nodes[source].x) * phase,
            nodes[source].y + (nodes[dest].y - nodes[source].y) * phase};
        fill(square(pt, 3. + level * 4.5), gray(0xf2, .32 + level * .68));
      };
      marker(i, j);
      marker(j, i);
#endif
    }
  }
  for (uint32_t i = 0; i < kChannelCount; ++i) {
    const auto pt = s3g::topologyPointForLane(i, kChannelCount, controls);
    const double size = 8. + std::clamp(pt.radius, 0., 1.5) * 2.;
    bool injected = false;
    for (uint32_t input = 0; input < kChannelCount; ++input)
      injected |= (patchRow(input) & (uint64_t{1} << i)) != 0;
#if defined(S3G_TOPOLOGY_WAVE_GEOMETRY_PORT)
    const float activity = std::sqrt(std::clamp(nodeEnergy[i], 0.f, 1.f));
    if (settings.mesh.coupling > .0001f && activity > .015f) {
      // NSFrameRect uses the current fill color, not setStroke's color.
      stroke(CRect(square(nodes[i], size + 5. + activity * 15.)).inset(.5, .5),
             context->getFillColor());
    }
    fill(square(nodes[i], size), injected ? style.text : gray(0x62, .82));
#else
    fill(square(nodes[i], size), injected ? style.accent : style.text);
#endif
    smallText(format("L%u", i + 1), nodes[i].x + 7., nodes[i].y - 6., 40.);
  }
  const double x = f.right - 188.;
#if defined(S3G_TOPOLOGY_WAVE_GEOMETRY_PORT)
  smallText(settings.mesh.coupling > .0001f ? "8PT WAVE MESH"
                                            : "8PT NEIGHBOR MAP",
            x, f.top + 10.);
  smallText(state.shape == 0 ? "XYZ = FOLD STEP POL" : "XYZ = FOLD ZERO BITS",
            x, f.top + 40.);
#else
  smallText(format("%uPT NEIGHBOR MAP", kChannelCount), x, f.top + 10.);
  smallText(state.shape == 0 ? "XYZ = TILT HOLD PHAS" : "XYZ = BIN DROP RPT", x,
            f.top + 40.);
  smallText("BRIGHT NODE = DIRECT IN", f.left + 42., f.top + 10., 240.);
#endif
  smallText(format("SHAPE = %s", s3g::topologyShapeName(state.shape)), x,
            f.top + 25.);
  readoutButton(f);
  if (showReadout) {
#if defined(S3G_TOPOLOGY_WAVE_GEOMETRY_PORT)
    smallText("FLD STP BIT POL", x, f.top + 55.);
    for (uint32_t i = 0; i < kChannelCount; ++i) {
      const auto v = s3g::waveGeometryLaneParams(settings, i, kChannelCount);
      smallText(format("L%u %3.0f %.2f %.2f %.2f", i + 1, v.fold, v.step,
                       v.bits, v.polar),
                x, f.top + 70. + i * 15.);
    }
#else
    smallText("BIN FRZ DMG RPT", x, f.top + 55.);
    for (uint32_t i = 0; i < kChannelCount; ++i) {
      const auto v =
          s3g::spectralTopologyLaneParams(settings, i, kChannelCount);
      smallText(format("L%u %3.0f %.2f %.2f %.2f", i + 1, v.sprayBins, v.freeze,
                       v.damage, v.repeat),
                x, f.top + 70. + i * 15.);
    }
#endif
  }
}
