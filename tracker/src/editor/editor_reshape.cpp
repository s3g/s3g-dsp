#include "editor_geometry_support.h"
#include "s3g/tracker/editor_authoring.h"
namespace s3g::tracker::editor {
using namespace geometry_support;
PatternReshapeSettings defaultReshapePanelSettings() {
  PatternReshapeSettings s;
  s.microTimingWrite = PatternReshapeWriteMode::FillMissing;
  s.mutationAmount = .55f;
  s.densityChange = .15f;
  s.syncopation = .35f;
  s.displacementRows = 1;
  s.burstChance = .20f;
  s.cycleDrift = .20f;
  return s;
}
void ReshapeEditor::reload() {
  if (loadedPatternId != state.patternBank.activePatternId)
    clearPreview();
  loadedPatternId = state.patternBank.activePatternId;
  auto n = std::min<std::size_t>(state.session.pattern.tracks.size(), 32);
  uint32_t valid = n >= 32 ? 0xffffffffu : n ? (1u << n) - 1 : 0;
  if (settings.laneMask != 0xffffffffu) {
    settings.laneMask &= valid;
    if (!settings.laneMask && valid)
      settings.laneMask = 1u << std::min(state.session.selectedTrack, n - 1);
  }
  if ((settings.laneMask & valid) == valid)
    settings.laneMask = 0xffffffffu;
  refreshResult();
}
void ReshapeEditor::refreshResult() {
  settings.laneDefaultNotes = state.session.laneDefaultNotes;
  settings.burstBankId = state.activeBurstBankId;
  result = reshapePattern(state.session.pattern, state.session.burstLibrary,
                          settings);
  syncPreview();
}
void ReshapeEditor::syncPreview() {
  if (!preview || publishingPreview_)
    return;
  // A failed publication may synchronously reload every page. Do not recurse
  // into the same host callback while its first publication is still active.
  publishingPreview_ = true;
  if (showingReshaped) {
    if (callbacks.previewPattern) {
      const auto snapshot = result.pattern;
      callbacks.previewPattern(snapshot);
    }
  } else if (callbacks.clearPatternPreview)
    callbacks.clearPatternPreview();
  publishingPreview_ = false;
}
void ReshapeEditor::clearPreview() {
  if (!preview)
    return;
  preview = false;
  if (callbacks.clearPatternPreview)
    callbacks.clearPatternPreview();
}
void ReshapeEditor::togglePreview() {
  if (preview)
    clearPreview();
  else {
    preview = true;
    showingReshaped = true;
    syncPreview();
  }
}
void ReshapeEditor::toggleLane(int lane) {
  if (lane < 0)
    settings.laneMask = 0xffffffffu;
  else {
    if (std::size_t(lane) >= state.session.pattern.tracks.size() || lane >= 32)
      return;
    uint32_t bit = 1u << lane;
    if (settings.laneMask == 0xffffffffu)
      settings.laneMask = bit;
    else if (settings.laneMask & bit) {
      if (settings.laneMask & ~bit)
        settings.laneMask &= ~bit;
    } else
      settings.laneMask |= bit;
  }
  reload();
}
void ReshapeEditor::reseed() {
  settings.mutationSeed =
      settings.mutationSeed * 6364136223846793005ULL + 1442695040888963407ULL;
  if (!settings.mutationSeed)
    settings.mutationSeed = 1;
  refreshResult();
}
bool ReshapeEditor::apply() {
  if (state.songPlaybackActive || !result.changed())
    return false;
  auto pattern = result.pattern;
  auto library = result.burstLibrary;
  clearPreview();
  state.session.pattern = std::move(pattern);
  state.session.burstLibrary = std::move(library);
  state.status = "Pattern reshape applied";
  if (callbacks.patternChanged)
    callbacks.patternChanged();
  return true;
}
bool ReshapeEditor::variant() {
  if (state.songPlaybackActive || !result.changed() ||
      !callbacks.createPatternVariant)
    return false;
  auto pattern = result.pattern;
  clearPreview();
  // Rhythm mutation references existing bank-qualified Bursts; it does not
  // author new definitions or replace the library when creating a variant.
  callbacks.createPatternVariant(pattern);
  return true;
}
std::string ReshapeEditor::laneTitle() const {
  if (settings.laneMask == 0xffffffffu)
    return "ALL LANES";
  std::size_t count = 0, last = 0;
  for (std::size_t i = 0;
       i < std::min<std::size_t>(32, state.session.pattern.tracks.size()); ++i)
    if (settings.laneMask & (1u << i)) {
      ++count;
      last = i;
    }
  return count == 1
             ? format("L%02lu ONLY", static_cast<unsigned long>(last + 1))
             : format("%lu LANES", static_cast<unsigned long>(count));
}
std::string ReshapeEditor::analysisText() const {
  auto &b = result.before;
  return format("%lu HITS  ·  %lu MT  ·  %lu VEL\n%lu MT TARGETS  ·  %lu VEL "
                "DEFAULTS\nCONFIDENCE %.0f%%",
                static_cast<unsigned long>(b.noteEvents),
                static_cast<unsigned long>(b.timingValues),
                static_cast<unsigned long>(b.velocityValues),
                static_cast<unsigned long>(b.writableTimingOnsets),
                static_cast<unsigned long>(b.defaultVelocityValues),
                double(b.confidence * 100.f));
}
std::string ReshapeEditor::resultText() const {
  auto &r = result;
  auto rhythm =
      settings.mutationAmount > 0
          ? format("HITS +%lu −%lu · MOVE %lu · BURST %lu\nCYCLES %lu CHANGED",
                   static_cast<unsigned long>(r.notesAdded),
                   static_cast<unsigned long>(r.notesRemoved),
                   static_cast<unsigned long>(r.notesMoved),
                   static_cast<unsigned long>(r.burstsCreated),
                   static_cast<unsigned long>(r.cyclesChanged))
          : "HITS PRESERVED";
  return format("%s\nMT %lu CHANGED / %lu ADDED\nVEL %lu CHANGED / %lu ADDED%s",
                rhythm, static_cast<unsigned long>(r.timingChanged),
                static_cast<unsigned long>(r.timingCreated),
                static_cast<unsigned long>(r.velocityChanged),
                static_cast<unsigned long>(r.velocityCreated),
                r.timingSkipped
                    ? format("  ·  %lu MT protected",
                             static_cast<unsigned long>(r.timingSkipped))
                    : "");
}
} // namespace s3g::tracker::editor
