#include "editor_geometry_support.h"
#include "s3g_tracker_authoring_page.h"
namespace s3g::tracker::editor {
using namespace VSTGUI;
using geometry_support::format;
namespace {
CRect rect(s3g::gui_layout::Rect r) {
  return toolRect(r.x, r.y, r.width, r.height);
}
CRect control(CRect p, int row, bool slider = false) {
  return toolRect(p.left + 108, p.top + 36 + row * 26 - (slider ? 8 : 1),
                  std::max(80., p.getWidth() - 124), slider ? 24 : 15);
}
} // namespace
void AuthoringPageView::drawReshape() {
  auto layout =
      s3g::gui_layout::trackerReshapeFamilyLayout({getWidth(), getHeight()});
  auto profile = rect(layout.profilePanel),
       mutation = rect(layout.mutation.frame),
       target = rect(layout.targetAnalyze.frame),
       timing = rect(layout.timing.frame),
       dynamics = rect(layout.dynamics.frame),
       commit = rect(layout.previewApply.frame);
  panel(profile, "PATTERN PROFILE  /  ORIGINAL → VARIANT");
  panel(mutation, "RHYTHM MUTATION / STATISTICAL");
  panel(target, "TARGET / ANALYZE");
  panel(timing, "TIMING / MICROTIME");
  panel(dynamics, "DYNAMICS / VELOCITY");
  panel(commit, "PREVIEW / APPLY");
  canvas_ = toolRect(profile.left + 1, profile.top + 21, profile.getWidth() - 2,
                     profile.getHeight() - 22);
  drawProfile(canvas_);
  rowLabel(target, 0, "PATTERN");
  rowLabel(target, 1, "LANES");
  rowLabel(target, 2, "CYCLE");
  std::vector<std::string> patterns, ids;
  int current = 0;
  for (auto &p : state_.patternBank.entries) {
    if (p.id == state_.patternBank.activePatternId)
      current = int(patterns.size());
    patterns.push_back(p.id +
                       (p.pattern.name.empty() ? "" : " · " + p.pattern.name));
    ids.push_back(p.id);
  }
  choice(control(target, 0), "pattern", patterns, current, [this, ids](int i) {
    if (ids[i] == state_.patternBank.activePatternId)
      return;
    reshape.clearPreview();
    if (callbacks_.selectPattern)
      callbacks_.selectPattern(ids[i]);
  });
  auto &s = reshape.settings;
  std::vector<std::string> lanes{"ALL LANES"};
  for (std::size_t i = 0;
       i < std::min<std::size_t>(32, state_.session.pattern.tracks.size());
       ++i) {
    auto &t = state_.session.pattern.tracks[i];
    lanes.push_back(format(
        "%s L%02lu  %s", (s.laneMask & (1u << i)) ? "●" : "○",
        static_cast<unsigned long>(i + 1),
        t.name.empty() ? format("LANE %02lu", static_cast<unsigned long>(i + 1))
                       : t.name));
  }
  choice(
      control(target, 1), "lanes", lanes, 0,
      [this](int i) {
        reshape.toggleLane(i - 1);
        invalid();
      },
      true, reshape.laneTitle());
  constexpr std::size_t cycles[] = {0, 4, 8, 16, 32, 64};
  int ci = 0;
  for (int i = 0; i < 6; ++i)
    if (cycles[i] == s.cycleRows)
      ci = i;
  choice(control(target, 2), "cycle", {"AUTO", "4", "8", "16", "32", "64"}, ci,
         [this](int i) {
           constexpr std::size_t n[] = {0, 4, 8, 16, 32, 64};
           reshape.settings.cycleRows = n[i];
           reshape.refreshResult();
           invalid();
         });
  multiline(reshape.analysisText(), toolRect(target.left + 16, target.top + 112,
                                             target.getWidth() - 32, 64));
  const char *tl[] = {"POCKET", "TIGHTEN", "DEPTH", "WRITE MT", "MT OUTLIERS"};
  const char *dl[] = {"RANGE", "ACCENTS", "LANE BAL", "WRITE VEL",
                      "VEL OUTLIERS"};
  for (int i = 0; i < 5; ++i) {
    rowLabel(timing, i, tl[i]);
    rowLabel(dynamics, i, dl[i]);
  }
  slider(control(timing, 0, true), "pocket", s.pocket, 0, 100);
  slider(control(timing, 1, true), "tighten", s.tighten, 0, 100);
  slider(control(timing, 2, true), "depth", s.timingDepth, -100, 100);
  slider(control(dynamics, 0, true), "range", s.velocityRange, 0, 200);
  slider(control(dynamics, 1, true), "accent", s.accentDepth, 0, 200);
  slider(control(dynamics, 2, true), "balance", s.laneBalance, 0, 100);
  choice(control(timing, 3), "timing_write", {"EXISTING ONLY", "ADD TO ONSETS"},
         s.microTimingWrite == PatternReshapeWriteMode::FillMissing ? 1 : 0,
         [this](int i) {
           reshape.settings.microTimingWrite =
               i ? PatternReshapeWriteMode::FillMissing
                 : PatternReshapeWriteMode::ExistingOnly;
           reshape.refreshResult();
           invalid();
         });
  choice(control(dynamics, 3), "velocity_write",
         {"EXISTING ONLY", "FILL DEFAULTS"},
         s.velocityWrite == PatternReshapeWriteMode::FillMissing ? 1 : 0,
         [this](int i) {
           reshape.settings.velocityWrite =
               i ? PatternReshapeWriteMode::FillMissing
                 : PatternReshapeWriteMode::ExistingOnly;
           reshape.refreshResult();
           invalid();
         });
  auto outlier = [](float v) { return v <= 0 ? 0 : v >= 3 ? 1 : 2; };
  choice(control(timing, 4), "timing_outlier", {"OFF", "SOFT", "STRONG"},
         outlier(s.timingOutlierThreshold), [this](int i) {
           reshape.settings.timingOutlierThreshold =
               i == 1 ? 3.f : i == 2 ? 2.f : 0;
           reshape.refreshResult();
           invalid();
         });
  choice(control(dynamics, 4), "velocity_outlier", {"OFF", "SOFT", "STRONG"},
         outlier(s.velocityOutlierThreshold), [this](int i) {
           reshape.settings.velocityOutlierThreshold =
               i == 1 ? 3.f : i == 2 ? 2.f : 0;
           reshape.refreshResult();
           invalid();
         });
  double half =
      (mutation.getWidth() - s3g::gui_layout::kStandardMetrics.panelGap) * .5;
  auto left = toolRect(mutation.left, mutation.top, half, mutation.getHeight()),
       right = toolRect(mutation.left + half +
                            s3g::gui_layout::kStandardMetrics.panelGap,
                        mutation.top, half, mutation.getHeight());
  const char *ml[] = {"AMOUNT", "DENSITY", "SYNCOPATE", "SHIFT MAX"};
  const char *mr[] = {"BURSTS", "CYCLE DRIFT", "SEED", "ANCHORS"};
  for (int i = 0; i < 4; ++i) {
    rowLabel(left, i, ml[i]);
    rowLabel(right, i, mr[i]);
  }
  slider(control(left, 0, true), "amount", s.mutationAmount, 0, 100);
  slider(control(left, 1, true), "density", s.densityChange, -100, 100);
  slider(control(left, 2, true), "syncopation", s.syncopation, -100, 100);
  choice(control(left, 3), "displacement",
         {"OFF", "1 ROW", "2 ROWS", "3 ROWS", "4 ROWS"},
         int(s.displacementRows), [this](int i) {
           reshape.settings.displacementRows = uint32_t(i);
           reshape.refreshResult();
           invalid();
         });
  slider(control(right, 0, true), "burst_chance", s.burstChance, 0, 100);
  slider(control(right, 1, true), "cycle_drift", s.cycleDrift, 0, 100);
  button(control(right, 2), "reseed",
         format("RESEED · %04llu",
                static_cast<unsigned long long>(s.mutationSeed % 10000)),
         [this] {
           reshape.reseed();
           invalid();
         });
  label("DOWNBEAT / HIGH CONF", control(right, 3),
        themeRGB(ThemeRole::TextSecondary), Alignment::Left, 8.5);
  double width = std::max(80., commit.getWidth() - 32), cw = (width - 5) * .5;
  auto buttonRect = [&](int row) {
    return toolRect(commit.left + 16, commit.top + 35 + row * 26, width, 15);
  };
  button(
      buttonRect(0), "preview",
      reshape.preview ? "PREVIEW: ON" : "PREVIEW: OFF",
      [this] {
        reshape.togglePreview();
        invalid();
      },
      true, reshape.preview ? 1 : 0);
  auto b = buttonRect(1);
  button(
      toolRect(b.left, b.top, cw, 15), "original", "ORIGINAL",
      [this] {
        reshape.showingReshaped = false;
        reshape.syncPreview();
        invalid();
      },
      true, !reshape.showingReshaped ? 1 : 0);
  button(
      toolRect(b.left + cw + 5, b.top, cw, 15), "reshaped", "RESHAPED",
      [this] {
        reshape.showingReshaped = true;
        reshape.syncPreview();
        invalid();
      },
      true, reshape.showingReshaped ? 1 : 0);
  b = buttonRect(2);
  button(toolRect(b.left, b.top, cw, 15), "reset", "RESET", [this] {
    reshape.settings = defaultReshapePanelSettings();
    reshape.reload();
    invalid();
  });
  button(
      toolRect(b.left + cw + 5, b.top, cw, 15), "apply", "APPLY IN PLACE",
      [this] { error(reshape.apply()); },
      reshape.result.changed() && !state_.songPlaybackActive);
  button(
      buttonRect(3), "variant", "CREATE VARIANT",
      [this] { error(reshape.variant()); },
      reshape.result.changed() && bool(callbacks_.createPatternVariant) &&
          !state_.songPlaybackActive);
  multiline(reshape.resultText(),
            toolRect(commit.left + 16, commit.top + 138, width,
                     std::max(22., commit.getHeight() - 145)));
}
void AuthoringPageView::drawProfile(CRect bounds) {
  context_->saveGlobalState();
  CRect clip;
  context_->getClipRect(clip);
  clip.bound(bounds);
  context_->setClipRect(clip);
  fill(bounds, themeRGB(ThemeRole::Workspace));
  const auto &before = reshape.result.before;
  const auto &after = reshape.result.after;
  const auto &shown = reshape.showingReshaped ? after : before;
  double left = bounds.left + 78, top = bounds.top + 28,
         width = std::max(1., bounds.getWidth() - 96),
         height = std::max(1., bounds.getHeight() - 62),
         bh = std::max(12., (height - 24) / 4);
  CRect bands[4];
  for (int i = 0; i < 4; ++i)
    bands[i] = toolRect(left, top + i * (bh + 8), width, bh);
  auto cycle = std::max<std::size_t>(1, shown.cycleRows);
  auto step = cycle > 32 ? 4u : cycle > 16 ? 2u : 1u;
  DisplayList list;
  auto line = [&](std::vector<Point> p, ThemeRole role, double alpha = 1.,
                  double lw = 1.) {
    list.polyline(std::move(p), color(themeRGB(role), alpha), lw);
  };
  for (std::size_t phase = 0; phase <= cycle; phase += step) {
    double x = left + width * phase / cycle;
    line({{x, top}, {x, bands[2].bottom}}, ThemeRole::Grid, .72);
  }
  for (double y : {bands[0].bottom + 4, bands[1].bottom + 4,
                   bands[2].bottom + 4, (bands[1].top + bands[1].bottom) * .5})
    line({{left, y}, {left + width, y}}, ThemeRole::Grid, .72);
  drawDisplayList(*context_, list, services_.fontFactory);
  list = DisplayList{};
  auto maxHits = [](const PatternReshapeAnalysis &a) {
    return a.phaseNoteEvents.empty()
               ? std::size_t(0)
               : *std::max_element(a.phaseNoteEvents.begin(),
                                   a.phaseNoteEvents.end());
  };
  auto maximum =
      std::max(maxHits(before), reshape.showingReshaped ? maxHits(after) : 0);
  double mt = std::clamp(state_.session.transport.microTimingRangeMilliseconds,
                         0., 500.);
  auto scale = [&](int band, std::string title, std::string hi, std::string mid,
                   std::string lo) {
    auto b = bands[band];
    label(title, toolRect(bounds.left + 7, b.top + 3, 65, 15));
    label(hi, toolRect(bounds.left + 42, b.top, 31, 10), 0x737a80,
          Alignment::Right, 7);
    if (!mid.empty())
      label(mid,
            toolRect(bounds.left + 42, (b.top + b.bottom) * .5 - 5, 31, 10),
            0x737a80, Alignment::Right, 7);
    label(lo, toolRect(bounds.left + 42, b.bottom - 10, 31, 10), 0x737a80,
          Alignment::Right, 7);
  };
  scale(0, "HITS", std::to_string(maximum), "", "0");
  scale(1, "MT", format("+%.0fms", mt), "0", format("−%.0fms", mt));
  scale(2, "VEL", "127", "", "0");
  scale(3, "LANE", "127", "", "0");
  label(format("%lu ROW CYCLE  ·  %lu PASS%s",
               static_cast<unsigned long>(cycle),
               static_cast<unsigned long>(shown.passes),
               shown.passes == 1 ? "" : "ES"),
        toolRect(left, bounds.bottom - 22, width, 15));
  auto points = [&](const std::vector<Point> &p, ThemeRole role, double a,
                    double lw, double radius) {
    line(p, role, a, lw);
    for (auto point : p)
      list.shape(Primitive::StrokeEllipse,
                 {point.x - radius, point.y - radius, radius * 2, radius * 2},
                 color(themeRGB(role), a), 1);
  };
  auto hits = [&](const PatternReshapeAnalysis &a, bool transformed,
                  double alpha) {
    std::vector<Point> p;
    for (std::size_t phase = 0; phase < cycle; ++phase) {
      double x = left + width * (phase + .5) / cycle;
      auto hits =
          phase < a.phaseNoteEvents.size() ? a.phaseNoteEvents[phase] : 0;
      double n = maximum ? double(hits) / maximum : 0;
      p.push_back({x, bands[0].bottom - 5 - n * std::max(1., bh - 10)});
    }
    points(p, transformed ? ThemeRole::Note : ThemeRole::TextSecondary, alpha,
           transformed ? 1.8 : 1, 2.3);
  };
  hits(before, false, reshape.showingReshaped ? .38 : .92);
  if (reshape.showingReshaped)
    hits(after, true, 1);
  auto contours = [&](const PatternReshapeAnalysis &a, bool transformed,
                      double alpha) {
    std::vector<Point> timing, velocity, lanes;
    for (std::size_t phase = 0; phase < cycle; ++phase) {
      double x = left + width * (phase + .5) / cycle;
      if (phase < a.phaseTimingMedian.size() &&
          phase < a.phaseTimingSupport.size() && a.phaseTimingSupport[phase])
        timing.push_back(
            {x, (bands[1].top + bands[1].bottom) * .5 -
                    std::clamp(double(a.phaseTimingMedian[phase]), -1., 1.) *
                        bh * .42});
      if (phase < a.phaseVelocityMedian.size() &&
          phase < a.phaseVelocitySupport.size() &&
          a.phaseVelocitySupport[phase])
        velocity.push_back(
            {x, bands[2].top + 5 +
                    (bh - 10) *
                        (1 - std::clamp(double(a.phaseVelocityMedian[phase]),
                                        0., 1.))});
    }
    for (std::size_t i = 0; i < a.lanes.size(); ++i)
      if (a.lanes[i].velocityValues)
        lanes.push_back(
            {left + width * (i + .5) / a.lanes.size(),
             bands[3].top + 5 +
                 (bh - 10) * (1 - std::clamp(double(a.lanes[i].velocityMedian),
                                             0., 1.))});
    points(timing, transformed ? ThemeRole::Live : ThemeRole::TextSecondary,
           alpha, transformed ? 1.8 : 1, 2.6);
    points(velocity, transformed ? ThemeRole::Value : ThemeRole::TextSecondary,
           alpha, transformed ? 1.8 : 1, 2.6);
    points(lanes, transformed ? ThemeRole::Value : ThemeRole::TextSecondary,
           alpha, 1, 2.6);
  };
  contours(before, false, reshape.showingReshaped ? .38 : .92);
  if (reshape.showingReshaped)
    contours(after, true, 1);
  if (state_.playing && !state_.notePlayheads.empty()) {
    auto lane =
        std::min(state_.session.selectedTrack, state_.notePlayheads.size() - 1);
    auto phase = state_.notePlayheads[lane] % cycle;
    double x = left + width * phase / cycle;
    line({{x, top}, {x, bands[2].bottom}}, ThemeRole::GridPlaybackAccent, .82);
  }
  drawDisplayList(*context_, list, services_.fontFactory);
  if (!shown.noteEvents) {
    auto f = services_.font(10);
    label("NO NOTE ONSETS TO ANALYZE",
          toolRect(bounds.left,
                   (bounds.top + bounds.bottom - f.lineHeight) * .5,
                   bounds.getWidth(), f.lineHeight),
          0xa8a8a8, Alignment::Center);
  } else if (!shown.timingValues && !shown.velocityValues)
    label("WRITE MT / WRITE VEL CAN AUTHOR MISSING VALUES",
          toolRect(left, bounds.bottom - 22, width, 15), 0xa8a8a8,
          Alignment::Right);
  context_->restoreGlobalState();
}
} // namespace s3g::tracker::editor
