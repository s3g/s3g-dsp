#include "s3g/tracker/editor_geometry.h"
#include "editor_geometry_support.h"
#include "s3g/tracker/editor_reference.h"
#include <chrono>

namespace s3g::tracker::editor {
using namespace geometry_support;
GeometryEditor::GeometryEditor(TrackerViewState &state,
                               WorkspaceCallbacks &callbacks, bool bursts,
                               GeometryServices services)
    : burstLibraryOnly(bursts), trackerState(&state), callbacks_(&callbacks),
      services_(std::move(services)) {
  geometryViewMode = bursts ? GeometryModeBurst : GeometryModeRingField;
  directionPopup.items = {"FORWARD  >", "REVERSE  <", "PALINDROME  <>",
                          "RANDOM"};
  viewModePopup.items = {"RING FIELD",   "ACTIVE PULSES", "ALL STEPS UNDERLAY",
                         "PHASE SPOKES", "LANE FOCUS",    "COMPOSITE RING",
                         "BURST EDITOR", "PITCH MAP"};
  viewModePopup.indexOfSelectedItem = geometryViewMode;
  morphTargetPopup.items = {"PREVIOUS LANE", "NEXT LANE"};
  morphTargetPopup.indexOfSelectedItem = 1;
  for (std::size_t i = 0; i < 4; ++i) {
    toolButtons[i].tag = static_cast<int>(i);
    toolButtons[i].state = i == 0;
    morphButtons[i].tag = static_cast<int>((i + 1) * 25);
  }
  _selectedBurstField = 1;
  _pitchSettings.scale = 2;
  _pitchSettings.contour = PitchContour::VaryExisting;
  _pitchOverrides.fill(-1);
  _pitchLastRow = 63;
  _pitchUseFullCycle = true;
  _pitchDragAssignment = -1;
  _pitchStatus = "READY TO PREVIEW";
  _lastGestureRow = _geometryMenuHoverIndex = -1;
  if (!services_.monotonicTime)
    services_.monotonicTime = [] {
      return std::chrono::duration<double>(
                 std::chrono::steady_clock::now().time_since_epoch())
          .count();
    };
  now_ = services_.monotonicTime();
  resize(1320, 820);
}
GeometryEditor::~GeometryEditor() { stopBurstPreview(); }
void GeometryEditor::invalidate() {
  if (services_.invalidate)
    services_.invalidate();
}
void GeometryEditor::focus() {
  if (services_.focus)
    services_.focus();
}
void GeometryEditor::error() {
  if (services_.error)
    services_.error();
}
void GeometryEditor::publish() {
  if (callbacks_->patternChanged)
    callbacks_->patternChanged();
}
void GeometryEditor::selectionChanged() {
  if (callbacks_->selectionChanged)
    callbacks_->selectionChanged();
}
void GeometryEditor::togglePlayback() {
  if (callbacks_->togglePlayback)
    callbacks_->togglePlayback();
}
void GeometryEditor::revealTracker() {
  if (services_.revealTracker)
    services_.revealTracker();
  else if (callbacks_->showTrackerPage)
    callbacks_->showTrackerPage();
}
void GeometryEditor::resize(double width, double height) {
  bounds_ = {0, 0, std::max(1., width), std::max(1., height)};
  invalidate();
}
void GeometryEditor::reloadModel() {
  // The coordinator may have restored another document. Never write a stale
  // gesture snapshot into that document or publish a cancelled edit.
  cancelGesture(false);
  stopBurstPreview();
  _openGeometryMenu = GeometryMenuNone;
  _pitchOverrides.fill(-1);
  _pitchDragAssignment = -1;
  syncToolboxControls();
  invalidate();
}
void GeometryEditor::suspend() {
  cancelGesture();
  stopBurstPreview();
  _openGeometryMenu = GeometryMenuNone;
  _geometryMenuHoverIndex = -1;
  _burstPlaceFeedbackActive = _pitchPreviewFeedbackActive = false;
}
void GeometryEditor::pointerDown(const GeometryInput &event) {
  cancelGesture();
  gesturePattern_ = trackerState->session.pattern;
  gestureBursts_ = trackerState->session.burstLibrary;
  gesturePatternId_ = trackerState->patternBank.activePatternId;
  gestureBankId_ = trackerState->activeBurstBankId;
  gesturePitch_ = _pitchSettings;
  gestureOverrides_ = _pitchOverrides;
  pointerActive_ = true;
  mouseDown(event);
  if (!_geometryGestureActive) {
    gesturePattern_.reset();
    gestureBursts_.reset();
    pointerActive_ = false;
  }
}
void GeometryEditor::pointerMove(const GeometryInput &event, bool dragging) {
  if (dragging) {
    if (trackerState->patternBank.activePatternId != gesturePatternId_ ||
        trackerState->activeBurstBankId != gestureBankId_ ||
        !canEditDisplayedPattern()) {
      cancelGesture();
      return;
    }
    mouseDragged(event);
  } else
    mouseMoved(event);
}
void GeometryEditor::pointerUp(const GeometryInput &event) {
  if (pointerActive_ &&
      (trackerState->patternBank.activePatternId != gesturePatternId_ ||
       trackerState->activeBurstBankId != gestureBankId_ ||
       !canEditDisplayedPattern())) {
    cancelGesture();
    return;
  }
  mouseUp(event);
  gesturePattern_.reset();
  gestureBursts_.reset();
  pointerActive_ = false;
}
void GeometryEditor::cancelGesture(bool restore) {
  if (restore && pointerActive_ && _geometryGestureActive &&
      trackerState->patternBank.activePatternId == gesturePatternId_ &&
      trackerState->activeBurstBankId == gestureBankId_) {
    if (gesturePattern_)
      trackerState->session.pattern = *gesturePattern_;
    if (gestureBursts_)
      trackerState->session.burstLibrary = *gestureBursts_;
    _pitchSettings = gesturePitch_;
    _pitchOverrides = gestureOverrides_;
  }
  pointerActive_ = _geometryGestureActive = _geometryGestureChanged =
      _geometrySliderGesture = false;
  _geometryGestureKind = GeometryGestureNone;
  _pitchDragAssignment = -1;
  _gestureOriginalNotes.clear();
  _gesturePreviewNotes.clear();
  gesturePattern_.reset();
  gestureBursts_.reset();
  invalidate();
}
void GeometryEditor::syncToolboxControls() {
  const auto *pattern = geometryPattern(trackerState);
  const auto lanes = visibleGeometryLanes(trackerState);
  lanePopup.items.clear();
  lanePopup.indexOfSelectedItem = 0;
  std::size_t selectedLane = lanes.count ? lanes.indices[0] : 0;
  for (std::size_t i = 0; i < lanes.count; ++i) {
    const auto lane = lanes.indices[i];
    lanePopup.items.push_back(format("%02lu  %s",
                                     static_cast<unsigned long>(lane + 1),
                                     pattern->tracks[lane].name));
    if (lane == trackerState->session.selectedTrack) {
      lanePopup.indexOfSelectedItem = static_cast<int>(i);
      selectedLane = lane;
    }
  }
  if (pattern && selectedLane < pattern->tracks.size())
    directionPopup.indexOfSelectedItem =
        directionPopupIndex(pattern->tracks[selectedLane].noteColumn.direction);
  lanePopup.enabled = directionPopup.enabled =
      canEditDisplayedPattern() && lanes.count;
  morphTargetPopup.enabled = canEditDisplayedPattern() && lanes.count > 1;
  for (std::size_t i = 0; i < toolButtons.size(); ++i)
    toolButtons[i].enabled =
        i == 0 || (canEditDisplayedPattern() &&
                   geometryViewMode == GeometryModeRingField);
  viewModePopup.indexOfSelectedItem = geometryViewMode;
  auto &burst = trackerState->session.burstLibrary.bursts[_selectedBurstSlot];
  _selectedBurstEvent =
      burst.empty()
          ? 0
          : std::min<std::size_t>(_selectedBurstEvent, burst.eventCount - 1);
}
std::vector<std::string>
GeometryEditor::itemsForGeometryMenu(GeometryMenu menu) {
  switch (menu) {
  case GeometryMenuView:
    return burstLibraryOnly
               ? std::vector<std::string>{}
               : std::vector<std::string>{
                     "RING FIELD",   "ACTIVE PULSES", "ALL STEPS UNDERLAY",
                     "PHASE SPOKES", "LANE FOCUS",    "COMPOSITE RING",
                     "PITCH MAP"};
  case GeometryMenuLane:
    return lanePopup.items;
  case GeometryMenuDirection:
    return directionPopup.items;
  case GeometryMenuMorphTarget:
    return morphTargetPopup.items;
  case GeometryMenuPitchScope:
    return {"SELECTED ROWS", "FULL NOTE CYCLE"};
  case GeometryMenuPitchRoot:
    return {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  case GeometryMenuPitchContour:
    return {"FIT",         "RISE",          "FALL",  "PENDULUM",
            "RANDOM WALK", "VARY EXISTING", "MANUAL"};
  case GeometryMenuPitchLeap:
    return {"1 DEGREE",  "2 DEGREES", "3 DEGREES", "4 DEGREES",
            "5 DEGREES", "6 DEGREES", "7 DEGREES", "8 DEGREES"};
  default:
    break;
  }
  std::vector<std::string> result;
  if (menu == GeometryMenuPitchScale)
    for (uint32_t i = 0; i < s3g::kMusicalScaleCount; ++i)
      result.push_back(
          s3g::musicalScaleDefinition(s3g::musicalScaleValueForMenuIndex(i))
              .name);
  if (menu == GeometryMenuBurstBank)
    for (const auto &bank : trackerState->burstBanks)
      result.push_back(bank.name.empty() ? "UNTITLED BANK" : bank.name);
  if (menu == GeometryMenuBurstSlot)
    for (std::size_t i = 0; i < kBurstDefinitionCount; ++i) {
      const auto &burst = trackerState->session.burstLibrary.bursts[i];
      result.push_back(burstSlotToken(i) + "  ·  " +
                       (burst.empty() ? "EMPTY" : burst.name));
    }
  if (menu == GeometryMenuBurstEvent) {
    const auto &burst =
        trackerState->session.burstLibrary.bursts[_selectedBurstSlot];
    for (std::size_t i = 0; i < burst.eventCount; ++i)
      result.push_back(format("STEP %lu  ·  %s · %03u",
                              static_cast<unsigned long>(i + 1),
                              midiNoteName(burst.events[i].note),
                              static_cast<unsigned>(burst.events[i].note)));
  }
  if (menu == GeometryMenuBurstPreviewChannel)
    for (unsigned i = 1; i <= 16; ++i)
      result.push_back(format("%02u", i));
  return result;
}
int GeometryEditor::selectedIndexForGeometryMenu(GeometryMenu menu) {
  switch (menu) {
  case GeometryMenuView:
    return geometryViewMode == GeometryModePitchMap ? 6 : geometryViewMode;
  case GeometryMenuLane:
    return lanePopup.indexOfSelectedItem;
  case GeometryMenuDirection:
    return directionPopup.indexOfSelectedItem;
  case GeometryMenuMorphTarget:
    return morphTargetPopup.indexOfSelectedItem;
  case GeometryMenuPitchScope:
    return _pitchUseFullCycle ? 1 : 0;
  case GeometryMenuPitchRoot:
    return _pitchSettings.rootPitchClass % 12;
  case GeometryMenuPitchScale:
    return static_cast<int>(
        s3g::musicalScaleMenuIndexForValue(_pitchSettings.scale));
  case GeometryMenuPitchContour:
    return static_cast<int>(_pitchSettings.contour);
  case GeometryMenuPitchLeap:
    return static_cast<int>(
               std::clamp<unsigned>(_pitchSettings.maximumLeapDegrees, 1, 8)) -
           1;
  case GeometryMenuBurstSlot:
    return static_cast<int>(_selectedBurstSlot);
  case GeometryMenuBurstEvent:
    return static_cast<int>(_selectedBurstEvent);
  case GeometryMenuBurstPreviewChannel:
    return static_cast<int>(std::clamp<unsigned>(
               trackerState->burstPreviewMidiChannel, 1, 16)) -
           1;
  case GeometryMenuBurstBank:
    for (std::size_t i = 0; i < trackerState->burstBanks.size(); ++i)
      if (trackerState->burstBanks[i].id == trackerState->activeBurstBankId)
        return static_cast<int>(i);
    break;
  default:
    break;
  }
  return 0;
}
void GeometryEditor::applyGeometryMenuSelection(int index) {
  if (index < 0 ||
      index >= static_cast<int>(itemsForGeometryMenu(_openGeometryMenu).size()))
    return;
  switch (_openGeometryMenu) {
  case GeometryMenuLane:
    lanePopup.indexOfSelectedItem = index;
    lanePopupChanged(lanePopup);
    break;
  case GeometryMenuDirection:
    directionPopup.indexOfSelectedItem = index;
    directionPopupChanged(directionPopup);
    break;
  case GeometryMenuMorphTarget:
    morphTargetPopup.indexOfSelectedItem = index;
    break;
  case GeometryMenuView:
    viewModePopup.indexOfSelectedItem = index >= 6 ? index + 1 : index;
    viewModeChanged(viewModePopup);
    break;
  case GeometryMenuBurstSlot:
    stopBurstPreview();
    _selectedBurstSlot = static_cast<std::size_t>(index);
    _selectedBurstEvent = 0;
    break;
  case GeometryMenuBurstBank: {
    stopBurstPreview();
    const auto bank =
        trackerState->burstBanks[static_cast<std::size_t>(index)].id;
    _selectedBurstSlot = _selectedBurstEvent = 0;
    if (callbacks_->selectBurstBank)
      callbacks_->selectBurstBank(bank);
    break;
  }
  case GeometryMenuBurstEvent:
    _selectedBurstEvent = static_cast<std::size_t>(index);
    break;
  case GeometryMenuBurstPreviewChannel:
    trackerState->burstPreviewMidiChannel = static_cast<uint8_t>(index + 1);
    break;
  case GeometryMenuPitchScope:
    _pitchUseFullCycle = index == 1;
    _pitchOverrides.fill(-1);
    break;
  case GeometryMenuPitchRoot:
    _pitchSettings.rootPitchClass = static_cast<uint8_t>(index);
    _pitchOverrides.fill(-1);
    break;
  case GeometryMenuPitchScale:
    _pitchSettings.scale =
        s3g::musicalScaleValueForMenuIndex(static_cast<uint32_t>(index));
    _pitchOverrides.fill(-1);
    break;
  case GeometryMenuPitchContour:
    _pitchSettings.contour = static_cast<PitchContour>(index);
    _pitchOverrides.fill(-1);
    break;
  case GeometryMenuPitchLeap:
    _pitchSettings.maximumLeapDegrees = static_cast<uint8_t>(index + 1);
    _pitchOverrides.fill(-1);
    break;
  default:
    break;
  }
  _openGeometryMenu = GeometryMenuNone;
  _geometryMenuHoverIndex = -1;
  invalidate();
}
void GeometryEditor::lanePopupChanged(const GeometryMenuState &sender) {
  if (!canEditDisplayedPattern())
    return;
  const auto lanes = visibleGeometryLanes(trackerState);
  if (sender.indexOfSelectedItem < 0 ||
      static_cast<std::size_t>(sender.indexOfSelectedItem) >= lanes.count)
    return;
  selectLane(
      lanes.indices[static_cast<std::size_t>(sender.indexOfSelectedItem)]);
  if (geometryViewMode == GeometryModePitchMap) {
    _pitchOverrides.fill(-1);
    _pitchStatus = "LANE CHANGED · ANALYZE OR PREVIEW";
  }
  invalidate();
}
void GeometryEditor::directionPopupChanged(const GeometryMenuState &sender) {
  auto *track = selectedEditableTrack();
  if (!track)
    return;
  constexpr Direction directions[]{Direction::Forward, Direction::Reverse,
                                   Direction::Palindrome, Direction::Random};
  const auto direction =
      directions[std::clamp(sender.indexOfSelectedItem, 0, 3)];
  const bool changed = track->noteColumn.direction != direction;
  track->noteColumn.direction = direction;
  commitGeometryChange(changed);
  selectionChanged();
  invalidate();
}
void GeometryEditor::viewModeChanged(const GeometryMenuState &sender) {
  int requested = std::clamp(sender.indexOfSelectedItem, 0, 7);
  if (burstLibraryOnly)
    requested = GeometryModeBurst;
  else if (requested == GeometryModeBurst)
    requested = GeometryModeRingField;
  cancelGesture();
  stopBurstPreview();
  geometryViewMode = static_cast<GeometryMode>(requested);
  viewModePopup.indexOfSelectedItem = requested;
  if (geometryViewMode != GeometryModeRingField)
    toolChanged(toolButtons[0]);
  invalidate();
}
void GeometryEditor::toolChanged(const GeometryButton &sender) {
  geometryTool = static_cast<GeometryTool>(std::clamp(sender.tag, 0, 3));
  for (std::size_t i = 0; i < toolButtons.size(); ++i)
    toolButtons[i].state = static_cast<int>(i) == geometryTool;
  invalidate();
}
bool GeometryEditor::saveBurstName(std::string value) {
  if (!textEnabled())
    return false;
  const auto unicode = referenceUnicode(value);
  if (referenceUtf8(unicode) != value) {
    error();
    return false;
  }
  const auto whitespace = [](char32_t c) {
    return (c >= 9 && c <= 13) || c == 32 || c == 0x85 || c == 0xa0 ||
           c == 0x1680 || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 ||
           c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
  };
  const auto first =
      std::find_if_not(unicode.begin(), unicode.end(), whitespace);
  const auto last =
      std::find_if_not(unicode.rbegin(), unicode.rend(), whitespace).base();
  value = first >= last ? "BURST " + burstSlotToken(_selectedBurstSlot)
                        : referenceUtf8(std::u32string_view(
                              &*first, static_cast<std::size_t>(last - first)));
  if (value.size() > kMaximumBurstNameBytes) {
    error();
    return false;
  }
  auto &burst = trackerState->session.burstLibrary.bursts[_selectedBurstSlot];
  if (value != burst.name) {
    burst.name = std::move(value);
    publish();
  }
  invalidate();
  return true;
}
bool GeometryEditor::menuKey(GridKey key, bool confirm) {
  if (_openGeometryMenu == GeometryMenuNone)
    return false;
  const int count =
      static_cast<int>(itemsForGeometryMenu(_openGeometryMenu).size());
  if (count == 0)
    return true;
  const int columns = _openGeometryMenu == GeometryMenuPitchScale
                          ? 4
                          : _openGeometryMenu == GeometryMenuBurstSlot ? 2 : 1;
  const int rows = (count + columns - 1) / columns;
  if (_geometryMenuHoverIndex < 0)
    _geometryMenuHoverIndex = selectedIndexForGeometryMenu(_openGeometryMenu);
  if (confirm)
    applyGeometryMenuSelection(_geometryMenuHoverIndex);
  else {
    const int delta = key == GridKey::Up
                          ? -1
                          : key == GridKey::Down
                                ? 1
                                : key == GridKey::Left
                                      ? -rows
                                      : key == GridKey::Right ? rows : 0;
    _geometryMenuHoverIndex =
        std::clamp(_geometryMenuHoverIndex + delta, 0, count - 1);
  }
  invalidate();
  return true;
}
void GeometryEditor::stopBurstPreview() {
  if (burstPreviewTimer && services_.stopAuditionTimer)
    services_.stopAuditionTimer();
  burstPreviewTimer = false;
  _burstPreviewFeedbackActive = false;
  invalidate();
}
void GeometryEditor::startBurstPreview() {
  stopBurstPreview();
  if (!emitBurstPreview()) {
    error();
    return;
  }
  if (!trackerState->burstLoopPreview) {
    pulseBurstPreviewFeedback();
    return;
  }
  const double bpm = trackerState->hostBpm > 0
                         ? trackerState->hostBpm
                         : trackerState->session.transport.bpm;
  const double seconds =
      60. / (std::max(1., bpm) *
             std::clamp<unsigned>(trackerState->session.transport.ticksPerBeat,
                                  1, 96));
  if (services_.startAuditionTimer) {
    burstPreviewTimer = true;
    _burstPreviewFeedbackActive = true;
    services_.startAuditionTimer(seconds);
  }
  invalidate();
}
void GeometryEditor::auditionTick() {
  if (!burstPreviewTimer)
    return;
  if (trackerState->playing || !trackerState->burstLoopPreview ||
      !emitBurstPreview())
    stopBurstPreview();
}
void GeometryEditor::pulseBurstPlaceFeedback() {
  placeFeedbackUntil_ = services_.monotonicTime() + .18;
  _burstPlaceFeedbackActive = true;
  invalidate();
}
void GeometryEditor::pulseBurstPreviewFeedback() {
  burstFeedbackUntil_ = services_.monotonicTime() + .18;
  _burstPreviewFeedbackActive = true;
  invalidate();
}
void GeometryEditor::pulsePitchPreviewFeedback() {
  pitchFeedbackUntil_ = services_.monotonicTime() + .18;
  _pitchPreviewFeedbackActive = true;
  invalidate();
}
DisplayList GeometryEditor::paint() {
  list_ = {};
  syncToolboxControls();
  drawRect(bounds_);
  // Overlay first, menu last: an open menu must not acquire playback marks.
  drawPlaybackOverlay();
  drawOpenGeometryMenu();
  return std::move(list_);
}
} // namespace s3g::tracker::editor
