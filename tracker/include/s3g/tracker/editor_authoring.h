#pragma once
#include "s3g/tracker/editor_grid_selection.h"
#include "s3g/tracker/editor_state.h"
#include "s3g/tracker/pattern_reshape.h"
#include <set>
#include <variant>

namespace s3g::tracker::editor {
// The final authoring pages retain the Cocoa document operations. Platform
// adapters supply text/clipboard/timers; these models never draw or clock MIDI.
using PhraseGridCell =
    std::variant<NoteCell, ValueCell, FxActionCell, FxValueCell, GateCell>;
uint8_t phraseGridFieldType(std::size_t);
PhraseGridCell phraseGridCellAt(const PhraseDefinition &, std::size_t field,
                                std::size_t row);
void writePhraseGridCell(PhraseDefinition &, std::size_t field, std::size_t row,
                         const PhraseGridCell &);
PhraseGridCell blankPhraseGridCell(std::size_t field);
std::string phraseGridCellText(const PhraseDefinition &, std::size_t field,
                               std::size_t row);
bool applyPhraseCellText(std::string, PhraseDefinition &,
                         const app::TrackerViewState &, std::size_t row,
                         std::size_t field);

struct AuthoringAudition {
  std::vector<PitchPreviewEvent> events;
  std::size_t firstRow = 0, lastRow = 0;
  uint8_t channel = 1;
  double bpm = 120;
  uint32_t ticksPerBeat = 4;
  double rowSeconds() const;
};
AuthoringAudition phraseAudition(const app::TrackerViewState &,
                                 const PhraseDefinition &);

class PhraseEditor {
public:
  PhraseEditor(app::TrackerViewState &s, app::WorkspaceCallbacks &c)
      : state(s), callbacks(c) {
    reload();
  }
  app::TrackerViewState &state;
  app::WorkspaceCallbacks &callbacks;
  std::size_t row = 0, field = 0;
  app::GridSelection selection;
  std::string status;
  PhraseDefinition &phrase();
  void reload();
  void changed();
  void resetSelection();
  void selectAll();
  app::GridSelectionRange range() const;
  void clear();
  bool edit(std::string);
  std::string copy();
  // The host must verify the clipboard's identity, not just text equality,
  // before asking for exact typed-cell reuse (avoids lossy float round trips).
  bool paste(std::string text, bool sameClipboard);
  bool save(std::string name, std::string bpm);
  bool duplicate();
  void erase();
  void length(std::size_t);
  bool capture(std::size_t track, std::size_t first, std::size_t last);
  bool place(std::size_t track, std::size_t at, bool merge);
  void action(std::string token); // Context-menu action also seeds its value.
  void condition(std::size_t index);

private:
  std::vector<PhraseGridCell> copied_;
  std::vector<uint8_t> types_;
  std::size_t copiedRows_ = 0, copiedFields_ = 0;
};

const PhraseDefinition *assemblyPhrase(const app::TrackerViewState &,
                                       const PhraseAssemblyBlock &);
std::size_t assemblyBlockRows(const app::TrackerViewState &,
                              const PhraseAssemblyBlock &);
double assemblyBlockHeight(const app::TrackerViewState &,
                           const PhraseAssemblyBlock &);
class AssembleEditor {
public:
  AssembleEditor(app::TrackerViewState &s, app::WorkspaceCallbacks &c)
      : state(s), callbacks(c) {
    reload();
  }
  app::TrackerViewState &state;
  app::WorkspaceCallbacks &callbacks;
  std::set<std::size_t> selected;
  std::string status;
  uint32_t repeats = 1;
  bool wholePreview = true;
#if defined(_WIN32)
  uint32_t effectiveTargetTrack() const;
#endif
  void reload();
  void changed();
  std::size_t rows() const;
  double contentHeight() const;
  double pixelYForRow(std::size_t) const;
  int blockAt(double y, bool insertion) const;
  bool append();
  bool move(std::size_t destination, bool copy);
  void up();
  void down();
  void duplicate();
  void erase();
  void clear();
  bool place();
  void reveal();
  bool savePhrase();
  AuthoringAudition audition() const;
};

PatternReshapeSettings defaultReshapePanelSettings();
class ReshapeEditor {
public:
  ReshapeEditor(app::TrackerViewState &s, app::WorkspaceCallbacks &c)
      : state(s), callbacks(c) {
    reload();
  }
  app::TrackerViewState &state;
  app::WorkspaceCallbacks &callbacks;
  PatternReshapeSettings settings = defaultReshapePanelSettings();
  PatternReshapeResult result;
  bool preview = false, showingReshaped = true;
  std::string loadedPatternId;
  void reload();
  void refreshResult();
  void syncPreview();
  void clearPreview();
  void togglePreview();
  void toggleLane(int lane);
  void reseed();
  bool apply();
  bool variant();
  std::string laneTitle() const;
  std::string analysisText() const;
  std::string resultText() const;

private:
  bool publishingPreview_ = false;
};
} // namespace s3g::tracker::editor
