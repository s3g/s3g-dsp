#include "editor_geometry_support.h"
#include "s3g_tracker_authoring_page.h"
#include "vstgui/lib/cgraphicspath.h"
namespace s3g::tracker::editor {
using namespace VSTGUI;
using geometry_support::format;
namespace {
CRect rect(s3g::gui_layout::Rect r) {
  return toolRect(r.x, r.y, r.width, r.height);
}
CRect control(CRect p, int row) {
  return toolRect(p.left + 108, p.top + 35 + row * 26, p.getWidth() - 124, 15);
}
} // namespace
void AuthoringPageView::drawAssemble() {
  auto layout = s3g::gui_layout::trackerGeometryFamilyLayout(
      {getWidth(), getHeight()}, 9, 4, false);
  auto field = rect(layout.fieldPanel), palette = rect(layout.laneCycle.frame),
       assembly = rect(layout.editShape.frame),
       target = rect(layout.trackerBridge.frame);
  panel(field, "ASSEMBLY TRAY  /  VERTICAL PATTERN FLOW");
  panel(palette, "PHRASE PALETTE");
  panel(assembly, "ASSEMBLY / AUDITION");
  panel(target, "PLACEMENT");
  beginCanvas(toolRect(field.left + 1, field.top + 21, field.getWidth() - 2,
                       field.getHeight() - 22),
              560, assemble.contentHeight());
  drawAssembly();
  endCanvas();
  rowLabel(palette, 0, "BANK");
  rowLabel(palette, 1, "PHRASE");
  rowLabel(palette, 2, "REPEAT");
  std::vector<std::string> banks;
  std::vector<AssetBankId> ids;
  int current = 0;
  for (auto &b : state_.phraseBanks) {
    if (b.id == state_.activePhraseBankId)
      current = int(banks.size());
    banks.push_back(assetBankToken(b.id) + " · " + b.name);
    ids.push_back(b.id);
  }
  choice(control(palette, 0), "bank", banks, current, [this, ids](int i) {
    stopAudition();
    if (callbacks_.selectPhraseBank)
      callbacks_.selectPhraseBank(ids[i]);
    reloadModel();
  });
  std::vector<std::string> slots;
  for (std::size_t i = 0; i < state_.phraseLibrary.phrases.size(); ++i) {
    auto &p = state_.phraseLibrary.phrases[i];
    slots.push_back(format("P%02lu · %s · %lu",
                           static_cast<unsigned long>(i + 1),
                           p.name.empty() ? "EMPTY" : p.name,
                           static_cast<unsigned long>(p.length)));
  }
  choice(control(palette, 1), "phrase", slots, int(state_.selectedPhrase),
         [this](int i) {
           stopAudition();
           state_.selectedPhrase = std::size_t(i);
           invalid();
         });
  const uint32_t repeats[] = {1, 2, 4, 8, 16};
  int ri = 0;
  for (int i = 0; i < 5; ++i)
    if (assemble.repeats == repeats[i])
      ri = i;
  choice(control(palette, 2), "repeat", {"1×", "2×", "4×", "8×", "16×"}, ri,
         [this](int i) {
           constexpr uint32_t n[] = {1, 2, 4, 8, 16};
           assemble.repeats = n[i];
           invalid();
         });
  button(
      control(palette, 3), "append", "APPEND  [A]",
      [this] {
        stopAudition();
        error(assemble.append());
      },
      state_.assembly.blocks.size() < kMaximumAssemblyBlocks &&
          assemble.rows() < 256);
  rowLabel(assembly, 0, "PREVIEW");
  rowLabel(assembly, 1, "MIDI CH");
  choice(control(assembly, 0), "scope", {"ASSEMBLY", "SELECTED PHRASE"},
         assemble.wholePreview ? 0 : 1, [this](int i) {
           stopAudition();
           assemble.wholePreview = i == 0;
           invalid();
         });
  std::vector<std::string> channels;
  for (int i = 1; i <= 16; ++i)
    channels.push_back(format("%02d", i));
  choice(control(assembly, 1), "channel", channels,
         int(state_.assembly.previewMidiChannel) - 1, [this](int i) {
           stopAudition();
           state_.assembly.previewMidiChannel = uint8_t(i + 1);
           assemble.changed();
         });
  button(
      toolRect(assembly.right - 90, assembly.top + 3, 78, 15), "listen",
      "LISTEN ▶",
      [this] {
        if (auditionRow_ >= 0)
          stopAudition();
        else
          startAudition();
      },
      true, auditionRow_ >= 0 ? 1 : 0);
  button(
      toolRect(assembly.right - 172, assembly.top + 3, 78, 15), "loop",
      state_.assembly.loopPreview ? "LOOP: ON" : "LOOP: OFF",
      [this] {
        state_.assembly.loopPreview = !state_.assembly.loopPreview;
        if (auditionToken_ && callbacks_.loopAuthoringPreview)
          callbacks_.loopAuthoringPreview(auditionToken_, state_.assembly.loopPreview);
        assemble.changed();
      },
      true, state_.assembly.loopPreview ? 1 : 0);
  auto b = control(assembly, 5);
  b.offset(0, 1);
  double ew = (b.getWidth() - 12) / 4;
  const char *idsEdit[] = {"up", "down", "duplicate", "delete"};
  const char *titles[] = {"↑", "↓", "DUP", "DELETE"};
  for (int i = 0; i < 4; ++i)
    button(toolRect(b.left + i * (ew + 4), b.top, ew, 15), idsEdit[i],
           titles[i], [this, i] {
             stopAudition();
             if (i == 0)
               assemble.up();
             else if (i == 1)
               assemble.down();
             else if (i == 2)
               assemble.duplicate();
             else
               assemble.erase();
             invalid();
           });
  b = control(assembly, 6);
  b.offset(0, 1);
  double half = (b.getWidth() - 4) / 2;
  button(toolRect(b.left, b.top, half, 15), "clear", "CLEAR", [this] {
    stopAudition();
    assemble.clear();
    invalid();
  });
  button(
      toolRect(b.left + half + 4, b.top, half, 15), "save_phrase",
      "SAVE AS PHRASE",
      [this] {
        stopAudition();
        error(assemble.savePhrase());
      },
      assemble.rows() >= 2 && assemble.rows() <= 64);
  auto font = services_.font(10);
  label(format("%lu BLOCKS  ·  %lu ROWS",
               static_cast<unsigned long>(state_.assembly.blocks.size()),
               static_cast<unsigned long>(assemble.rows())),
        toolRect(assembly.left + 16,
                 assembly.top + 244 + std::floor((15 - font.lineHeight) * .5),
                 assembly.getWidth() - 32, 15));
  const char *targets[] = {"PATTERN", "LANE", "ROW", "MODE", "FIT"};
  for (int i = 0; i < 5; ++i)
    rowLabel(target, i, targets[i]);
  std::vector<std::string> patterns;
  std::vector<std::string> patternIds;
  current = 0;
  for (auto &p : state_.patternBank.entries) {
    if (p.id == state_.patternBank.activePatternId)
      current = int(patterns.size());
    patterns.push_back(p.id + " · " + p.pattern.name);
    patternIds.push_back(p.id);
  }
  choice(control(target, 0), "pattern", patterns, current,
         [this, patternIds](int i) {
           stopAudition();
           if (callbacks_.selectPattern)
             callbacks_.selectPattern(patternIds[i]);
         });
  std::vector<std::string> lanes;
  for (std::size_t i = 0; i < state_.session.pattern.tracks.size(); ++i)
    lanes.push_back(format("L%02lu · %s", static_cast<unsigned long>(i + 1),
                           state_.session.pattern.tracks[i].name));
#if defined(_WIN32)
  choice(control(target, 1), "lane", lanes, int(assemble.effectiveTargetTrack()),
#else
  choice(control(target, 1), "lane", lanes, int(state_.assembly.targetTrack),
#endif
         [this](int i) {
           state_.assembly.targetTrack = uint32_t(i);
           assemble.changed();
         });
  std::vector<std::string> rows;
  for (int i = 1; i <= 256; ++i)
    rows.push_back(format("%03d", i));
  choice(control(target, 2), "row", rows, int(state_.assembly.targetRow),
         [this](int i) {
           state_.assembly.targetRow = uint32_t(i);
           assemble.changed();
         });
  choice(control(target, 3), "mode", {"REPLACE", "MERGE EMPTY"},
         int(state_.assembly.placementMode), [this](int i) {
           state_.assembly.placementMode = AssemblyPlacementMode(i);
           assemble.changed();
         });
  choice(control(target, 4), "fit",
         {"EXTEND PATTERN", "CROP AT 256", "WRAP AT 256"},
         int(state_.assembly.fitMode), [this](int i) {
           state_.assembly.fitMode = AssemblyFitMode(i);
           assemble.changed();
         });
  button(
      toolRect(target.right - 154, target.top + 3, 142, 15), "place",
      "PLACE IN TRACKER",
      [this] {
        stopAudition();
        bool ok = assemble.place();
        if (ok)
          placeFlashUntil_ = now() + .16;
        error(ok);
      },
      !state_.songPlaybackActive && !state_.assembly.blocks.empty() &&
          !state_.session.pattern.tracks.empty(),
      now() < placeFlashUntil_ ? 1 : 0);
  b = control(target, 6);
  b.offset(0, 1);
  button(b, "reveal", "REVEAL IN TRACKER", [this] { assemble.reveal(); });
  bodyText(
      assemble.status,
      toolRect(target.left + 16, target.top + 244, target.getWidth() - 32, 30),
      ThemeRole::TextMuted, 8);
}
void AuthoringPageView::drawAssembly() {
  double x = canvas_.left - scroll_.x, y = canvas_.top - scroll_.y,
         w = content_.x;
  fill(canvas_, themeRGB(ThemeRole::Workspace));
  fill(toolRect(x, y, w, 28), themeRGB(ThemeRole::Raised));
  bodyText("ROW", toolRect(x + 12, y + 8, 40, 15), ThemeRole::TextSecondary, 9,
           FontWeight::Semibold);
  bodyText(state_.session.pattern.tracks.empty()
               ? "PHRASE BLOCKS"
               : format("TARGET L%02u  /  PHRASE BLOCKS",
#if defined(_WIN32)
                        assemble.effectiveTargetTrack() + 1),
#else
                        state_.assembly.targetTrack + 1),
#endif
           toolRect(x + 60, y + 8, w - 60, 15), ThemeRole::TextSecondary, 9,
           FontWeight::Semibold);
  y += 28;
  std::size_t start = state_.assembly.targetRow;
  for (std::size_t i = 0; i < state_.assembly.blocks.size(); ++i) {
    auto &b = state_.assembly.blocks[i];
    auto *p = assemblyPhrase(state_, b);
    auto rows = assemblyBlockRows(state_, b);
    double h = assemblyBlockHeight(state_, b);
    auto box = toolRect(x + 54, y + 2, w - 62, h - 4);
    auto path = owned(context_->createGraphicsPath());
    if (path) {
      path->addRoundRect(box, 3);
      auto c = color(themeRGB(assemble.selected.count(i) ? ThemeRole::Selection
                                                         : ThemeRole::Control));
      context_->setFillColor({c.red, c.green, c.blue, c.alpha});
      context_->drawGraphicsPath(path, CDrawContext::kPathFilled);
      path = owned(
          context_->createGraphicsPath()); // Separate exact inset outline.
      if (path) {
        path->addRoundRect(CRect(box).inset(.5, .5), 3);
        c = color(themeRGB(assemble.selected.count(i) ? ThemeRole::Focus
                                                      : ThemeRole::Border));
        context_->setFrameColor({c.red, c.green, c.blue, c.alpha});
        context_->setLineWidth(1);
        context_->drawGraphicsPath(path, CDrawContext::kPathStroked);
      }
    }
    std::string identity =
        format("BK%03u:P%02u", b.phraseBankId, b.phraseSlot + 1);
    std::string title =
        p && !p->name.empty()
            ? format("%s  ·  %s  ·  %lu ROWS%s", identity, p->name,
                     static_cast<unsigned long>(p->length),
                     b.repeats > 1 ? format(" ×%u", b.repeats) : "")
            : identity + "  ·  MISSING";
    bodyText(title, CRect(box).inset(8, std::max(2., (h - 18) * .5)),
             p ? ThemeRole::TextPrimary : ThemeRole::Danger, 9,
             FontWeight::Medium);
    bodyText(format("%03lu", static_cast<unsigned long>(start + 1)),
             toolRect(x + 10, y + 5, 44, 15), ThemeRole::TextMuted, 8);
    if (auditionWhole_ && auditionRow_ >= 0 &&
        std::size_t(auditionRow_) >= start - state_.assembly.targetRow &&
        std::size_t(auditionRow_) < start - state_.assembly.targetRow + rows)
      fill(toolRect(x + 54,
                    y + double(std::size_t(auditionRow_) -
                               (start - state_.assembly.targetRow)) *
                            6,
                    w - 62, 2),
           themeRGB(ThemeRole::Live), .85);
    y += h;
    start += rows;
  }
  if (blocksDragging_ && dropIndex_ >= 0) {
    double line = canvas_.top - scroll_.y + 28;
    for (int i = 0; i < dropIndex_ && i < int(state_.assembly.blocks.size());
         ++i)
      line +=
          assemblyBlockHeight(state_, state_.assembly.blocks[std::size_t(i)]);
    fill(toolRect(x + 52, line - 2, w - 56, 4), themeRGB(ThemeRole::Focus));
  }
}
} // namespace s3g::tracker::editor
