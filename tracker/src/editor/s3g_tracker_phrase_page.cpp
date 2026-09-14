#include "editor_geometry_support.h"
#include "s3g/tracker/fx_catalog.h"
#include "s3g_tracker_authoring_page.h"
namespace s3g::tracker::editor {
using namespace VSTGUI;
using geometry_support::format;
namespace {
CRect rect(s3g::gui_layout::Rect r) {
  return toolRect(r.x, r.y, r.width, r.height);
}
CRect control(CRect p, int row) {
  return toolRect(p.left + 108, p.top + 35 + row * 26,
                  std::max(20., p.getWidth() - 124), 15);
}
constexpr double columns[] = {0, 52, 222, 302, 402, 478, 578, 654, 800};
} // namespace
void AuthoringPageView::drawPhrases() {
  auto layout = s3g::gui_layout::trackerGeometryFamilyLayout(
      {getWidth(), getHeight()}, 1, 11, false);
  auto field = rect(layout.fieldPanel), library = rect(layout.laneCycle.frame),
       audition = rect(layout.editShape.frame),
       placement = rect(layout.trackerBridge.frame);
  panel(field, "PHRASE TRACKER  /  PROJECT MIDI PHRASE");
  panel(library, "PHRASE LIBRARY");
  panel(audition, "AUDITION");
  panel(placement, "PLACEMENT");
  beginCanvas(toolRect(field.left + 1, field.top + 21, field.getWidth() - 2,
                       field.getHeight() - 22),
              800, 30 + phrases.phrase().length * 22);
  drawPhraseGrid();
  endCanvas();
  const char *labels[] = {"BANK", "PHRASE", "NAME", "BPM", "LENGTH"};
  for (int i = 0; i < 5; ++i)
    rowLabel(library, i, labels[i]);
  std::vector<std::string> banks;
  std::vector<AssetBankId> ids;
  int current = 0;
  for (auto &b : state_.phraseBanks) {
    if (b.id == state_.activePhraseBankId)
      current = int(banks.size());
    banks.push_back(b.name.empty() ? "UNTITLED BANK" : b.name);
    ids.push_back(b.id);
  }
  choice(control(library, 0), "bank", banks, current, [this, ids](int i) {
    stopAudition();
    if (callbacks_.selectPhraseBank)
      callbacks_.selectPhraseBank(ids[i]);
    reloadModel();
  });
  std::vector<std::string> slots;
  for (std::size_t i = 0; i < state_.phraseLibrary.phrases.size(); ++i) {
    auto &p = state_.phraseLibrary.phrases[i];
    slots.push_back(format("P%02lu · %s", static_cast<unsigned long>(i + 1),
                           p.name.empty() ? "EMPTY" : p.name));
  }
  choice(control(library, 1), "phrase", slots, int(state_.selectedPhrase),
         [this](int i) {
           state_.selectedPhrase = std::size_t(i);
           reloadModel();
         });
  auto name = control(library, 2);
  name.top -= 7;
  name.bottom = name.top + 24;
  auto bpm = control(library, 3);
  bpm.top -= 7;
  bpm.bottom = bpm.top + 24;
  textBox(name, "name", nameDraft_, [this, name] {
    startText("name", name, nameDraft_, [this](std::string s) {
      // Match Cocoa: metadata remains a draft until the explicit SAVE action.
      nameDraft_ = std::move(s);
      return true;
    });
  });
  textBox(bpm, "bpm", bpmDraft_, [this, bpm] {
    startText("bpm", bpm, bpmDraft_, [this](std::string s) {
      bpmDraft_ = std::move(s);
      return true;
    });
  });
  std::vector<std::string> lengths;
  for (int i = 2; i <= 64; ++i)
    lengths.push_back(std::to_string(i) + " ROWS");
  choice(control(library, 4), "length", lengths,
         int(phrases.phrase().length) - 2, [this](int i) {
           stopAudition();
           phrases.length(std::size_t(i + 2));
           invalid();
         });
  auto buttons =
      [&](int row,
          const std::vector<std::pair<std::string, std::function<void()>>>
              &list) {
        auto r = control(library, row);
        double w = (r.getWidth() - 4 * (list.size() - 1)) / list.size();
        for (std::size_t i = 0; i < list.size(); ++i)
          button(toolRect(r.left + i * (w + 4), r.top, w, 15), list[i].first,
                 list[i].first, list[i].second,
                 list[i].first != "DELETE BANK" ||
                     state_.activePhraseBankId != kProjectAssetBankId);
      };
  buttons(5, {{"SAVE",
               [this] {
                 if (phrases.save(nameDraft_, bpmDraft_))
                   reloadModel();
                 else
                   error(false);
               }},
              {"DUP",
               [this] {
                 stopAudition();
                 error(phrases.duplicate());
                 reloadModel();
               }},
              {"DELETE", [this] {
                 stopAudition();
                 phrases.erase();
                 reloadModel();
               }}});
  buttons(8, {{"IMPORT PACK",
               [this] {
                 stopAudition();
                 if (callbacks_.importAssetPack)
                   callbacks_.importAssetPack();
               }},
              {"EXPORT ONE", [this] {
                 auto &p = phrases.phrase();
                 bool has = !p.empty() || !p.name.empty() ||
                            p.recommendedBpm.has_value();
                 if (has && callbacks_.exportPhraseAssetPack)
                   callbacks_.exportPhraseAssetPack(state_.selectedPhrase);
                 else
                   error(false);
               }}});
  buttons(9, {{"EXPORT BANK",
               [this] {
                 bool has = false;
                 for (auto &p : state_.phraseLibrary.phrases)
                   has |= !p.empty() || !p.name.empty() ||
                          p.recommendedBpm.has_value();
                 if (has && callbacks_.exportPhraseLibraryAssetPack)
                   callbacks_.exportPhraseLibraryAssetPack();
                 else
                   error(false);
               }},
              {"COPY PROJECT", [this] {
                 stopAudition();
                 if (!callbacks_.copyPhraseToProject)
                   return;
                 auto i = callbacks_.copyPhraseToProject(state_.selectedPhrase);
                 if (i >= kPhraseLibrarySlots) {
                   error(false);
                   return;
                 }
                 state_.selectedPhrase = i;
                 reloadModel();
               }}});
  buttons(10, {{"CLEAR BANK",
                [this] {
                  stopAudition();
                  if (callbacks_.clearPhraseBank)
                    callbacks_.clearPhraseBank();
                }},
               {"DELETE BANK", [this] {
                  stopAudition();
                  if (callbacks_.deletePhraseBank)
                    callbacks_.deletePhraseBank();
                }}});
  rowLabel(audition, 0, "MIDI CH");
  std::vector<std::string> channels;
  for (int i = 1; i <= 16; ++i)
    channels.push_back(format("%02d", i));
  choice(control(audition, 0), "channel", channels,
         int(phrases.phrase().previewMidiChannel) - 1, [this](int i) {
           stopAudition();
           phrases.phrase().previewMidiChannel = uint8_t(i + 1);
           phrases.changed();
         });
  button(
      toolRect(audition.right - 90, audition.top + 3, 78, 15), "listen",
      "LISTEN ▶",
      [this] {
        if (auditionRow_ >= 0)
          stopAudition();
        else
          startAudition();
      },
      true, auditionRow_ >= 0 ? 1 : 0);
  button(
      toolRect(audition.right - 172, audition.top + 3, 78, 15), "loop",
      state_.phraseLoopPreview ? "LOOP: ON" : "LOOP: OFF",
      [this] {
        state_.phraseLoopPreview = !state_.phraseLoopPreview;
        if (!state_.phraseLoopPreview)
          stopAudition();
        else if (auditionToken_ && callbacks_.loopAuthoringPreview)
          callbacks_.loopAuthoringPreview(auditionToken_, true);
        invalid();
      },
      true, state_.phraseLoopPreview ? 1 : 0);
  rowLabel(placement, 0, "MODE");
  rowLabel(placement, 1, "TARGET");
  choice(control(placement, 0), "mode", {"REPLACE", "MERGE EMPTY"},
         merge_ ? 1 : 0, [this](int i) {
           merge_ = i == 1;
           invalid();
         });
  auto target = control(placement, 1);
  auto font = services_.font(10);
  target.offset(0, std::floor((15 - font.lineHeight) * .5));
  label(
      state_.session.pattern.tracks.empty()
          ? "NO LANES"
          : format("T%02lu  ·  ROW %03lu",
                   static_cast<unsigned long>(
                       std::min(state_.session.selectedTrack,
                                state_.session.pattern.tracks.size() - 1) +
                       1),
                   static_cast<unsigned long>(state_.session.selectedRow + 1)),
      target);
  bodyText(phrases.status,
           toolRect(placement.left + 16, placement.top + 87,
                    placement.getWidth() - 32, 30),
           ThemeRole::TextMuted, 8);
  button(
      toolRect(placement.right - 154, placement.top + 3, 142, 15), "place",
      "PLACE IN TRACKER",
      [this] {
        error(phrases.place(state_.session.selectedTrack,
                            state_.session.selectedRow, merge_));
      },
      !state_.songPlaybackActive && !state_.session.pattern.tracks.empty());
}
void AuthoringPageView::drawPhraseGrid() {
  const auto &p = phrases.phrase();
  double x = canvas_.left - scroll_.x, y0 = canvas_.top - scroll_.y,
         w = content_.x;
  auto box = [&](double a, double b, double c, double d) {
    return toolRect(x + a, y0 + b, c, d);
  };
  auto paint = [&](CRect r, ThemeRole c, double a = 1.) {
    fill(r, themeRGB(c), a);
  };
  paint(canvas_, ThemeRole::Workspace);
  paint(box(0, 0, w, 30), ThemeRole::Panel);
  paint(box(0, 0, w, 2), ThemeRole::Focus);
  const char *titles[] = {"ROW", "NOTE", "VOL", "SEQ1",
                          "V1",  "SEQ2", "V2",  "GATE"};
  ThemeRole roles[] = {ThemeRole::TextMuted, ThemeRole::Note,
                       ThemeRole::Value,     ThemeRole::Warning,
                       ThemeRole::Value,     ThemeRole::Warning,
                       ThemeRole::Value,     ThemeRole::Value};
  for (std::size_t f = 0; f < 8; ++f) {
    bodyText(titles[f],
             box(columns[f] + 6, 7, columns[f + 1] - columns[f] - 12, 15),
             roles[f], 8, FontWeight::Semibold,
             f ? Alignment::Center : Alignment::Right);
    if (f)
      paint(box(columns[f], 2, 1, 28), ThemeRole::Grid, .7);
  }
  for (std::size_t r = 0; r < p.length; ++r) {
    double y = 30 + r * 22;
    if (y0 + y > canvas_.bottom || y0 + y + 22 < canvas_.top)
      continue;
    paint(box(0, y, w, 22), r % 4 ? ThemeRole::Panel : ThemeRole::Raised);
    paint(box(0, y, 51, 22), r % 4 ? ThemeRole::Raised : ThemeRole::Control);
    if (r == phrases.row)
      paint(box(0, y, w, 22), ThemeRole::Focus, .11);
    else if (r % 4 == 0)
      paint(box(0, y, w, 1), ThemeRole::Border, .72);
    for (std::size_t f = 0; f < 7; ++f) {
      auto b = box(columns[f + 1], y, columns[f + 2] - columns[f + 1], 22);
      if (int(r) == auditionRow_) {
        paint(CRect(b).inset(1, 1), ThemeRole::GridPlayback);
        if (r != phrases.row || f != phrases.field)
          paint(toolRect(b.left + 1, b.top + 3, 2, 16),
                ThemeRole::GridPlaybackAccent);
      }
      if (phrases.selection.active &&
          phrases.selection.range().contains(0, 0, f, r))
        paint(CRect(b).inset(1, 1), ThemeRole::GridSelection);
      if (r == phrases.row && f == phrases.field)
        paint(CRect(b).inset(1, 1), ThemeRole::GridCursor);
    }
    bool active[] = {false,
                     p.notes[r].state != NoteCellState::Rest,
                     p.velocities[r].state == ValueCellState::Value,
                     p.fxPairs[0].actions[r].state != FxActionCellState::Empty,
                     p.fxPairs[0].values[r].state == FxValueCellState::Value,
                     p.fxPairs[1].actions[r].state != FxActionCellState::Empty,
                     p.fxPairs[1].values[r].state == FxValueCellState::Value,
                     p.gates[r].voiceCount > 0};
    for (std::size_t f = 0; f < 8; ++f) {
      auto role = f == 0
                      ? (r == phrases.row ? ThemeRole::Focus
                                          : r % 4 == 0 ? ThemeRole::TextMuted
                                                       : ThemeRole::TextFaint)
                      : !active[f] ? ThemeRole::TextFaint
                                   : f == 1 ? ThemeRole::TextPrimary : roles[f];
      bodyText(f ? phraseGridCellText(p, f - 1, r)
                 : format("%02lu", static_cast<unsigned long>(r + 1)),
               box(columns[f] + 6, y + 4, columns[f + 1] - columns[f] - 12, 15),
               role, f == 0 ? 9.6 : f == 2 ? 9.2 : 10,
               f == 0 && r == phrases.row
                   ? FontWeight::Semibold
                   : f == 0 || active[f] ? FontWeight::Medium
                                         : FontWeight::Regular,
               f == 0 || f == 2 ? Alignment::Right : Alignment::Center);
      if (f)
        paint(box(columns[f], y, 1, 22), ThemeRole::Grid, .7);
    }
    paint(box(0, y + 21, w, 1), ThemeRole::Grid, .7);
  }
}
void AuthoringPageView::gridMenu(CRect r) {
  auto f = phrases.field, row = phrases.row;
  auto &p = phrases.phrase();
  std::vector<ToolMenuItem> items;
  if (f == 2 || f == 4) {
    auto c = p.fxPairs[(f - 2) / 2].actions[row];
    items.push_back(
        {"SEQ ACTION  ·  NORMALIZED VALUE 0.000–1.000", {}, false, false});
    items.push_back({"---   CLEAR",
                     [this] {
                       phrases.action("---");
                       invalid();
                     },
                     c.state == FxActionCellState::Empty});
    items.push_back({"PRV   PREVIOUS / RECALL",
                     [this] {
                       phrases.action("PRV");
                       invalid();
                     },
                     c.state == FxActionCellState::Previous});
    for (std::size_t i = 0; i < sequencerActionCount(); ++i) {
      auto *a = sequencerAction(i);
      if (!a)
        continue;
      std::string token(a->mnemonic);
      items.push_back(
          {token + "   " +
               geometry_support::uppercase(std::string(a->displayName)) +
               "  ·  " + std::string(a->valueMeaning),
           [this, token] {
             phrases.action(token);
             invalid();
           },
           c.state == FxActionCellState::Sequencer &&
               c.sequencerAction == a->action});
    }
    // Preserve the original CC grouping as successive portable canvas menus.
    items.push_back(
        {"MIDI CONTROL CHANGE  →", [this, r] {
           std::vector<ToolMenuItem> groups;
           for (int group = 0; group < 4; ++group)
             groups.push_back(
                 {format("CC%03d–CC%03d  →", group * 32, group * 32 + 31),
                  [this, r, group] {
                    std::vector<ToolMenuItem> cc;
                    auto &c = phrases.phrase()
                                  .fxPairs[(phrases.field - 2) / 2]
                                  .actions[phrases.row];
                    for (int i = group * 32; i < group * 32 + 32; ++i)
                      cc.push_back(
                          {format("CC%03d", i),
                           [this, i] {
                             phrases.action("CC" + std::to_string(i));
                             invalid();
                           },
                           c.state == FxActionCellState::MidiControlChange &&
                               c.midiController == i});
                    openToolMenu(r, cc);
                  }});
           openToolMenu(r, groups);
         }});
  } else if ((f == 3 || f == 5) &&
             p.fxPairs[(f - 3) / 2].actions[row].state ==
                 FxActionCellState::Sequencer &&
             p.fxPairs[(f - 3) / 2].actions[row].sequencerAction ==
                 SequencerAction::Condition) {
    auto &v = p.fxPairs[(f - 3) / 2].values[row];
    auto current = v.state == FxValueCellState::Value
                       ? sequencerConditionFromNormalized(v.normalized)
                       : SequencerCondition::FirstOf2;
    items.push_back({"CD  ·  PLAY THIS NOTE WHEN", {}, false, false});
    for (std::size_t i = 0; i < kSequencerConditionCount; ++i) {
      auto *c = sequencerCondition(i);
      if (c)
        items.push_back(
            {std::string(c->token) + "   " + std::string(c->displayName),
             [this, i] {
               phrases.condition(i);
               invalid();
             },
             current == c->condition});
    }
  }
  if (!items.empty()) {
    r.right = std::min(getWidth() - 8., r.left + 570);
    openToolMenu(r, std::move(items));
  }
}
} // namespace s3g::tracker::editor
