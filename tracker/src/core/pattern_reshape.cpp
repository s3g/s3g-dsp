#include "s3g/tracker/pattern_reshape.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace s3g::tracker {
namespace {

bool reshapeLaneIncluded(uint32_t laneMask, std::size_t lane) noexcept
{
    return lane < 32u
        && (laneMask & (uint32_t { 1u } << static_cast<uint32_t>(lane)))
            != 0u;
}

bool noteProducesOnset(const NoteCell& cell) noexcept
{
    return cell.state == NoteCellState::Note
        || cell.state == NoteCellState::Burst
        || cell.state == NoteCellState::RetriggerPrevious;
}

float median(std::vector<float> values, float fallback = 0.0f)
{
    if (values.empty()) return fallback;
    const auto middle = values.begin()
        + static_cast<std::ptrdiff_t>(values.size() / 2u);
    std::nth_element(values.begin(), middle, values.end());
    const float upper = *middle;
    if ((values.size() % 2u) != 0u) return upper;
    const auto lower = std::max_element(values.begin(), middle);
    return lower == middle ? upper : (*lower + upper) * 0.5f;
}

float mad(const std::vector<float>& values, float center)
{
    std::vector<float> deviations;
    deviations.reserve(values.size());
    for (const float value : values)
        deviations.push_back(std::abs(value - center));
    return median(std::move(deviations));
}

bool columnIsDirect(const ColumnDefinition& column) noexcept
{
    return column.direction == Direction::Forward
        && column.stride == 1u && column.phase == 0u
        && column.length > 0u;
}

bool directTimingPair(const FxPair& pair) noexcept
{
    return columnIsDirect(pair.actionColumn)
        && columnIsDirect(pair.valueColumn)
        && pair.actionColumn.length == pair.valueColumn.length;
}

bool pairContainsMicroTime(const FxPair& pair) noexcept
{
    const auto count = std::min(pair.actionColumn.length,
        pair.actions.size());
    for (std::size_t row = 0u; row < count; ++row) {
        const auto& action = pair.actions[row];
        if (action.state == FxActionCellState::Sequencer
            && action.sequencerAction == SequencerAction::MicroTime)
            return true;
    }
    return false;
}

bool hasMicroTimeAtStorageRow(const Track& track, std::size_t row) noexcept
{
    for (const auto& pair : track.fxPairs) {
        if (row >= pair.actions.size()) continue;
        const auto& action = pair.actions[row];
        if (action.state == FxActionCellState::Sequencer
            && action.sequencerAction == SequencerAction::MicroTime)
            return true;
    }
    return false;
}

FxPair* writableTimingPair(Track& track, std::size_t row) noexcept
{
    for (auto& pair : track.fxPairs) {
        if (!directTimingPair(pair)) continue;
        const auto count = std::min({ pair.actionColumn.length,
            pair.actions.size(), pair.values.size() });
        if (row >= count) continue;
        if (pair.actions[row].state == FxActionCellState::Empty
            && pair.values[row].state == FxValueCellState::Previous)
            return &pair;
    }
    return nullptr;
}

bool hasWritableTimingPair(const Track& source, std::size_t row) noexcept
{
    for (const auto& pair : source.fxPairs) {
        if (!directTimingPair(pair)) continue;
        const auto count = std::min({ pair.actionColumn.length,
            pair.actions.size(), pair.values.size() });
        if (row < count
            && pair.actions[row].state == FxActionCellState::Empty
            && pair.values[row].state == FxValueCellState::Previous)
            return true;
    }
    return false;
}

template <typename Callback>
void visitDirectTiming(const Pattern& pattern, Callback&& callback,
    uint32_t laneMask, std::size_t* unsupported = nullptr)
{
    for (std::size_t lane = 0u; lane < pattern.tracks.size(); ++lane) {
        if (!reshapeLaneIncluded(laneMask, lane)) continue;
        const auto& track = pattern.tracks[lane];
        for (std::size_t pairIndex = 0u;
             pairIndex < track.fxPairs.size(); ++pairIndex) {
            const auto& pair = track.fxPairs[pairIndex];
            if (!directTimingPair(pair)) {
                if (unsupported && pairContainsMicroTime(pair)) {
                    const auto count = std::min(pair.actionColumn.length,
                        pair.actions.size());
                    for (std::size_t row = 0u; row < count; ++row) {
                        const auto& action = pair.actions[row];
                        if (action.state == FxActionCellState::Sequencer
                            && action.sequencerAction
                                == SequencerAction::MicroTime)
                            ++*unsupported;
                    }
                }
                continue;
            }
            const auto count = std::min({ pair.actionColumn.length,
                pair.actions.size(), pair.values.size() });
            for (std::size_t row = 0u; row < count; ++row) {
                const auto& action = pair.actions[row];
                const auto& value = pair.values[row];
                if (action.state != FxActionCellState::Sequencer
                    || action.sequencerAction != SequencerAction::MicroTime
                    || value.state != FxValueCellState::Value) continue;
                callback(lane, pairIndex, row,
                    std::clamp(value.normalized, 0.0f, 1.0f));
            }
        }
    }
}

float limitedResidual(float residual, float spread,
    float threshold) noexcept
{
    if (!(threshold > 0.0f) || !(spread > 0.0f)) return residual;
    const float limit = threshold * spread;
    return std::clamp(residual, -limit, limit);
}

float phaseStrength(const PatternReshapeAnalysis& analysis,
    std::size_t phase) noexcept
{
    if (phase >= analysis.phaseNoteEvents.size()) return 0.0f;
    const auto maximum = *std::max_element(analysis.phaseNoteEvents.begin(),
        analysis.phaseNoteEvents.end());
    const float density = maximum > 0u
        ? static_cast<float>(analysis.phaseNoteEvents[phase])
            / static_cast<float>(maximum)
        : 0.0f;
    const float velocity = phase < analysis.phaseVelocitySupport.size()
            && analysis.phaseVelocitySupport[phase] > 0u
        ? analysis.phaseVelocityMedian[phase] : analysis.velocityMedian;
    return density * 0.75f + velocity * 0.25f;
}

// Infer a timing contour from the pattern itself rather than a named groove.
// This intentionally ignores existing MT: otherwise a phase with one MT
// sample becomes its own target and timing controls cannot reshape it. Weak
// onsets lean toward the nearest stronger rhythmic/velocity anchor; an exact
// tie leans forward without embedding a fixed template.
float inferredPhaseTiming(const PatternReshapeAnalysis& analysis,
    std::size_t phase) noexcept
{
    const auto cycle = analysis.cycleRows;
    if (cycle < 2u || phase >= analysis.phaseNoteEvents.size()
        || analysis.phaseNoteEvents[phase] == 0u)
        return 0.0f;
    const float current = phaseStrength(analysis, phase);
    float previousPull = 0.0f;
    float nextPull = 0.0f;
    for (std::size_t distance = 1u; distance < cycle; ++distance) {
        const auto previous = (phase + cycle - distance) % cycle;
        const float strength = phaseStrength(analysis, previous);
        if (strength > current + 0.0001f) {
            previousPull = (strength - current)
                / static_cast<float>(distance);
            break;
        }
    }
    for (std::size_t distance = 1u; distance < cycle; ++distance) {
        const auto next = (phase + distance) % cycle;
        const float strength = phaseStrength(analysis, next);
        if (strength > current + 0.0001f) {
            nextPull = (strength - current) / static_cast<float>(distance);
            break;
        }
    }
    const float strongest = std::max(previousPull, nextPull);
    if (!(strongest > 0.0f)) return 0.0f;
    const float magnitude = std::clamp(0.25f + strongest * 0.80f,
        0.25f, 0.80f);
    return nextPull >= previousPull ? magnitude : -magnitude;
}

float inferredVelocityAccent(const PatternReshapeAnalysis& analysis,
    std::size_t phase) noexcept
{
    if (phase < analysis.phaseVelocitySupport.size()
        && analysis.phaseVelocitySupport[phase] > 0u) {
        return analysis.phaseVelocityMedian[phase]
            - analysis.velocityMedian;
    }
    if (phase >= analysis.phaseNoteEvents.size()
        || analysis.phaseNoteEvents.empty())
        return 0.0f;
    const float total = static_cast<float>(std::accumulate(
        analysis.phaseNoteEvents.begin(), analysis.phaseNoteEvents.end(),
        std::size_t { 0u }));
    const float mean = total
        / static_cast<float>(analysis.phaseNoteEvents.size());
    const auto maximum = *std::max_element(analysis.phaseNoteEvents.begin(),
        analysis.phaseNoteEvents.end());
    if (maximum == 0u) return 0.0f;
    return 0.18f * (static_cast<float>(analysis.phaseNoteEvents[phase])
        - mean) / static_cast<float>(maximum);
}

uint64_t mutationBits(uint64_t seed, std::size_t lane,
    std::size_t row, uint64_t salt) noexcept
{
    uint64_t value = seed ^ (static_cast<uint64_t>(lane) + 1u)
        * 0x9e3779b97f4a7c15ULL;
    value ^= (static_cast<uint64_t>(row) + 1u)
        * 0xbf58476d1ce4e5b9ULL;
    value ^= salt * 0x94d049bb133111ebULL;
    value ^= value >> 30u;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27u;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31u);
}

float mutationUnit(uint64_t seed, std::size_t lane,
    std::size_t row, uint64_t salt) noexcept
{
    return static_cast<float>(mutationBits(seed, lane, row, salt) >> 40u)
        / static_cast<float>(1u << 24u);
}

float authoredVelocityAt(const Track& track, std::size_t row) noexcept
{
    const auto length = std::min(track.velocityColumn.length,
        track.velocities.size());
    if (length == 0u) return 0.787f;
    const auto& cell = track.velocities[row % length];
    return cell.state == ValueCellState::Value
        ? std::clamp(cell.normalized, 0.0f, 1.0f) : 0.787f;
}

float mutationAnchorStrength(const PatternReshapeAnalysis& analysis,
    const Track& track, std::size_t row) noexcept
{
    const float metrical = row % 4u == 0u ? 1.0f
        : row % 2u == 0u ? 0.55f : 0.20f;
    const float velocity = authoredVelocityAt(track, row);
    const auto maximum = analysis.phaseNoteEvents.empty() ? 0u
        : *std::max_element(analysis.phaseNoteEvents.begin(),
            analysis.phaseNoteEvents.end());
    const auto phase = analysis.phaseNoteEvents.empty() ? 0u
        : row % analysis.phaseNoteEvents.size();
    const float support = maximum > 0u
        ? static_cast<float>(analysis.phaseNoteEvents[phase])
            / static_cast<float>(maximum)
        : 0.0f;
    return std::clamp(velocity * 0.45f + metrical * 0.35f
        + support * 0.20f, 0.0f, 1.0f);
}

uint8_t mutationDefaultNote(const PatternReshapeSettings& settings,
    const Track& track, std::size_t lane) noexcept
{
    if (lane < settings.laneDefaultNotes.size())
        return settings.laneDefaultNotes[lane];
    for (const auto& cell : track.notes)
        if (cell.state == NoteCellState::Note) return cell.note;
    return 60u;
}

void mutateRhythm(PatternReshapeResult& result,
    const PatternReshapeSettings& settings)
{
    const float amount = settings.mutationAmount;
    if (!(amount > 0.0f)) return;
    constexpr std::array<std::size_t, 9u> cycleChoices {
        4u, 8u, 12u, 16u, 20u, 24u, 32u, 48u, 64u,
    };
    std::vector<uint8_t> burstSlots;
    for (std::size_t slot = 0u;
         slot < result.burstLibrary.bursts.size(); ++slot) {
        if (!result.burstLibrary.bursts[slot].empty())
            burstSlots.push_back(static_cast<uint8_t>(slot));
    }

    for (std::size_t lane = 0u;
         lane < result.pattern.tracks.size(); ++lane) {
        if (!reshapeLaneIncluded(settings.laneMask, lane)) continue;
        auto& track = result.pattern.tracks[lane];
        if (track.noteColumn.length == 0u) continue;

        if (settings.cycleDrift > 0.0f
            && mutationUnit(settings.mutationSeed, lane, 0u, 1u)
                < amount * settings.cycleDrift) {
            const auto current = std::clamp<std::size_t>(
                track.noteColumn.length, 4u, 64u);
            std::size_t nearest = 0u;
            for (std::size_t index = 1u;
                 index < cycleChoices.size(); ++index) {
                if (std::abs(static_cast<long long>(cycleChoices[index])
                        - static_cast<long long>(current))
                    < std::abs(static_cast<long long>(cycleChoices[nearest])
                        - static_cast<long long>(current)))
                    nearest = index;
            }
            const bool grow = (mutationBits(settings.mutationSeed,
                lane, current, 2u) & 1u) != 0u;
            const std::size_t distance = amount > 0.75f
                    && mutationUnit(settings.mutationSeed, lane, current, 3u)
                        < amount
                ? 2u : 1u;
            const std::size_t targetIndex = grow
                ? std::min(nearest + distance, cycleChoices.size() - 1u)
                : nearest > distance ? nearest - distance : 0u;
            const auto nextLength = cycleChoices[targetIndex];
            if (nextLength != track.noteColumn.length) {
                track.noteColumn.length = nextLength;
                track.noteColumn.phase %= nextLength;
                if (track.notes.size() < nextLength)
                    track.notes.resize(nextLength, NoteCell::rest());
                result.pattern.visibleRows = std::max(
                    result.pattern.visibleRows, nextLength);
                ++result.cyclesChanged;
            }
        }

        const auto length = std::min(track.noteColumn.length,
            track.notes.size());
        if (length == 0u) continue;
        std::size_t activeCount = static_cast<std::size_t>(std::count_if(
            track.notes.begin(), track.notes.begin()
                + static_cast<std::ptrdiff_t>(length),
            [](const NoteCell& cell) { return noteProducesOnset(cell); }));

        for (std::size_t row = 0u; row < length; ++row) {
            auto& cell = track.notes[row];
            const bool active = noteProducesOnset(cell);
            const float anchor = active ? mutationAnchorStrength(
                result.before, track, row) : 0.0f;
            if (settings.densityChange < 0.0f && active && activeCount > 1u
                && row != 0u && anchor < 0.88f) {
                const float chance = amount * -settings.densityChange
                    * (1.0f - anchor) * 1.35f;
                if (mutationUnit(settings.mutationSeed, lane, row, 11u)
                        < chance) {
                    cell = NoteCell::rest();
                    --activeCount;
                    ++result.notesRemoved;
                }
            } else if (settings.densityChange > 0.0f
                && cell.state == NoteCellState::Rest) {
                const float phase = static_cast<float>(row % 4u);
                const float offbeat = (phase == 1.0f || phase == 3.0f)
                    ? 1.0f : (phase == 2.0f ? 0.65f : 0.25f);
                const float gridBias = settings.syncopation >= 0.0f
                    ? 0.55f + settings.syncopation * 0.45f * offbeat
                    : 0.55f + -settings.syncopation * 0.45f
                        * (1.0f - offbeat);
                const float chance = amount * settings.densityChange
                    * 0.48f * gridBias;
                if (mutationUnit(settings.mutationSeed, lane, row, 12u)
                        < chance) {
                    cell = NoteCell::withNote(mutationDefaultNote(
                        settings, track, lane));
                    ++activeCount;
                    ++result.notesAdded;
                }
            }
        }

        if (settings.displacementRows > 0u) {
            const auto source = track.notes;
            for (std::size_t row = 0u; row < length; ++row) {
                if (!noteProducesOnset(source[row])
                    || !noteProducesOnset(track.notes[row]) || row == 0u)
                    continue;
                const float anchor = mutationAnchorStrength(
                    result.before, track, row);
                if (anchor >= 0.90f) continue;
                const float chance = amount * (0.28f
                    + 0.14f * static_cast<float>(
                        settings.displacementRows)) * (1.0f - anchor * 0.55f);
                if (mutationUnit(settings.mutationSeed, lane, row, 21u)
                        >= chance) continue;
                std::size_t best = row;
                float bestScore = -1.0f;
                for (uint32_t distance = 1u;
                     distance <= settings.displacementRows; ++distance) {
                    for (const int direction : { -1, 1 }) {
                        const auto signedTarget = static_cast<long long>(row)
                            + static_cast<long long>(direction)
                                * static_cast<long long>(distance);
                        if (signedTarget < 0
                            || signedTarget >= static_cast<long long>(length))
                            continue;
                        const auto target = static_cast<std::size_t>(
                            signedTarget);
                        if (track.notes[target].state != NoteCellState::Rest)
                            continue;
                        const auto phase = target % 4u;
                        const float offbeat = phase == 1u || phase == 3u
                            ? 1.0f : (phase == 2u ? 0.55f : 0.0f);
                        const float bias = settings.syncopation >= 0.0f
                            ? offbeat * settings.syncopation
                            : (1.0f - offbeat) * -settings.syncopation;
                        const float score = bias
                            + mutationUnit(settings.mutationSeed, lane,
                                target, 22u) * 0.35f
                            - static_cast<float>(distance) * 0.03f;
                        if (score > bestScore) {
                            best = target;
                            bestScore = score;
                        }
                    }
                }
                if (best == row) continue;
                track.notes[best] = track.notes[row];
                track.notes[row] = NoteCell::rest();
                ++result.notesMoved;
            }
        }

        if (!burstSlots.empty() && settings.burstChance > 0.0f) {
            for (std::size_t row = 0u; row < length; ++row) {
                auto& cell = track.notes[row];
                if (cell.state != NoteCellState::Note) continue;
                const float anchor = mutationAnchorStrength(
                    result.before, track, row);
                if (row == 0u || anchor >= 0.90f) continue;
                const float chance = amount * settings.burstChance
                    * (0.35f + (1.0f - anchor) * 0.65f);
                if (mutationUnit(settings.mutationSeed, lane, row, 31u)
                        >= chance) continue;
                const auto choice = mutationBits(settings.mutationSeed,
                    lane, row, 32u) % burstSlots.size();
                cell = NoteCell::withBurst(burstSlots[choice]);
                ++result.burstsCreated;
            }
        }
    }
}

} // namespace

std::size_t patternReshapeRows(const Pattern& pattern) noexcept
{
    std::size_t rows = std::max<std::size_t>(pattern.visibleRows, 1u);
    for (const auto& track : pattern.tracks) {
        rows = std::max(rows, std::min(track.noteColumn.length,
            track.notes.size()));
        rows = std::max(rows, std::min(track.velocityColumn.length,
            track.velocities.size()));
        for (const auto& pair : track.fxPairs) {
            rows = std::max(rows, std::min(pair.actionColumn.length,
                pair.actions.size()));
        }
    }
    return rows;
}

std::size_t inferPatternReshapeCycle(
    const Pattern& pattern, std::size_t requestedCycle) noexcept
{
    const auto rows = patternReshapeRows(pattern);
    if (requestedCycle > 0u)
        return std::clamp<std::size_t>(requestedCycle, 1u, 256u);
    constexpr std::size_t choices[] { 64u, 32u, 16u, 8u, 4u };
    for (const auto choice : choices) {
        if (choice <= rows && rows % choice == 0u) return choice;
    }
    for (const auto choice : choices)
        if (choice <= rows) return choice;
    return rows;
}

PatternReshapeAnalysis analyzePatternReshape(
    const Pattern& pattern, std::size_t cycleRows, uint32_t laneMask)
{
    PatternReshapeAnalysis result;
    result.rows = patternReshapeRows(pattern);
    result.cycleRows = inferPatternReshapeCycle(pattern, cycleRows);
    result.passes = std::max<std::size_t>(1u,
        (result.rows + result.cycleRows - 1u) / result.cycleRows);
    result.lanes.resize(pattern.tracks.size());
    result.phaseTimingMedian.assign(result.cycleRows, 0.0f);
    result.phaseVelocityMedian.assign(result.cycleRows, 0.787f);
    result.phaseNoteEvents.assign(result.cycleRows, 0u);
    result.phaseTimingSupport.assign(result.cycleRows, 0u);
    result.phaseVelocitySupport.assign(result.cycleRows, 0u);
    std::vector<float> timingValues;
    std::vector<float> velocityValues;
    std::vector<std::vector<float>> laneTiming(pattern.tracks.size());
    std::vector<std::vector<float>> laneVelocity(pattern.tracks.size());
    std::vector<std::vector<float>> phaseTiming(result.cycleRows);
    std::vector<std::vector<float>> phaseVelocity(result.cycleRows);

    for (std::size_t lane = 0u; lane < pattern.tracks.size(); ++lane) {
        if (!reshapeLaneIncluded(laneMask, lane)) continue;
        const auto& track = pattern.tracks[lane];
        const auto noteCount = std::min(track.noteColumn.length,
            track.notes.size());
        for (std::size_t row = 0u; row < noteCount; ++row) {
            if (!noteProducesOnset(track.notes[row])) continue;
            ++result.noteEvents;
            ++result.lanes[lane].noteEvents;
            ++result.phaseNoteEvents[row % result.cycleRows];
            if (!hasMicroTimeAtStorageRow(track, row)
                && hasWritableTimingPair(track, row))
                ++result.writableTimingOnsets;
        }
        const auto velocityCount = std::min(track.velocityColumn.length,
            track.velocities.size());
        for (std::size_t row = 0u; row < velocityCount; ++row) {
            if (track.velocities[row].state != ValueCellState::Value)
            {
                if (track.velocities[row].state == ValueCellState::Default)
                    ++result.defaultVelocityValues;
                continue;
            }
            const float value = std::clamp(
                track.velocities[row].normalized, 0.0f, 1.0f);
            velocityValues.push_back(value);
            laneVelocity[lane].push_back(value);
            phaseVelocity[row % result.cycleRows].push_back(value);
            ++result.velocityValues;
            ++result.lanes[lane].velocityValues;
        }
    }
    visitDirectTiming(pattern,
        [&](std::size_t lane, std::size_t, std::size_t row, float value) {
            const float centered = (value - 0.5f) * 2.0f;
            timingValues.push_back(centered);
            laneTiming[lane].push_back(centered);
            phaseTiming[row % result.cycleRows].push_back(centered);
            ++result.timingValues;
            ++result.lanes[lane].timingValues;
        }, laneMask, &result.unsupportedTimingValues);

    result.timingMedian = median(timingValues);
    result.timingMad = mad(timingValues, result.timingMedian);
    result.velocityMedian = median(velocityValues, 0.787f);
    result.velocityMad = mad(velocityValues, result.velocityMedian);
    std::size_t supportedTimingPhases = 0u;
    std::size_t supportedRhythmPhases = 0u;
    for (std::size_t phase = 0u; phase < result.cycleRows; ++phase) {
        result.phaseTimingSupport[phase] = phaseTiming[phase].size();
        result.phaseVelocitySupport[phase] = phaseVelocity[phase].size();
        result.phaseTimingMedian[phase] = median(phaseTiming[phase],
            result.timingMedian);
        result.phaseVelocityMedian[phase] = median(phaseVelocity[phase],
            result.velocityMedian);
        if (phaseTiming[phase].size() >= 2u) ++supportedTimingPhases;
        if (result.phaseNoteEvents[phase] > 0u) ++supportedRhythmPhases;
    }
    for (std::size_t lane = 0u; lane < result.lanes.size(); ++lane) {
        auto& stats = result.lanes[lane];
        stats.timingMedian = median(laneTiming[lane], result.timingMedian);
        stats.timingMad = mad(laneTiming[lane], stats.timingMedian);
        stats.velocityMedian = median(laneVelocity[lane],
            result.velocityMedian);
        stats.velocityMad = mad(laneVelocity[lane], stats.velocityMedian);
    }
    const auto evidence = std::max(result.timingValues, result.noteEvents);
    const float sampleConfidence = std::min(1.0f,
        static_cast<float>(evidence)
            / static_cast<float>(std::max<std::size_t>(2u,
                result.cycleRows * 2u)));
    const float phaseConfidence = result.cycleRows == 0u ? 0.0f
        : static_cast<float>(result.timingValues > 0u
                ? supportedTimingPhases : supportedRhythmPhases)
            / static_cast<float>(result.cycleRows);
    result.confidence = evidence == 0u ? 0.0f
        : std::clamp(0.25f + sampleConfidence * 0.45f
            + phaseConfidence * 0.30f, 0.0f, 1.0f);
    return result;
}

PatternReshapeResult reshapePattern(
    const Pattern& pattern, const BurstLibrary& burstLibrary,
    PatternReshapeSettings settings)
{
    settings.cycleRows = inferPatternReshapeCycle(
        pattern, settings.cycleRows);
    settings.pocket = std::clamp(settings.pocket, 0.0f, 1.0f);
    settings.tighten = std::clamp(settings.tighten, 0.0f, 1.0f);
    settings.timingDepth = std::clamp(settings.timingDepth, -1.0f, 1.0f);
    settings.velocityRange = std::clamp(settings.velocityRange, 0.0f, 2.0f);
    settings.accentDepth = std::clamp(settings.accentDepth, 0.0f, 2.0f);
    settings.laneBalance = std::clamp(settings.laneBalance, 0.0f, 1.0f);
    settings.timingOutlierThreshold = std::clamp(
        settings.timingOutlierThreshold, 0.0f, 8.0f);
    settings.velocityOutlierThreshold = std::clamp(
        settings.velocityOutlierThreshold, 0.0f, 8.0f);
    settings.mutationAmount = std::clamp(
        settings.mutationAmount, 0.0f, 1.0f);
    settings.densityChange = std::clamp(
        settings.densityChange, -1.0f, 1.0f);
    settings.syncopation = std::clamp(
        settings.syncopation, -1.0f, 1.0f);
    settings.displacementRows = std::min<uint32_t>(
        settings.displacementRows, 4u);
    settings.burstChance = std::clamp(
        settings.burstChance, 0.0f, 1.0f);
    settings.cycleDrift = std::clamp(
        settings.cycleDrift, 0.0f, 1.0f);

    PatternReshapeResult result;
    result.pattern = pattern;
    result.burstLibrary = burstLibrary;
    result.before = analyzePatternReshape(
        pattern, settings.cycleRows, settings.laneMask);
    mutateRhythm(result, settings);
    const auto working = analyzePatternReshape(
        result.pattern, settings.cycleRows, settings.laneMask);
    result.timingSkipped = working.unsupportedTimingValues;

    visitDirectTiming(result.pattern,
        [&](std::size_t lane, std::size_t pairIndex, std::size_t row,
            float) {
            const auto phase = row % working.cycleRows;
            const auto& laneStats = working.lanes[lane];
            const float contour = std::clamp(
                inferredPhaseTiming(working, phase)
                    * (1.0f + settings.timingDepth), -1.0f, 1.0f);
            auto& destination = result.pattern.tracks[lane]
                .fxPairs[pairIndex].values[row];
            const auto voiceCount = destination.valueVoiceCount();
            std::array<float, kMaximumNoteVoices> values {};
            bool changed = false;
            for (std::size_t voice = 0u; voice < voiceCount; ++voice) {
                const float source = (destination.valueVoice(voice)
                    - 0.5f) * 2.0f;
                float residual = source - laneStats.timingMedian;
                const float spread = laneStats.timingMad > 0.0f
                    ? laneStats.timingMad : working.timingMad;
                residual = limitedResidual(residual, spread,
                    settings.timingOutlierThreshold);
                const float limitedSource = std::clamp(
                    laneStats.timingMedian + residual, -1.0f, 1.0f);
                const float pocketed = limitedSource
                    + settings.pocket * (contour - limitedSource);
                const float shaped = std::clamp(
                    pocketed * (1.0f - settings.tighten), -1.0f, 1.0f);
                values[voice] = std::clamp(
                    0.5f + shaped * 0.5f, 0.0f, 1.0f);
                changed = changed || std::abs(destination.valueVoice(voice)
                    - values[voice]) > 0.00001f;
            }
            if (changed) {
                destination = FxValueCell::withValues(values, voiceCount);
                ++result.timingChanged;
            }
        }, settings.laneMask);

    if (settings.microTimingWrite == PatternReshapeWriteMode::FillMissing
        && settings.pocket > 0.0f) {
        for (std::size_t lane = 0u;
             lane < result.pattern.tracks.size(); ++lane) {
            if (!reshapeLaneIncluded(settings.laneMask, lane)) continue;
            auto& track = result.pattern.tracks[lane];
            const auto noteCount = std::min(track.noteColumn.length,
                track.notes.size());
            for (std::size_t row = 0u; row < noteCount; ++row) {
                if (!noteProducesOnset(track.notes[row])
                    || hasMicroTimeAtStorageRow(track, row))
                    continue;
                const auto phase = row % working.cycleRows;
                const float inferred = inferredPhaseTiming(
                    working, phase);
                const float target = std::clamp(inferred
                    * (1.0f + settings.timingDepth), -1.0f, 1.0f);
                const float shaped = std::clamp(settings.pocket * target
                    * (1.0f - settings.tighten), -1.0f, 1.0f);
                if (std::abs(shaped) <= 0.00001f) continue;
                auto* pair = writableTimingPair(track, row);
                if (!pair) {
                    ++result.timingSkipped;
                    continue;
                }
                pair->actions[row] = FxActionCell::sequencer(
                    SequencerAction::MicroTime);
                pair->values[row] = FxValueCell::withValue(
                    std::clamp(0.5f + shaped * 0.5f, 0.0f, 1.0f));
                ++result.timingCreated;
                ++result.timingChanged;
            }
        }
    }

    for (std::size_t lane = 0u;
         lane < result.pattern.tracks.size(); ++lane) {
        if (!reshapeLaneIncluded(settings.laneMask, lane)) continue;
        auto& track = result.pattern.tracks[lane];
        const auto count = std::min(track.velocityColumn.length,
            track.velocities.size());
        const auto& laneStats = working.lanes[lane];
        for (std::size_t row = 0u; row < count; ++row) {
            auto& cell = track.velocities[row];
            const bool create = cell.state == ValueCellState::Default
                && settings.velocityWrite
                    == PatternReshapeWriteMode::FillMissing;
            if (cell.state != ValueCellState::Value && !create) continue;
            const float spread = laneStats.velocityMad > 0.0f
                ? laneStats.velocityMad : working.velocityMad;
            const float balancedCenter = laneStats.velocityMedian
                + settings.laneBalance
                    * (working.velocityMedian
                        - laneStats.velocityMedian);
            const auto phase = row % working.cycleRows;
            const float accent = inferredVelocityAccent(
                working, phase);
            const auto voices = cell.state == ValueCellState::Value
                ? cell.valueVoiceCount() : 1u;
            std::array<float, kMaximumNoteVoices> reshaped {};
            bool rowChanged = create;
            for (std::size_t voice = 0u; voice < voices; ++voice) {
                const float source = cell.state == ValueCellState::Value
                    ? std::clamp(cell.valueVoice(voice), 0.0f, 1.0f)
                    : 0.787f;
                float residual = source - laneStats.velocityMedian;
                residual = limitedResidual(residual, spread,
                    settings.velocityOutlierThreshold);
                reshaped[voice] = std::clamp(balancedCenter
                    + residual * settings.velocityRange
                    + accent * (settings.accentDepth - 1.0f), 0.0f, 1.0f);
                rowChanged |= std::abs(source - reshaped[voice]) > 0.00001f;
            }
            if (rowChanged) {
                cell = ValueCell::withValues(reshaped, voices);
                if (create) ++result.velocityCreated;
                ++result.velocityChanged;
            }
        }
    }
    result.after = analyzePatternReshape(result.pattern,
        settings.cycleRows, settings.laneMask);
    return result;
}

} // namespace s3g::tracker
