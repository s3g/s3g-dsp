#pragma once

#include "s3g/tracker/instrument_rack.h"
#include "s3g/tracker/timing_warp.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace s3g::tracker {

// Thirty-two tracks x (retrigger off/on + two SEQ actions), plus bounded CC
// interpolation, fit in the 4096 events returned by one process call. Timing
// FX can retain up to fifteen onsets per lane across future ticks; the separate
// 8192-event pending heap bounds that expansion. More extreme density fails
// closed with explicit telemetry rather than silently diverging audio and MIDI
// output.
constexpr std::size_t kMaximumTrackCount = 32u;
constexpr std::size_t kBurstDefinitionCount = 32u;
constexpr std::size_t kMaximumBurstEvents = 8u;
constexpr std::size_t kMaximumBurstNameBytes = 64u;
constexpr uint8_t kNoBurstDefinition = 0xffu;
constexpr std::size_t kMaximumScheduledEventsPerBlock = 4096u;
constexpr std::size_t kMaximumPendingScheduledEvents = 8192u;
constexpr std::size_t kMaximumCcInterpolationEventsPerTick = 128u;
constexpr double kMaximumCcInterpolationRateHz = 200.0;
constexpr std::size_t kMaximumNoteVoices = 8u;

enum class NoteCellState : uint8_t {
    Rest,
    RetriggerPrevious,
    Kill,
    Hold,
    Note,
    Burst,
};

struct NoteCell {
    NoteCellState state = NoteCellState::Rest;
    uint8_t note = 0u;
    uint8_t voiceCount = 0u;
    std::array<uint8_t, kMaximumNoteVoices - 1u> additionalNotes {};

    static NoteCell rest() { return {}; }

    static NoteCell retriggerPrevious()
    {
        NoteCell cell;
        cell.state = NoteCellState::RetriggerPrevious;
        return cell;
    }

    static NoteCell kill()
    {
        NoteCell cell;
        cell.state = NoteCellState::Kill;
        return cell;
    }

    static NoteCell hold()
    {
        NoteCell cell;
        cell.state = NoteCellState::Hold;
        return cell;
    }

    static NoteCell withNote(uint8_t newNote)
    {
        NoteCell cell;
        cell.state = NoteCellState::Note;
        cell.note = newNote;
        cell.voiceCount = 1u;
        return cell;
    }

    static NoteCell withNotes(
        const std::array<uint8_t, kMaximumNoteVoices>& voices,
        std::size_t count)
    {
        NoteCell cell;
        cell.state = NoteCellState::Note;
        count = std::clamp<std::size_t>(count, 1u, kMaximumNoteVoices);
        cell.note = voices[0u];
        cell.voiceCount = static_cast<uint8_t>(count);
        for (std::size_t voice = 1u; voice < count; ++voice)
            cell.additionalNotes[voice - 1u] = voices[voice];
        return cell;
    }

    std::size_t noteVoiceCount() const noexcept
    {
        if (state != NoteCellState::Note) return 0u;
        return std::clamp<std::size_t>(voiceCount == 0u ? 1u : voiceCount,
            1u, kMaximumNoteVoices);
    }

    uint8_t noteVoice(std::size_t voice) const noexcept
    {
        return voice == 0u ? note : additionalNotes[std::min<std::size_t>(
            voice - 1u, additionalNotes.size() - 1u)];
    }

    static NoteCell withBurst(uint8_t definition)
    {
        NoteCell cell;
        cell.state = NoteCellState::Burst;
        cell.note = definition;
        return cell;
    }
};

// One reusable sub-row MIDI phrase. Position spans the current logical tick:
// 0 is its leading edge and 65535 is immediately before the next tick. Burst
// events use absolute MIDI pitches so they can directly address sampler slices
// and drum maps without borrowing a lane's pitch memory.
struct BurstEvent {
    uint16_t position = 0u;
    uint8_t note = 60u;
    uint8_t velocity = 127u;
    uint8_t gatePercent = 70u;
};

struct BurstDefinition {
    std::string name;
    std::array<BurstEvent, kMaximumBurstEvents> events {};
    uint8_t eventCount = 0u;

    bool empty() const noexcept { return eventCount == 0u; }
};

std::string burstSlotToken(std::size_t index);
bool parseBurstSlot(std::string_view text, std::size_t& index) noexcept;
// Set each event's gate to the distance to the next event; the final event
// ends at the primary Tracker-row boundary. Percent storage is rounded to the
// nearest legal 1..100 value.
void fitBurstGatesToRow(BurstDefinition& burst) noexcept;

// Decimal MIDI (0..127) and readable note names share one entry contract.
// Natural names accept both tracker form (C-3) and compact form (C3);
// sharps and flats accept C#3 and Db3.
bool parseMidiNote(std::string_view text, uint8_t& note) noexcept;

enum class ValueCellState : uint8_t {
    Default,
    Previous,
    Value,
};

struct ValueCell {
    ValueCellState state = ValueCellState::Default;
    float normalized = 0.0f;
    uint8_t voiceCount = 0u;
    std::array<float, kMaximumNoteVoices - 1u> additionalValues {};

    static ValueCell defaultValue() { return {}; }

    static ValueCell previous()
    {
        ValueCell cell;
        cell.state = ValueCellState::Previous;
        return cell;
    }

    static ValueCell withValue(float newValue)
    {
        ValueCell cell;
        cell.state = ValueCellState::Value;
        cell.normalized = newValue;
        cell.voiceCount = 1u;
        return cell;
    }

    static ValueCell withValues(
        const std::array<float, kMaximumNoteVoices>& voices,
        std::size_t count)
    {
        ValueCell cell;
        cell.state = ValueCellState::Value;
        count = std::clamp<std::size_t>(count, 1u, kMaximumNoteVoices);
        cell.normalized = voices[0u];
        cell.voiceCount = static_cast<uint8_t>(count);
        for (std::size_t voice = 1u; voice < count; ++voice)
            cell.additionalValues[voice - 1u] = voices[voice];
        return cell;
    }

    std::size_t valueVoiceCount() const noexcept
    {
        if (state != ValueCellState::Value) return 0u;
        return std::clamp<std::size_t>(voiceCount == 0u ? 1u : voiceCount,
            1u, kMaximumNoteVoices);
    }

    float valueVoice(std::size_t voice) const noexcept
    {
        return voice == 0u ? normalized
            : additionalValues[std::min<std::size_t>(
                voice - 1u, additionalValues.size() - 1u)];
    }
};

// Instrument is an independently advancing memory column. Empty leaves the
// remembered rack node untouched without making an explicit recall gesture;
// Previous also retains it, but records that intent in the authored pattern.
// An Instrument cell replaces the memory before same-tick FX and notes resolve
// their lane-relative target.
enum class InstrumentCellState : uint8_t {
    Empty,
    Previous,
    Instrument,
};

struct InstrumentCell {
    InstrumentCellState state = InstrumentCellState::Empty;
    uint32_t nodeId = kInvalidInstrumentNode;

    static InstrumentCell empty() { return {}; }

    static InstrumentCell previous()
    {
        InstrumentCell cell;
        cell.state = InstrumentCellState::Previous;
        return cell;
    }

    static InstrumentCell withInstrument(uint32_t newNodeId)
    {
        InstrumentCell cell;
        cell.state = InstrumentCellState::Instrument;
        cell.nodeId = newNodeId;
        return cell;
    }
};

enum class Direction : uint8_t {
    Forward,
    Reverse,
    Random,
    Palindrome,
};

enum class ParameterScope : uint8_t {
    Global,
    Channel,
    Note,
};

enum class ValueInterpolation : uint8_t {
    Step,
    Linear,
};

// Destination-neutral operations that reshape note production before the
// canonical audio/MIDI event boundary. Their paired V1/V2 values remain
// normalized; the action catalog defines the musical interpretation.
enum class SequencerAction : uint8_t {
    Ratchet,
    MicroTime,
    Delay,
    Flam,
    Stutter,
    Accent,
    Ghost,
    Probability,
    Skip,
    Offset,
    RepeatPrevious,
    Euclid,
    Condition,
    Energy,
    Count,
};

constexpr std::size_t kSequencerActionCount = static_cast<std::size_t>(
    SequencerAction::Count);

// CD is deliberately a small discrete vocabulary stored in the existing
// normalized V column. The stable index mapping keeps project files and the
// polymetric action/value memory model unchanged while the UI can present the
// value as a musical menu instead of a misleading continuous number.
enum class SequencerCondition : uint8_t {
    FirstOf2,
    SecondOf2,
    FirstOf4,
    SecondOf4,
    ThirdOf4,
    FourthOf4,
    FirstOf8,
    SecondOf8,
    ThirdOf8,
    FourthOf8,
    FifthOf8,
    SixthOf8,
    SeventhOf8,
    EighthOf8,
    First,
    Last,
    Fill,
    NotFill,
    SongFirst,
    SongLast,
    RowOdd,
    RowEven,
    SongFirstOf2,
    SongSecondOf2,
    SongFirstOf4,
    SongSecondOf4,
    SongThirdOf4,
    SongFourthOf4,
    SongFirstOf8,
    SongSecondOf8,
    SongThirdOf8,
    SongFourthOf8,
    SongFifthOf8,
    SongSixthOf8,
    SongSeventhOf8,
    SongEighthOf8,
    Count,
};

constexpr std::size_t kSequencerConditionCount = static_cast<std::size_t>(
    SequencerCondition::Count);

struct SequencerConditionDefinition {
    SequencerCondition condition = SequencerCondition::FirstOf2;
    std::string_view token;
    std::string_view displayName;
};

struct SequencerConditionContext {
    // Zero-based visit to the current pattern cycle or Song row repetition.
    uint64_t passIndex = 0u;
    // Zero means the total is unknown. Song rows publish their authored
    // repeat count, which makes LAST exact; free-running patterns leave it 0.
    uint64_t passCount = 0u;
    bool fill = false;
    bool songActive = false;
    std::size_t songRowIndex = 0u;
    std::size_t songRowCount = 0u;
    uint64_t songLoopPassIndex = 0u;
    float songEnergy = 1.0f;
};

const SequencerConditionDefinition* sequencerCondition(
    std::size_t index) noexcept;
const SequencerConditionDefinition* findSequencerCondition(
    std::string_view token) noexcept;
SequencerCondition sequencerConditionFromNormalized(float value) noexcept;
float normalizedFromSequencerCondition(SequencerCondition condition) noexcept;
bool sequencerConditionPasses(SequencerCondition condition,
    const SequencerConditionContext& context) noexcept;

enum class FxActionCellState : uint8_t {
    Empty,
    Previous,
    Parameter,
    Sequencer,
    MidiControlChange,
};

struct FxActionCell {
    FxActionCellState state = FxActionCellState::Empty;
    uint32_t targetNode = kTrackInstrumentNode;
    uint32_t parameterId = 0u;
    ParameterScope scope = ParameterScope::Global;
    SequencerAction sequencerAction = SequencerAction::Ratchet;
    uint8_t midiController = 0u;

    static FxActionCell empty() { return {}; }

    static FxActionCell previous()
    {
        FxActionCell cell;
        cell.state = FxActionCellState::Previous;
        return cell;
    }

    static FxActionCell parameter(uint32_t parameterId,
        ParameterScope scope = ParameterScope::Global,
        uint32_t targetNode = kTrackInstrumentNode)
    {
        FxActionCell cell;
        cell.state = FxActionCellState::Parameter;
        cell.targetNode = targetNode;
        cell.parameterId = parameterId;
        cell.scope = scope;
        return cell;
    }

    static FxActionCell sequencer(SequencerAction action)
    {
        FxActionCell cell;
        cell.state = FxActionCellState::Sequencer;
        cell.sequencerAction = action;
        return cell;
    }

    static FxActionCell midiControlChange(uint8_t controller)
    {
        FxActionCell cell;
        cell.state = FxActionCellState::MidiControlChange;
        cell.midiController = controller;
        return cell;
    }
};

enum class FxValueCellState : uint8_t {
    Previous,
    Value,
};

struct FxValueCell {
    FxValueCellState state = FxValueCellState::Previous;
    float normalized = 0.0f;

    static FxValueCell previous() { return {}; }

    static FxValueCell withValue(float newValue)
    {
        FxValueCell cell;
        cell.state = FxValueCellState::Value;
        cell.normalized = newValue;
        return cell;
    }
};

// Routing is part of the scheduled musical event, not an assumption made by
// an output adapter. Values are bit-compatible so adapters can cheaply test a
// destination without allocating or consulting project state.
enum class EventDestination : uint8_t {
    None = 0u,
    Internal = 1u << 0u,
    Midi = 1u << 1u,
    Both = (1u << 0u) | (1u << 1u),
};

constexpr bool routesToInternal(EventDestination destination) noexcept
{
    return (static_cast<uint8_t>(destination)
        & static_cast<uint8_t>(EventDestination::Internal)) != 0u;
}

constexpr bool routesToMidi(EventDestination destination) noexcept
{
    return (static_cast<uint8_t>(destination)
        & static_cast<uint8_t>(EventDestination::Midi)) != 0u;
}

struct ColumnDefinition {
    std::size_t length = 16u;
    uint32_t stride = 1u;
    // Zero-based rotation applied independently to this column. Phase is part
    // of authored pattern state: reset and global-loop seeks begin at
    // (transport row + phase) modulo the active column length.
    std::size_t phase = 0u;
    Direction direction = Direction::Forward;
    bool muted = false;
};

// A typed action/value pair inspired by the two FX pairs in the v8 tracker.
// Action and value are independently polymetric. Empty retains action memory
// without executing it; Previous executes the remembered action. Value memory
// can change on a row whose action is empty and be consumed later.
struct FxPair {
    std::vector<FxActionCell> actions;
    std::vector<FxValueCell> values;
    ColumnDefinition actionColumn;
    ColumnDefinition valueColumn;
    // STEP emits only the authored row value. LINEAR also describes a bounded
    // segment from the current resolved value to the next executing CC value;
    // the timing facade materializes sample-timed 7-bit points between them.
    ValueInterpolation valueInterpolation = ValueInterpolation::Step;
};

// Read-only snapshot of the authoritative playback memory behind one FX pair.
// Empty, Previous, and muted cells retain these values; reset/setPattern clear
// them, while replacePattern retains overlapping track indices.
struct FxPlaybackMemorySnapshot {
    FxActionCell action;
    float value = 0.0f;
    bool hasAction = false;
    bool hasValue = false;
};

constexpr std::size_t kFxPairCount = 2u;

struct Track {
    std::string name;
    // Event-stage performance trim shared by MIDI and internal NoteOn
    // destinations. This deliberately scales onset velocity rather than
    // pretending the current shared instrument rack has per-lane post-DSP
    // buses. A future audio mixer can retain this as input trim.
    float velocityScale = 1.0f;
    // MIDI channels are one-based here to match the tracker UI and project
    // files. An output adapter converts this to the wire-format nibble.
    uint8_t midiChannel = 1u;
    // Retained as a fallback for explicitly targeted future graph nodes.
    // Known rack slots derive their destination from InstrumentKind, so MIDI
    // OUT is an instrument and never a second, ambiguous per-track switch.
    EventDestination destination = EventDestination::Midi;
    // Initial zero-based slot in the shared instrument rack. The independently
    // polymetric Instrument column can replace this playback memory per row.
    uint32_t initialInstrumentNodeId = 0u;
    // Zero means this lane does not participate in a choke group yet.
    uint32_t chokeGroup = 0u;
    std::vector<NoteCell> notes;
    std::vector<InstrumentCell> instruments;
    std::vector<ValueCell> velocities;
    std::array<FxPair, kFxPairCount> fxPairs;
    ColumnDefinition noteColumn;
    ColumnDefinition instrumentColumn;
    ColumnDefinition velocityColumn;
};

struct Pattern {
    std::string name;
    std::size_t visibleRows = 16u;
    std::vector<Track> tracks;
    std::array<BurstDefinition, kBurstDefinitionCount> bursts {};
};

enum class ScheduledEventKind : uint8_t {
    NoteOn,
    NoteOff,
    Parameter,
    ControlChange,
};

// Canonical scheduler output. Raw MIDI bytes and CLAP events are adapter
// formats derived from this destination-neutral representation.
struct ScheduledEvent {
    uint64_t absoluteSampleTime = 0u;
    // Zero is reserved for events without note identity (for example a
    // parameter action). Every onset/retrigger receives a new nonzero ID and
    // its explicit note-off carries the same ID.
    uint64_t noteId = 0u;
    // Zero means that duration is unspecified. For notes, the destination's
    // gate or a later NoteOff controls release; kSustainUntilExplicitNoteOff
    // suppresses the destination gate until the sequencer emits that NoteOff.
    // For a linear ControlChange this is the duration to its next row point.
    uint64_t durationSamples = 0u;
    uint32_t frameOffset = 0u;
    uint32_t track = 0u;
    uint32_t parameterId = 0u;
    uint32_t targetNode = kInvalidInstrumentNode;
    // Zero means no choke group. Nonzero groups are reserved for voice
    // allocators that support mutually exclusive articulations.
    uint32_t chokeGroup = 0u;
    float normalizedVelocity = 0.0f;
    // Canonical parameter values are normalized. A node adapter resolves the
    // destination's native range and stepped behavior off the render thread.
    float parameterValue = 0.0f;
    // Linear ControlChange segments retain their next resolved value here.
    // Ordinary endpoints and generated in-between points keep this equal to
    // parameterValue and use STEP interpolation.
    float parameterEndValue = 0.0f;
    uint8_t note = 0u;
    // kNoBurstDefinition marks an ordinary event. A valid index marks the
    // canonical first event of a Burst recipe for TimingPlaybackScheduler to
    // expand after whole-burst SEQ gates and timing have resolved.
    uint8_t burstDefinition = kNoBurstDefinition;
    // One-based to match the tracker model. MIDI and chip adapters translate
    // this into their own channel representation.
    uint8_t channel = 1u;
    ScheduledEventKind kind = ScheduledEventKind::NoteOn;
    ParameterScope parameterScope = ParameterScope::Global;
    ValueInterpolation valueInterpolation = ValueInterpolation::Step;
    // Marks a bounded derived CC point so a later authored endpoint can
    // cancel a stale tail after timing, tempo, or pattern changes.
    bool generatedInterpolation = false;
    EventDestination destination = EventDestination::Midi;
};

constexpr uint64_t kSustainUntilExplicitNoteOff
    = std::numeric_limits<uint64_t>::max();

EventDestination destinationForInstrument(uint32_t nodeId,
    EventDestination fallback = EventDestination::None) noexcept;

static_assert(std::is_trivially_copyable<ScheduledEvent>::value,
    "ScheduledEvent must remain callback-safe");

// Explicit MIDI-edge conversion. Note-on velocity is clamped to 1..127 to
// preserve the established tracker behavior (zero-velocity cells still
// produce a note-on rather than a MIDI note-off encoding).
uint8_t midiVelocityFromNormalized(float normalized) noexcept;
uint8_t midiValueFromNormalized(float normalized) noexcept;

struct TransportSettings {
    TransportSettings() = default;
    TransportSettings(double newSampleRate, double newBpm,
        uint32_t newTicksPerBeat, double newSwing) noexcept
        : sampleRate(newSampleRate)
        , bpm(newBpm)
        , ticksPerBeat(newTicksPerBeat)
        , swing(newSwing)
    {
    }

    double sampleRate = 48000.0;
    double bpm = 120.0;
    uint32_t ticksPerBeat = 4u;
    // 0.5 is straight. Values above 0.5 lengthen the first interval of each
    // two-tick pair and shorten the second while preserving pair duration.
    double swing = 0.5;
    // Functional warps repeat over this many tracker ticks. Legacy two-part
    // swing is applied first, then the serial stack maps the resulting
    // normalized phase. The stack remains editable while disabled; playback
    // applies it only after timingWarpEnabled is explicitly switched on.
    uint32_t warpCycleTicks = 16u;
    bool timingWarpEnabled = false;
    TimingWarpStack timingWarp;
    // MT can move a note early only if the sequencer knows about it before its
    // nominal tick. When a pattern contains MT, every note is delayed by this
    // compensation and MT moves within +/- microTimingRangeMilliseconds. The
    // lookahead is normalized to be at least the range.
    double timingLookaheadMilliseconds = 25.0;
    double microTimingRangeMilliseconds = 25.0;
    // Global song-row loop, shared by every polymetric column. loopEndRow is
    // exclusive so a 0..15 selection is represented as [0, 16).
    bool loopEnabled = false;
    uint32_t loopStartRow = 0u;
    uint32_t loopEndRow = 16u;
};

class Sequencer {
public:
    // This bootstrap object is deliberately single-owner and not thread-safe.
    // Serialize every call. setPattern(), setRandomSeed(), and reset() require
    // stopped playback. The application uses replacePattern() on its serial
    // scheduler queue to publish live edits without sharing this object with
    // the UI.
    void setPattern(Pattern pattern);
    // Control/scheduler-thread operation that may allocate. Existing column
    // phase, musical time, and transport state are retained where possible.
    void replacePattern(Pattern pattern);
    const Pattern& pattern() const noexcept { return pattern_; }

    // Builds a frozen, normalized set of runtime patterns and pre-sizes every
    // lane buffer needed by any member. This is a stopped control-thread
    // operation and may allocate. The selected member becomes authoritative
    // and reset() is applied exactly as for setPattern(). Stable project IDs
    // deliberately remain an engine concern; Song rows resolve them to these
    // prepared-set indices before playback starts.
    bool preparePatternSet(std::vector<Pattern> patterns,
        std::size_t initialPatternIndex = 0u);
    std::size_t preparedPatternCount() const noexcept
    {
        return preparedPatterns_.size();
    }
    // Allocation-free Song transition after one complete logical tick and
    // before the next. Overlapping lanes retain tracker recall, RNG, and true
    // active-note ownership. New/non-overlapping lanes start clean; removed
    // sounding lanes queue one exact NoteOff for the next target tick. The
    // master tick/sample clock and next note ID are untouched.
    bool activatePreparedPatternAtTickBoundary(
        std::size_t patternIndex) noexcept;

    void setTransport(TransportSettings settings);
    // Scheduler-thread operation for an already-emitted logical tick. The
    // replacement clock is normalized exactly like setTransport(), then the
    // interval from the completed tick to the next one is recomputed without
    // moving the completed tick, transport row, columns, or active voices.
    // Call only at the processSingleTick() boundary before emitting another
    // tick. Fixed-capacity TimingWarpStack keeps this allocation-free.
    void setTransportAtTickBoundary(TransportSettings settings) noexcept;
    const TransportSettings& transport() const { return transport_; }

    // Ephemeral Song playback controls. The runtime mask is ORed with the
    // authored NOTE mute and therefore performs the same one-time hard
    // release on the first muted tick. Pattern publication and reset retain
    // the mask; its owner must explicitly replace or clear it.
    void setRuntimeTrackMuteMask(uint32_t mask) noexcept
    {
        runtimeTrackMuteMask_ = mask;
    }
    uint32_t runtimeTrackMuteMask() const noexcept
    {
        return runtimeTrackMuteMask_;
    }
    // FILL is transient performance state. Song repetition context is owned
    // by the realtime planner and updated only at logical tick boundaries.
    void setFillActive(bool active) noexcept { fillActive_ = active; }
    bool fillActive() const noexcept { return fillActive_; }
    void setSongConditionContext(uint64_t passIndex,
        uint64_t passCount) noexcept
    {
        songConditionContext_.passIndex = passIndex;
        songConditionContext_.passCount = passCount;
        songConditionContext_.songActive = true;
        songConditionContextActive_ = true;
    }
    void setSongConditionContext(
        const SequencerConditionContext& context) noexcept
    {
        songConditionContext_ = context;
        songConditionContext_.songActive = true;
        songConditionContextActive_ = true;
    }
    void clearSongConditionContext() noexcept
    {
        songConditionContextActive_ = false;
    }
    // Quantized Song-row launch: seek every authored column to row + phase
    // while retaining the sample/tick clock, active voice ownership, FX
    // recall, deterministic random streams, and last-emitted note state.
    void relaunchColumnsAtTickBoundary(std::size_t row = 0u) noexcept;
    // Start a Song row at an explicit pattern position. Unlike a column-only
    // relaunch, this also moves the transport row so a nonzero loop-in point
    // is active on the very next tick. The global tick/sample clock and voice
    // ownership remain continuous.
    void launchSongRegionAtTickBoundary(std::size_t row) noexcept;
    // Performance resync for one track: move NOTE, INS, VOL, and both FX
    // action/value read heads to the same absolute row, deliberately ignoring
    // their independent authored phase rotations. The next logical tick
    // observes the new positions without moving the transport or another
    // track; playback memory and the last-rendered cursors are kept.
    bool resyncTrackColumnsAtTickBoundary(std::size_t track,
        std::size_t row = 0u) noexcept;
    // Performance resync for the complete pattern. Unlike a host seek or
    // Song launch, every authored phase is deliberately ignored so all
    // column read heads observe the same absolute row on the next tick.
    void resyncAllTrackColumnsAtTickBoundary(
        std::size_t row = 0u) noexcept;

    // start(true) and reset() rebuild playback storage and may allocate; they
    // are control-thread operations, never audio-callback commands.
    void start(bool resetPosition = true);
    // CLAP host-sync entry point. preparePatternSet() must have succeeded
    // first; the pre-sized playback storage then makes this reset/seek path
    // allocation-free for an audio-thread block-boundary transition. The
    // functional warp and swing stack are used to locate the first tracker
    // tick at or after the supplied host quarter-note position.
    bool startPreparedAtHostBeat(double hostBeat) noexcept;
    void stop();
    void reset();
    bool isPlaying() const { return playing_; }

    void setRandomSeed(uint32_t seed);

    // This call allocates nothing and limits writes to outputCapacity. Work is
    // linear in due ticks times track count, with the shared canonical slice
    // capped by kMaximumScheduledEventsPerBlock. Transport advances for the
    // complete block even when output fills; live callers treat any
    // dropped-event delta as a
    // fatal density error instead of emitting a partial MIDI/audio fanout.
    std::size_t process(uint32_t frameCount, ScheduledEvent* output,
        std::size_t outputCapacity) noexcept;
    // Additive stepping seam for scheduler facades. At most one due tracker
    // tick is emitted. The render clock advances across frameCount only when
    // no tick remains before the block end, allowing coincident warped ticks
    // to be observed independently without changing process().
    std::size_t processSingleTick(uint32_t frameCount,
        ScheduledEvent* output, std::size_t outputCapacity) noexcept;
    // Tail-drain clock seam for TimingPlaybackScheduler after Song playback
    // has stopped tick generation. Advances only the absolute render clock;
    // next tick time, musical counters, columns, memories, and voices remain
    // untouched. The increment saturates at uint64_t maximum.
    void advanceRenderClockWithoutTickGeneration(
        uint32_t frameCount) noexcept;

    uint64_t tickIndex() const { return tickIndex_; }
    uint64_t transportRow() const { return transportRow_; }
    uint64_t nextTickSampleFrame() const noexcept;
    uint64_t renderedFrameCount() const { return renderedFrameCount_; }
    uint64_t droppedEventCount() const { return droppedEventCount_; }
    std::size_t notePosition(std::size_t track) const noexcept;
    std::size_t instrumentPosition(std::size_t track) const noexcept;
    std::size_t velocityPosition(std::size_t track) const noexcept;
    std::size_t lastNotePosition(std::size_t track) const noexcept;
    bool lastNoteTriggered(std::size_t track) const noexcept;
    std::size_t lastInstrumentPosition(std::size_t track) const noexcept;
    std::size_t lastVelocityPosition(std::size_t track) const noexcept;
    std::size_t fxActionPosition(std::size_t track,
        std::size_t pair) const noexcept;
    std::size_t fxValuePosition(std::size_t track,
        std::size_t pair) const noexcept;
    std::size_t lastFxActionPosition(std::size_t track,
        std::size_t pair) const noexcept;
    std::size_t lastFxValuePosition(std::size_t track,
        std::size_t pair) const noexcept;
    FxPlaybackMemorySnapshot fxMemorySnapshot(std::size_t track,
        std::size_t pair) const noexcept;

private:
    struct TrackMemory {
        uint8_t note = 0u;
        std::array<uint8_t, kMaximumNoteVoices> notes {};
        uint8_t noteCount = 0u;
        uint8_t activeNote = 0u;
        std::array<uint8_t, kMaximumNoteVoices> activeNotes {};
        std::array<float, kMaximumNoteVoices> activeVelocities {};
        std::array<uint64_t, kMaximumNoteVoices> activeNoteIds {};
        uint8_t activeCount = 0u;
        uint8_t activeChannel = 1u;
        float velocity = 0.787f;
        std::array<float, kMaximumNoteVoices> velocities {};
        uint8_t velocityCount = 1u;
        float activeVelocity = 0.787f;
        uint64_t noteId = 0u;
        uint32_t instrumentNodeId = kInvalidInstrumentNode;
        // A lane may be reassigned while a note is active. Its release must
        // still address the rack slot that received the onset.
        uint32_t activeNodeId = kInvalidInstrumentNode;
        EventDestination activeDestination = EventDestination::None;
        // RP recalls the last onset that actually survived every source and
        // gate transform. This intentionally outlives a later Kill.
        uint8_t lastEmittedNote = 0u;
        std::array<uint8_t, kMaximumNoteVoices> lastEmittedNotes {};
        std::array<float, kMaximumNoteVoices> lastEmittedVelocities {};
        uint8_t lastEmittedCount = 0u;
        uint8_t lastEmittedChannel = 1u;
        float lastEmittedVelocity = 0.787f;
        uint32_t lastEmittedNodeId = kInvalidInstrumentNode;
        EventDestination lastEmittedDestination = EventDestination::None;
        uint8_t lastEmittedBurstDefinition = kNoBurstDefinition;
        bool hasLastEmitted = false;
        bool hasNote = false;
        bool noteMuted = true;
        bool sustainHeld = false;
    };

    struct ColumnState {
        std::size_t position = 0u;
        std::size_t lastPosition = 0u;
        int pingPongDirection = 1;
        uint32_t randomState = 1u;
    };

    struct TrackPlaybackState {
        struct FxState {
            ColumnState actionColumn;
            ColumnState valueColumn;
            FxActionCell action;
            float value = 0.0f;
            bool hasAction = false;
            bool hasValue = false;
        };

        TrackMemory memory;
        bool releaseActiveForDefaultReassignment = false;
        ColumnState noteColumn;
        ColumnState instrumentColumn;
        ColumnState velocityColumn;
        std::array<FxState, kFxPairCount> fxPairs;
        // SK is keyed by the resolved NOTE source row, matching the v8
        // first-pass-then-skip cycle. Storage is resized only at pattern
        // publication/reset boundaries, never in emitTick().
        std::vector<uint64_t> skipCounters;
        uint32_t noteFxRandomState = 1u;
        // True only when the most recently processed NOTE position emitted a
        // real onset. This distinguishes a polygon pulse from a moving cursor
        // and accounts for mute and unresolved Retrigger Previous cells.
        bool lastNoteTriggered = false;
    };

    static void normalizePattern(Pattern& pattern);
    void captureRemovedTrackReleases(std::size_t retainedTracks,
        const Pattern& previousPattern) noexcept;
    void resetTrackPlaybackState(std::size_t trackIndex,
        const Track& track);
    void transitionPlaybackState(const Pattern& previousPattern,
        const Pattern& nextPattern, std::size_t launchRow,
        bool relaunch) noexcept;
    void emitTick(uint64_t absoluteSampleTime, uint32_t frameOffset,
        ScheduledEvent* output, std::size_t outputCapacity,
        std::size_t& outputCount) noexcept;
    void advance(const ColumnDefinition& definition,
        ColumnState& state) noexcept;
    void seekAllColumns(std::size_t row) noexcept;
    static std::size_t activeLength(const ColumnDefinition& column,
        std::size_t dataSize) noexcept;
    static TransportSettings normalizedTransport(
        TransportSettings settings) noexcept;
    static long double tickInterval(const TransportSettings& settings,
        uint64_t tick) noexcept;
    static long double warpedTickPhase(const TransportSettings& settings,
        uint64_t tick) noexcept;
    long double nextTickInterval() const noexcept;
    uint32_t columnSeed(std::size_t track, uint32_t salt) const noexcept;
    static uint32_t nextRandom(uint32_t& state) noexcept;
    uint64_t allocateNoteId() noexcept;

    Pattern pattern_;
    std::vector<Pattern> preparedPatterns_;
    std::vector<TrackPlaybackState> playback_;
    std::array<ScheduledEvent, kMaximumTrackCount * kMaximumNoteVoices>
        pendingBoundaryReleases_ {};
    std::size_t pendingBoundaryReleaseCount_ = 0u;
    std::size_t activePreparedPatternIndex_ = 0u;
    TransportSettings transport_;
    long double nextTickFrame_ = 0.0L;
    uint64_t renderedFrameCount_ = 0u;
    uint64_t tickIndex_ = 0u;
    uint64_t transportRow_ = 0u;
    uint64_t droppedEventCount_ = 0u;
    uint64_t nextNoteId_ = 1u;
    uint32_t randomSeed_ = 0x6d2b79f5u;
    uint32_t runtimeTrackMuteMask_ = 0u;
    SequencerConditionContext songConditionContext_;
    bool fillActive_ = false;
    bool songConditionContextActive_ = false;
    bool playing_ = false;
};

} // namespace s3g::tracker
