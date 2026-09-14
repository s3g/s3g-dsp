#pragma once
#include "s3g/tracker/editor_grid_controller.h"
#include "s3g/tracker/editor_grid_painter.h"
#include "s3g/tracker/editor_palette.h"
#include "s3g/tracker/geometry_edit.h"
#include "s3g_gui_layout.h"
#include <optional>

namespace s3g::tracker::editor {
namespace layout = s3g::gui_layout;
using app::TrackerViewState;
using app::WorkspaceCallbacks;

// Geometry and Bursts share one presenter, as in the independent Cocoa editor.
// No AppKit, VSTGUI, host clocks or audio-thread pointers cross this boundary.
enum GeometryMode {
  GeometryModeRingField = 0,
  GeometryModeActivePulses,
  GeometryModeAllStepsUnderlay,
  GeometryModePhaseSpokes,
  GeometryModeLaneFocus,
  GeometryModeCompositeRing,
  GeometryModeBurst,
  GeometryModePitchMap,
};

enum GeometryTool {
  GeometryToolSelect = 0,
  GeometryToolPaint,
  GeometryToolErase,
  GeometryToolVelocity,
};

enum GeometryGesture {
  GeometryGestureNone = 0,
  GeometryGesturePaint,
  GeometryGestureErase,
  GeometryGestureVelocity,
  GeometryGestureDefaultNote,
  GeometryGestureLength,
  GeometryGestureRotate,
  GeometryGestureDensity,
  GeometryGestureBurstPosition,
  GeometryGestureBurstNote,
  GeometryGestureBurstVelocity,
  GeometryGestureBurstGate,
  GeometryGestureBurstMatrixPosition,
  GeometryGestureBurstMatrixNote,
  GeometryGestureBurstMatrixVelocity,
  GeometryGestureBurstMatrixGate,
  GeometryGestureBurstVelocityPoint,
  GeometryGesturePitchMinimum,
  GeometryGesturePitchMaximum,
  GeometryGesturePitchVariation,
  GeometryGesturePitchTranspose,
  GeometryGesturePitchPoint,
};

enum GeometryMenu {
  GeometryMenuNone = 0,
  GeometryMenuLane,
  GeometryMenuDirection,
  GeometryMenuView,
  GeometryMenuMorphTarget,
  GeometryMenuBurstSlot,
  GeometryMenuBurstEvent,
  GeometryMenuPitchScope,
  GeometryMenuPitchRoot,
  GeometryMenuPitchScale,
  GeometryMenuPitchContour,
  GeometryMenuPitchLeap,
  GeometryMenuBurstBank,
  GeometryMenuBurstPreviewChannel,
};

struct GeometryInput {
  Point point;
  unsigned clickCount = 1;
  uint32_t modifiers = 0;
  GridKey key = GridKey::None;
  std::string text;
};
struct GeometryButton {
  int tag = 0;
  bool enabled = true, state = false;
};
struct GeometryMenuState {
  std::vector<std::string> items;
  int indexOfSelectedItem = 0;
  bool enabled = true;
  std::string titleOfSelectedItem() const {
    return items.empty()
               ? std::string{}
               : items[std::clamp<std::size_t>(
                     static_cast<std::size_t>(std::max(indexOfSelectedItem, 0)),
                     0, items.size() - 1)];
  }
};
struct GeometryServices {
  GridPaintServices paint;
  std::function<GridFont(double)> suiteFont;
  std::function<double(std::string_view, const GridFont &)> measure;
  std::function<double(const GridFont &)> capHeight;
  std::function<void()> invalidate, focus, error, revealTracker;
  // An independent audition timer, never the drawing/refresh timer.
  std::function<void(double)> startAuditionTimer;
  std::function<void()> stopAuditionTimer;
  std::function<double()> monotonicTime;
};
struct GeometryPath {
  struct Element {
    Rect rect;
    bool ellipse = false;
    std::vector<Point> points;
  };
  std::vector<Element> elements;
  Rect bounds;
  double lineWidth = 1;
  bool roundCaps = false;
  std::vector<double> dashes;
  static GeometryPath ellipse(Rect);
  static GeometryPath rectangle(Rect);
  void moveToPoint(Point);
  void lineToPoint(Point);
  void closePath();
  void setLineDash(const double *, std::size_t, double);
};
struct GeometryTextStyle {
  Color color;
  GridFont font;
};
struct GeometryStyle {
  Color bg, panel, header, line, text, muted, track, fill, handle;
};
class GeometryEditor {
public:
  using TextStyle = GeometryTextStyle;
  GeometryEditor(TrackerViewState &, WorkspaceCallbacks &, bool bursts,
                 GeometryServices = {});
  ~GeometryEditor();
  GeometryEditor(const GeometryEditor &) = delete;
  GeometryEditor &operator=(const GeometryEditor &) = delete;
  void resize(double, double);
  DisplayList paint();
  void reloadModel();
  void suspend();
  void pointerDown(const GeometryInput &);
  void pointerMove(const GeometryInput &, bool dragging);
  void pointerUp(const GeometryInput &);
  void cancelGesture(bool restore = true);
  void auditionTick();
  bool saveBurstName(std::string);
  void syncToolboxControls();
  void syncBurstNameControls() {}
  std::vector<std::string> itemsForGeometryMenu(GeometryMenu);
  int selectedIndexForGeometryMenu(GeometryMenu);
  void applyGeometryMenuSelection(int);
  bool menuKey(GridKey, bool confirm = false);
  void drawOpenGeometryMenu();
  void lanePopupChanged(const GeometryMenuState &);
  void directionPopupChanged(const GeometryMenuState &);
  void viewModeChanged(const GeometryMenuState &);
  void toolChanged(const GeometryButton &);
  void startBurstPreview();
  void stopBurstPreview();
  void pulseBurstPlaceFeedback();
  void pulseBurstPreviewFeedback();
  void pulsePitchPreviewFeedback();
  std::size_t selectedBurstSlot() const { return _selectedBurstSlot; }
  std::size_t selectedBurstEvent() const { return _selectedBurstEvent; }
  int selectedBurstField() const { return _selectedBurstField; }
  GeometryMenu openMenu() const { return _openGeometryMenu; }
  const PitchMapResult &pitchPreview() const { return _pitchPreview; }
  const PitchMapSettings &pitchSettings() const { return _pitchSettings; }
  double halo(std::size_t lane) const { return _readHeadHaloStrength.at(lane); }
  bool auditionActive() const { return burstPreviewTimer; }
  bool textEnabled() {
    return canEditDisplayedPattern() &&
           !trackerState->session.burstLibrary.bursts[_selectedBurstSlot]
                .empty();
  }
  layout::TrackerGeometryFamilyLayout geometryLayout();
  Rect canvasRect();
  Rect canvasPlotRect();
  Rect inspectorRect();
  Rect laneCyclePanelRect();
  Rect editPanelRect();
  Rect viewPanelRect();
  Rect bridgePanelRect();
  layout::Panel burstAuditionPanelLayout();
  layout::Panel burstPlacementPanelLayout();
  Rect burstPreviewChannelMenuBoxRect();
  Rect geometrySliderTrackForRow(uint32_t row);
  Rect lengthSliderTrack();
  Rect defaultNoteSliderTrack();
  Rect rotateSliderTrack();
  Rect densitySliderTrack();
  Rect sliderHitRect(Rect track);
  Rect laneMenuBoxRect();
  Rect directionMenuBoxRect();
  Rect viewMenuBoxRect();
  Rect morphTargetMenuBoxRect();
  Rect linkVelocityLengthToggleRect();
  Rect burstSlotMenuBoxRect();
  Rect burstBankMenuBoxRect();
  Rect burstEventMenuBoxRect();
  Rect burstNameBoxRect();
  Rect pitchScopeMenuBoxRect();
  Rect pitchRootMenuBoxRect();
  Rect pitchScaleMenuBoxRect();
  Rect pitchMinimumSliderTrack();
  Rect pitchMaximumSliderTrack();
  Rect pitchContourMenuBoxRect();
  Rect pitchLeapMenuBoxRect();
  Rect pitchVariationSliderTrack();
  Rect pitchTransposeSliderTrack();
  Rect pitchInvertToggleRect();
  Rect pitchReverseToggleRect();
  Rect pitchAnchorToggleRect();
  Rect pitchAnalyzeHeaderButtonRect();
  Rect pitchNewSeedHeaderButtonRect();
  Rect pitchPreviewHeaderButtonRect();
  Rect pitchApplyHeaderButtonRect();
  Rect pitchGraphRect();
  Rect pitchIntervalGraphRect();
  bool pitchMapNoteMatchesScale(uint8_t note);
  int pitchScaleOrdinalForNote(int note);
  int pitchScaleNoteForOrdinal(int requestedOrdinal);
  int pitchIntervalAtIndex(std::size_t index, bool original);
  int pitchIntervalExtent();
  int pitchMapDisplayMinimum();
  int pitchMapDisplayMaximum();
  Point pitchMapPointForAssignment(const PitchMapAssignment &assignment);
  Point pitchMapPointForAssignmentAtIndex(std::size_t index, bool original);
  Point pitchMapPointForAssignmentAtIndex(std::size_t index, bool original,
                                          bool interval);
  int pitchMapAssignmentAtPoint(Point point);
  std::string pitchSelectedPointFlagText();
  void updatePitchMapPointAtPoint(Point point);
  Rect burstSliderTrackForRow(uint32_t row);
  Rect burstActionRectForRow(uint32_t row, std::size_t index,
                             std::size_t count);
  Rect editToolButtonRect(std::size_t index);
  Rect reverseButtonRect();
  Rect reflectButtonRect();
  Rect morphAmountButtonRect(std::size_t index);
  Rect revealHeaderButtonRect();
  Rect fitBurstGatesHeaderButtonRect();
  Rect burstPreviewHeaderButtonRect();
  Rect burstLoopHeaderButtonRect();
  Rect burstRenameHeaderButtonRect();
  std::string displayedPatternId();
  std::size_t displayedLaneCount();
  std::size_t displayedMutedLaneCount();
  int directionPopupIndex(Direction direction);
  Rect sourceRectForGeometryMenu(GeometryMenu menu);
  Rect dropdownRectForGeometryMenu(GeometryMenu menu);
  void openGeometryMenu(GeometryMenu menu);
  void selectBurstSlot(std::size_t slot);
  void pitchMapRowsFirst(std::size_t *first, std::size_t *last);
  void refreshPitchMapPreview();
  void freezePitchPreviewForManualEditing();
  void analyzePitchMap();
  void openPitchMapFirstRow(std::size_t firstRow, std::size_t lastRow);
  void applyCurrentPitchMap();
  void applyPitchMapContour(PitchContour contour, std::size_t firstRow,
                            std::size_t lastRow);
  void initializeBurstAtSlot(std::size_t slot);
  std::size_t firstEmptyBurstSlot();
  bool emitBurstPreview();
  bool handleBurstToolboxClickAtPoint(Point point);
  bool handleToolboxClickAtPoint(Point point);
  void mouseMoved(const GeometryInput &event);
  void advancePlaybackAnimation();
  void refreshPlaybackDisplay();
  Rect zoomOutRect();
  Rect zoomResetRect();
  Rect zoomInRect();
  void setGeometryZoomAndRedraw(double value);
  bool canEditDisplayedPattern();
  Track *selectedEditableTrack();
  void commitGeometryChange(bool changed);
  void rotateBack(int sender);
  void rotateForward(int sender);
  void densityDown(int sender);
  void densityUp(int sender);
  void reverse(int sender);
  void reflect(int sender);
  void morphAmount(const GeometryButton &sender);
  void revealInTracker(int sender);
  void selectLane(std::size_t lane, std::size_t row, std::size_t field);
  void selectLane(std::size_t lane);
  Point geometryCenter();
  double geometryMaximumRadius();
  double ringRadiusForOrdinal(std::size_t ordinal, std::size_t count);
  double ringRadiusForLane(std::size_t lane);
  bool selectedRingLane(std::size_t *lane, double *radius);
  double geometryAngleForPoint(Point point);
  Point geometryPointAtRadius(double radius, double angle);
  Point rotateHandlePoint();
  Point densityHandlePoint();
  void prepareGeometryGesture(GeometryGesture kind, std::size_t lane);
  void updateRotationPreviewForTrack(const Track &track);
  void updateDensityPreviewForTrack(const Track &track);
  bool beginSliderGestureAtPoint(Point point);
  void updateSliderGestureAtPoint(Point point);
  bool beginShapeGestureAtPoint(Point point);
  void updateShapeGestureAtPoint(Point point);
  void finishGeometryGesture();
  Rect burstMatrixRect();
  Rect burstOverviewRect();
  Rect burstBreakpointRect();
  Rect burstRadialPlotRect();
  Rect burstMatrixRowRect(std::size_t row);
  Rect burstMatrixCellRect(std::size_t row, int field);
  int burstMatrixRowAtPoint(Point point);
  int burstMatrixFieldAtPoint(Point point, std::size_t row);
  int burstBreakpointEventAtPoint(Point point);
  void updateBurstMatrixGestureAtPoint(Point point);
  bool beginBurstCanvasGestureAtPoint(Point point);
  int burstEventAtPoint(Point point);
  void updateBurstPositionAtPoint(Point point);
  bool geometryCellAtPoint(Point point, std::size_t *lane, std::size_t *row,
                           double *hitRadius);
  bool beadNear(Point point, std::size_t lane, std::size_t row, double radius);
  bool revealBeadAtPoint(Point point);
  void mouseDown(const GeometryInput &event);
  void mouseDragged(const GeometryInput &event);
  void mouseUp(const GeometryInput &event);
  void keyDown(const GeometryInput &event);
  std::size_t allStepsUnderlayNodeCount();
  void drawBurstWorkspace();
  void drawPitchMapWorkspace();
  void drawRect(Rect dirtyRect);
  void drawBurstPlaybackOverlay();
  void drawPlaybackOverlay();

  double geometryZoom = 1;
  GeometryMode geometryViewMode = GeometryModeRingField;
  GeometryTool geometryTool = GeometryToolSelect;
  bool burstLibraryOnly = false, linkVelocityLength = true;
  GeometryMenuState lanePopup, directionPopup, viewModePopup, morphTargetPopup;
  std::array<GeometryButton, 4> toolButtons, morphButtons;
  GeometryButton rotateBackButton, rotateForwardButton, densityDownButton,
      densityUpButton, reverseButton, reflectButton;

private:
  TrackerViewState *trackerState;
  WorkspaceCallbacks *callbacks_;
  GeometryServices services_;
  Rect bounds_;
  DisplayList list_;
  Color fillColor_, strokeColor_;
  double now_ = 0, placeFeedbackUntil_ = 0, burstFeedbackUntil_ = 0,
         pitchFeedbackUntil_ = 0;
  bool burstPreviewTimer = false, pointerActive_ = false, textConsumed_ = false;
  std::string accessibilityValue;
  std::optional<Pattern> gesturePattern_;
  std::optional<BurstLibrary> gestureBursts_;
  std::string gesturePatternId_;
  AssetBankId gestureBankId_ = kProjectAssetBankId;
  PitchMapSettings gesturePitch_;
  std::array<int16_t, 256> gestureOverrides_;

  std::array<double, s3g::tracker::kMaximumTrackCount> _readHeadHaloStrength{};
  std::array<std::size_t, s3g::tracker::kMaximumTrackCount> _readHeadHaloRows{};
  std::array<bool, s3g::tracker::kMaximumTrackCount> _documentationHitLanes{};
  bool _documentationPlaybackSnapshot{};
  double _lastReadHeadAnimationTime{};
  std::string _lastDisplayedPatternId{};
  uint32_t _lastDisplayedSongMuteMask{};
  bool _geometryGestureActive{};
  bool _geometryGestureChanged{};
  int _lastGestureRow{};
  std::size_t _gestureLane{};
  std::size_t _gestureRow{};
  double _velocityStartRadius{};
  float _velocityStartValue{};
  GeometryGesture _geometryGestureKind{};
  bool _geometrySliderGesture{};
  std::size_t _gestureOriginalLength{};
  std::size_t _gesturePreviewLength{};
  uint8_t _gestureOriginalDefaultNote{};
  uint8_t _gesturePreviewDefaultNote{};
  std::size_t _gestureOriginalPhase{};
  std::size_t _gesturePreviewPhase{};
  int _gesturePreviewRotation{};
  std::size_t _gestureOriginalDensity{};
  std::size_t _gesturePreviewDensity{};
  double _gestureLastAngle{};
  double _gestureAccumulatedAngle{};
  std::vector<NoteCell> _gestureOriginalNotes{};
  std::vector<NoteCell> _gesturePreviewNotes{};
  GeometryMenu _openGeometryMenu{};
  int _geometryMenuHoverIndex{};
  std::size_t _selectedBurstSlot{};
  std::size_t _selectedBurstEvent{};
  int _selectedBurstField{};
  Rect _burstGestureRect{};
  bool _burstPlaceFeedbackActive{};
  bool _burstPreviewFeedbackActive{};
  bool _pitchPreviewFeedbackActive{};
  PitchMapSettings _pitchSettings{};
  PitchMapAnalysis _pitchAnalysis{};
  PitchMapResult _pitchPreview{};
  std::array<int16_t, 256u> _pitchOverrides{};
  std::size_t _pitchFirstRow{};
  std::size_t _pitchLastRow{};
  bool _pitchUseFullCycle{};
  bool _pitchEditingIntervals{};
  int _pitchDragAssignment{};
  std::string _pitchStatus{};

  void invalidate();
  void focus();
  void error();
  void publish();
  void selectionChanged();
  void togglePlayback();
  void revealTracker();
  Color literal(uint32_t, double alpha = 1) const;
  Color trackerColor(uint32_t, double alpha = 1) const;
  Color themeColor(ThemeRole, double alpha = 1) const;
  GridFont font(double, FontWeight, bool centered) const;
  GridFont uiFont(double) const;
  TextStyle softLabelAttrs() const;
  TextStyle softValueAttrs() const;
  GeometryStyle softTextStyle() const;
  Point measure(std::string_view, const TextStyle &) const;
  std::string menuDisplayText(std::string, double, const TextStyle &) const;
  void drawAtPoint(std::string, Point, const TextStyle &);
  void drawInRect(std::string, Rect, const TextStyle &);
  void drawText(std::string, Rect, Color, double,
                FontWeight = FontWeight::Regular, Alignment = Alignment::Left);
  void drawCenteredText(std::string, Rect, Color, double,
                        FontWeight = FontWeight::Regular,
                        Alignment = Alignment::Center);
  void fillRect(Rect, Color);
  void strokeRect(Rect, Color, double width = 1);
  void fillCurrentRect(Rect r) { fillRect(r, fillColor_); }
  void strokeCurrentRect(Rect r) {
    strokeRect({r.x + .5, r.y + .5, r.width - 1, r.height - 1}, fillColor_);
  }
  void fillPath(const GeometryPath &);
  void strokePath(const GeometryPath &);
  void drawGeometryReadHead(Point, double, bool, bool);
  void drawPanelFrame(double, double, double, double, const GeometryStyle &);
  void drawPanelFrame(const layout::Panel &, const GeometryStyle &);
  void drawPanelHeader(std::string, bool, double, double, double, double,
                       const TextStyle &, const GeometryStyle &);
  void drawPanelHeader(std::string, bool, const layout::Panel &,
                       const TextStyle &, const GeometryStyle &);
  void drawToolboxHeaderButton(Rect, Rect, std::string, bool, const TextStyle &,
                               const GeometryStyle &);
  void drawToolboxHeaderActionButton(Rect, Rect, std::string, const TextStyle &,
                                     const GeometryStyle &);
  void S3GTrackerDrawSuiteActionButton(Rect, std::string, bool enabled,
                                       bool pressed, bool hovered, bool live,
                                       bool positive, bool binaryOff,
                                       bool danger, bool neutralTitle);
  void drawTrackerProcessorMenu(std::string, std::string, double, double,
                                double, const TextStyle &, const TextStyle &,
                                const GeometryStyle &);
  void drawProcessorSliderWithValueWidth(std::string, std::string, double,
                                         double, double, double, double,
                                         const TextStyle &, const TextStyle &,
                                         const GeometryStyle &);
  void drawProcessorSlider(std::string, std::string, double, double, double,
                           double, const TextStyle &, const TextStyle &,
                           const GeometryStyle &);
  void drawProcessorToggle(std::string, bool, double, double, double,
                           const TextStyle &, const TextStyle &,
                           const GeometryStyle &);
};
} // namespace s3g::tracker::editor
