void Editor::controls() {
  panel(rect(primaryX, 42., panelWidth, 80.), "OUTPUT");
  row(outputId, "OUT", primaryX, 70., format("%+.1f dB", value(outputId)));
  row(kMixParamId, "MIX", primaryX, 96.);
  struct Control {
    clap_id id;
    const char *label;
  };
#if defined(S3G_TOPOLOGY_DELAY_PORT)
  constexpr Control engine[]{
      {kDelayMsParamId, "TIME"},   {kFeedbackParamId, "FDBK"},
      {kToneParamId, "TONE"},      {kPitchParamId, "PITCH"},
      {kCharacterParamId, "CHAR"}, {kTapParamId, "SMR"}};
  constexpr const char *engineName = "ENGINE";
  constexpr Control extra[]{{kRouteAmountParamId, "ROUTE"},
                            {kRouteTurnParamId, "TURN"},
                            {kRouteBranchParamId, "BRCH"},
                            {kRouteLossParamId, "LOSS"}};
  constexpr const char *extraName = "ECHO ROUTES";
#elif defined(S3G_TOPOLOGY_WAVE_GEOMETRY_PORT)
  constexpr Control engine[]{
      {kFoldParamId, "FOLD"}, {kDriveParamId, "DRIV"},   {kHoldParamId, "HOLD"},
      {kClipParamId, "CLIP"}, {kRectifyParamId, "RECT"}, {kEdgeParamId, "EDGE"},
      {kZeroParamId, "ZERO"}, {kPolarParamId, "POL"},    {kBitsParamId, "BITS"},
      {kStepParamId, "STEP"}, {kTransParamId, "TRNS"},   {kTapeParamId, "TAPE"},
      {kSpeedParamId, "SPED"}};
  constexpr const char *engineName = "WAVE ENGINE";
  constexpr Control extra[]{{kMeshCouplingParamId, "COUP"},
                            {kMeshTensionParamId, "TENS"},
                            {kMeshDecayParamId, "DECY"},
                            {kMeshDampingParamId, "DAMP"}};
  constexpr const char *extraName = "WAVE MESH";
#else
  constexpr Control engine[]{
      {kSprayBinsParamId, "BINS"}, {kDriftParamId, "DRFT"},
      {kHoldParamId, "HOLD"},      {kFreezeParamId, "FRZ"},
      {kFeedbackParamId, "FDBK"},  {kSmearParamId, "SMR"},
      {kHolesParamId, "HOLE"},     {kPhaseBlurParamId, "PHAS"},
      {kDamageParamId, "DMG"},     {kRepeatParamId, "RPT"},
      {kTiltParamId, "TILT"},      {kLoFreqParamId, "LO"},
      {kHiFreqParamId, "HI"},      {kTransientProtectParamId, "TRANS"}};
  constexpr const char *engineName = "SPECTRAL ENGINE";
  constexpr Control extra[]{{kPropagationVelocityParamId, "VEL"},
                            {kPropagationDispersionParamId, "DISP"},
                            {kPropagationDampingParamId, "DAMP"}};
  constexpr const char *extraName = "PROPAGATION";
#endif
  constexpr auto count = sizeof(engine) / sizeof(engine[0]);
  constexpr double engineHeight = layout::toolboxHeightForRows(count);
  panel(rect(primaryX, 134., panelWidth, engineHeight), engineName);
  for (uint32_t i = 0; i < count; ++i) {
    const auto id = engine[i].id;
    const double v = value(id), y = 162. + i * 26.;
    std::string display;
#if defined(S3G_TOPOLOGY_DELAY_PORT)
    if (id == kDelayMsParamId)
      display = format("%4.0f", v);
    if (id == kPitchParamId)
      display = format("%+4.1f", v);
#elif defined(S3G_TOPOLOGY_WAVE_GEOMETRY_PORT)
    if (id == kTransParamId)
      display = format("%+.2f", v);
#else
    if (id == kSprayBinsParamId)
      display = format("%.0f", v);
    if (id == kTiltParamId)
      display = format("%+.2f", v);
    if (id == kLoFreqParamId || id == kHiFreqParamId) {
      display = v >= 1000. ? format("%.1fk", v * .001) : format("%.0f Hz", v);
      row(id, engine[i].label, primaryX, y, display,
          id == kLoFreqParamId ? 0. : 20., 24000.);
      continue;
    }
#endif
    row(id, engine[i].label, primaryX, y, display);
  }
#if defined(S3G_TOPOLOGY_SPECTRAL_TOPOLOGY_PORT)
  const auto cap = rect(primaryX + panelWidth - 86., 136., 38., 17.);
  const auto clr = rect(primaryX + panelWidth - 44., 136., 38., 17.);
  headerButton(cap, "CAP", false, [this] {
    p.capturePending.store(true, std::memory_order_release);
    requestService();
  });
  headerButton(clr, "CLR", false, [this] {
    p.clearCapturePending.store(true, std::memory_order_release);
    requestService();
  });
  if (p.processor.hasCapture())
    fill(rect(cap.left + 3., cap.top + 3., 3., 3.), style.accent);
#endif
  constexpr auto extraCount = sizeof(extra) / sizeof(extra[0]);
  panel(rect(secondaryX, 498., panelWidth,
             layout::toolboxHeightForRows(extraCount)),
        extraName);
  for (uint32_t i = 0; i < extraCount; ++i) {
    const auto id = extra[i].id;
    std::string display;
#if defined(S3G_TOPOLOGY_DELAY_PORT)
    if (id == kRouteTurnParamId)
      display = format("%+3.0f%%", value(id) * 100.);
#elif defined(S3G_TOPOLOGY_SPECTRAL_TOPOLOGY_PORT)
    if (id == kPropagationDispersionParamId)
      display = format("%+.2f", value(id));
#endif
    row(id, extra[i].label, secondaryX, 526. + i * 26., display);
  }
  matrix(126. + engineHeight + 12.);
}
