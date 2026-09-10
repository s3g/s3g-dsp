void Editor::graph(CRect f) {
  const auto b = rect(f.left + 30., f.top + 44., f.getWidth() - 60., 330.);
  fill(b, gray(0x10));
  std::array<uint32_t, kChannelCount> physical{};
  uint32_t count = 0;
  for (uint32_t i = 0; i < kChannelCount; ++i)
    if (patchRow(i))
      physical[count++] = i;
  if (!count)
    for (uint32_t i = 0; i < kChannelCount; ++i)
      physical[count++] = i;
  heatmap(topologyStateForPlugin(p), count,
          rect(f.left + 30., f.top + 392., f.getWidth() - 60., 180.));
  std::array<CPoint, kChannelCount> nodes{};
  std::array<double, kChannelCount> diffusion{};
  double cx = 0., cy = 0., cz = 0.;
  for (uint32_t i = 0; i < count; ++i) {
    const auto pt = topologyPointForLane(p, i, count);
    const double d =
        std::clamp(resolvedChannelDelayMs(p, i, count) / kDelayMaxMs, 0., 1.);
    const double fb =
        std::clamp(resolvedChannelFeedback(p, i, count) / .82, 0., 1.);
    diffusion[i] = resolvedChannelNetwork(p, i, count) / .68;
    const double r = .56 + d * .68 + p.topologySpread * .22;
    const double x = pt.x * r, y = pt.y * r + (fb - .5) * .58;
    const double z = pt.z * r + pt.lane * (.10 + p.topologyJitter * .44);
    cx += x;
    cy += y;
    cz += z;
    nodes[i] = project(x, y, z, b, .78);
  }
  cx /= count;
  cy /= count;
  cz /= count;
  bool drawn[kChannelCount][kChannelCount]{};
  for (uint32_t i = 0; i < count; ++i) {
    const auto neighbors = nearestTopologyNeighbors(p, i, count);
    for (uint32_t n = 0; n < std::clamp(p.topologyNeighborCount, 1u, 3u); ++n) {
      const int j = neighbors[n];
      if (j < 0 || uint32_t(j) >= count || j == int(i))
        continue;
      const uint32_t a = std::min(i, uint32_t(j)), d = std::max(i, uint32_t(j));
      if (drawn[a][d])
        continue;
      drawn[a][d] = true;
      const double amount =
          std::clamp((diffusion[a] + diffusion[d]) * .5, 0., 1.);
      line(nodes[a], nodes[d],
           gray(static_cast<unsigned>(0x6f + amount * 0x70)));
    }
  }
  for (uint32_t i = 0; i < count; ++i)
    for (uint32_t j = 0; j < count; ++j) {
      if (i == j)
        continue;
      const auto a = physical[i], d = physical[j];
      const double e = std::clamp<double>(
          p.routeEdgeEnergy[a][d].load(std::memory_order_relaxed), 0., 1.);
      if (e <= .0001)
        continue;
      line(nodes[i], nodes[j], gray(0xf2, .12 + e * .72), .75 + e * 2.5);
      double phase = p.routeEdgePhase[a][d].load(std::memory_order_relaxed);
      phase -= std::floor(phase);
      const CPoint marker{nodes[i].x + (nodes[j].x - nodes[i].x) * phase,
                          nodes[i].y + (nodes[j].y - nodes[i].y) * phase};
      ellipse(square(marker, 2. * (1.5 + e * 2.)), gray(0xff, .35 + e * .65));
    }
  const auto center = project(cx, cy, cz, b, .78);
  const double energy = std::clamp<double>(
      p.routeCentroidEnergy.load(std::memory_order_relaxed), 0., 1.);
  if (energy > .0001) {
    for (uint32_t i = 0; i < count; ++i)
      line(center, nodes[i], gray(0xe8, .05 + energy * .22), .5 + energy);
    const double radius = 9. + energy * 17.;
    ellipse(square(center, radius * 2.), gray(0xff, .05 + energy * .20));
    double phase = p.routeCentroidPhase.load(std::memory_order_relaxed);
    phase -= std::floor(phase);
    const double angle = phase * 6.283185307179586;
    ellipse(square({center.x + std::cos(angle) * radius,
                    center.y + std::sin(angle) * radius},
                   4.),
            style.accent);
  }
  line({center.x - 6., center.y}, {center.x + 6., center.y}, gray(0xd8));
  line({center.x, center.y - 6.}, {center.x, center.y + 6.}, gray(0xd8));
  for (uint32_t i = 0; i < count; ++i) {
    const double e = std::clamp<double>(
        p.routeNodeEnergy[physical[i]].load(std::memory_order_relaxed), 0., 1.);
    if (e > .0001)
      ellipse(square(nodes[i], 2. * (8. + e * 13.)), gray(0xf0, .06 + e * .24));
    fill(square(nodes[i], 10.), style.accent);
    smallText(format("%u", physical[i] + 1), nodes[i].x + 7., nodes[i].y - 8.,
              30.);
  }
  const double x = f.right - 188.;
  const bool routes =
      p.routeAmount > .000001 ||
      p.publishedRouteTailFrames.load(std::memory_order_relaxed) > 0;
  const auto name =
      routes ? format("ECHO ROUTES / %u NODES", count)
             : count == 8 ? "8PT NEIGHBOR MAP"
                          : count == 6 ? "6PT NEIGHBOR MAP"
                                       : count == 4 ? "4PT NEIGHBOR MAP"
                                                    : "SPHERE NEIGHBOR MAP";
  smallText(name, x, f.top + 10.);
  smallText(format("SHAPE %s", topologyShapeName(p.topologyShape)), x,
            f.top + 25.);
  smallText(p.topologyShape == 0
                ? "X=AZ Y=EL Z=DIST"
                : format("CENTROID + %uNN", p.topologyNeighborCount),
            x, f.top + 40.);
  readoutButton(f);
  if (showReadout) {
    smallText("DLY FDB CHR DIF SMR", x, f.top + 55.);
    for (uint32_t i = 0; i < count; ++i)
      smallText(format("L%u %4.0f %.2f %.2f %.2f %.2f", physical[i] + 1,
                       resolvedChannelDelayMs(p, i, count),
                       resolvedChannelFeedback(p, i, count),
                       resolvedChannelCharacter(p, i, count),
                       resolvedChannelNetwork(p, i, count),
                       resolvedChannelSmearAmount(p, i, count)),
                x, f.top + 70. + i * 15.);
  }
}
