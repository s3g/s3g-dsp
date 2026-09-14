#pragma once
#include "s3g/tracker/editor_state.h"
#include "s3g/tracker/project_history.h"

namespace s3g::tracker {
// CLAP product policy and editor synchronization. No window, timer, host or
// audio-thread calls: adapters own those boundaries and preserve their order.
double normalizedTempoScale(double value) noexcept;
void normalizeMidiOnlyDocument(ProjectDocument &document);
bool syncSessionToActivePattern(app::TrackerViewState &state);
bool loadActivePatternIntoSession(app::TrackerViewState &state);
bool syncActiveAssetBanks(app::TrackerViewState &state);
bool loadActiveAssetBanks(app::TrackerViewState &state);

class ClapDocumentController {
public:
  explicit ClapDocumentController(app::TrackerViewState &state)
      : state_(state) {}
  ProjectDocument snapshot(const SongArrangement &song);
  // Returns the normalized document so the adapter can load the pattern
  // catalog before applying Song rows (otherwise saved mute masks are lost).
  ProjectDocument apply(const ProjectDocument &document);
  ProjectResult resetHistory(const ProjectDocument &document);
  ProjectResult recordHistory(const ProjectDocument &document);
  ProjectResult undo(ProjectDocument &document);
  ProjectResult redo(ProjectDocument &document);
  void updateHistoryAvailability();

private:
  app::TrackerViewState &state_;
  ProjectHistory history_;
};
} // namespace s3g::tracker
