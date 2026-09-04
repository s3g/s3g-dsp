#include "s3g/tracker/pitch_map.h"

#include "s3g_musical_scales.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace s3g::tracker {
namespace {

uint32_t nextRandom(uint32_t& state) noexcept
{
    if (state == 0u) state = 0x9e3779b9u;
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
}

uint32_t randomRange(uint32_t& state, uint32_t count) noexcept
{
    return count == 0u ? 0u : nextRandom(state) % count;
}

bool scaleContains(uint32_t scaleValue, uint8_t rootPitchClass,
    uint8_t note) noexcept
{
    const auto& scale = s3g::musicalScaleDefinition(scaleValue);
    const uint8_t relative = static_cast<uint8_t>((note + 12u
        - rootPitchClass % 12u) % 12u);
    for (uint32_t degree = 0u; degree < scale.size; ++degree) {
        if (static_cast<uint8_t>(scale.semitones[degree]) == relative)
            return true;
    }
    return false;
}

std::vector<uint8_t> pitchPool(const PitchMapSettings& source)
{
    PitchMapSettings settings = source;
    if (settings.minimumNote > settings.maximumNote)
        std::swap(settings.minimumNote, settings.maximumNote);
    std::vector<uint8_t> pool;
    pool.reserve(static_cast<std::size_t>(settings.maximumNote
        - settings.minimumNote) + 1u);
    for (uint32_t note = settings.minimumNote;
         note <= settings.maximumNote; ++note) {
        if (scaleContains(settings.scale, settings.rootPitchClass,
                static_cast<uint8_t>(note)))
            pool.push_back(static_cast<uint8_t>(note));
    }
    return pool;
}

std::size_t nearestPoolIndex(const std::vector<uint8_t>& pool,
    uint8_t note) noexcept
{
    std::size_t nearest = 0u;
    int distance = std::numeric_limits<int>::max();
    for (std::size_t index = 0u; index < pool.size(); ++index) {
        const int candidate = std::abs(static_cast<int>(pool[index])
            - static_cast<int>(note));
        if (candidate >= distance) continue;
        nearest = index;
        distance = candidate;
    }
    return nearest;
}

std::vector<PitchMapAssignment> sourceAssignments(const Pattern& pattern,
    std::size_t lane, std::size_t firstRow, std::size_t lastRow)
{
    std::vector<PitchMapAssignment> result;
    if (lane >= pattern.tracks.size() || firstRow > lastRow) return result;
    const auto& track = pattern.tracks[lane];
    const auto active = std::min(track.noteColumn.length, track.notes.size());
    if (active == 0u || firstRow >= active) return result;
    lastRow = std::min(lastRow, active - 1u);
    for (std::size_t row = firstRow; row <= lastRow; ++row) {
        const auto& cell = track.notes[row];
        if (cell.state != NoteCellState::Note) continue;
        PitchMapAssignment assignment;
        assignment.row = row;
        assignment.originalNote = cell.note;
        assignment.note = cell.note;
        assignment.voiceCount = static_cast<uint8_t>(
            cell.noteVoiceCount());
        for (std::size_t voice = 0u; voice < assignment.voiceCount;
             ++voice) {
            assignment.originalNotes[voice] = cell.noteVoice(voice);
            assignment.notes[voice] = cell.noteVoice(voice);
        }
        result.push_back(assignment);
    }
    return result;
}

std::size_t wrappedIndex(int64_t value, std::size_t count) noexcept
{
    if (count == 0u) return 0u;
    const auto modulus = static_cast<int64_t>(count);
    value %= modulus;
    if (value < 0) value += modulus;
    return static_cast<std::size_t>(value);
}

} // namespace

void retargetPitchMapVoicing(PitchMapAssignment& assignment,
    const PitchMapSettings& settings) noexcept
{
    assignment.voiceCount = static_cast<uint8_t>(std::clamp<std::size_t>(
        assignment.voiceCount, 1u, kMaximumNoteVoices));
    assignment.note = static_cast<uint8_t>(std::min<int>(assignment.note,
        127 - static_cast<int>(assignment.voiceCount - 1u)));
    assignment.notes[0u] = assignment.note;
    const int delta = static_cast<int>(assignment.note)
        - static_cast<int>(assignment.originalNote);
    const uint8_t effectiveRoot = static_cast<uint8_t>((
        static_cast<int>(settings.rootPitchClass)
        + static_cast<int>(settings.transposeSemitones) + 120) % 12);
    for (std::size_t voice = 1u; voice < assignment.voiceCount; ++voice) {
        const int desired = std::clamp(
            static_cast<int>(assignment.originalNotes[voice]) + delta,
            static_cast<int>(assignment.notes[voice - 1u]) + 1, 127);
        int nearest = -1;
        int nearestDistance = 128;
        for (int candidate = static_cast<int>(assignment.notes[voice - 1u])
                 + 1; candidate <= 127; ++candidate) {
            if (!scaleContains(settings.scale, effectiveRoot,
                    static_cast<uint8_t>(candidate))) continue;
            const int distance = std::abs(candidate - desired);
            if (distance >= nearestDistance) continue;
            nearest = candidate;
            nearestDistance = distance;
        }
        assignment.notes[voice] = static_cast<uint8_t>(nearest >= 0
            ? nearest : std::min<int>(127,
                  static_cast<int>(assignment.notes[voice - 1u]) + 1));
    }
    assignment.note = assignment.notes[0u];
}

const char* pitchContourName(PitchContour contour) noexcept
{
    switch (contour) {
    case PitchContour::Fit: return "FIT";
    case PitchContour::Rise: return "RISE";
    case PitchContour::Fall: return "FALL";
    case PitchContour::Pendulum: return "PENDULUM";
    case PitchContour::RandomWalk: return "RANDOM WALK";
    case PitchContour::VaryExisting: return "VARY EXISTING";
    case PitchContour::Manual: return "MANUAL";
    }
    return "FIT";
}

PitchMapAnalysis analyzePitchMap(const Pattern& pattern, std::size_t lane,
    std::size_t firstRow, std::size_t lastRow)
{
    PitchMapAnalysis result;
    if (firstRow > lastRow || pattern.tracks.empty()) return result;
    std::array<std::size_t, 12u> pitchClasses {};
    result.minimumNote = 127u;
    result.maximumNote = 0u;
    const std::size_t firstLane = lane == kPitchMapAllLanes ? 0u : lane;
    const std::size_t lastLane = lane == kPitchMapAllLanes
        ? pattern.tracks.size() - 1u : lane;
    if (firstLane >= pattern.tracks.size()) return {};
    for (std::size_t trackIndex = firstLane;
         trackIndex <= std::min(lastLane, pattern.tracks.size() - 1u);
         ++trackIndex) {
        const auto& track = pattern.tracks[trackIndex];
        const auto active = std::min(track.noteColumn.length,
            track.notes.size());
        if (active == 0u || firstRow >= active) continue;
        const auto end = std::min(lastRow, active - 1u);
        for (std::size_t row = firstRow; row <= end; ++row) {
            const auto& cell = track.notes[row];
            if (cell.state != NoteCellState::Note) continue;
            for (std::size_t voice = 0u; voice < cell.noteVoiceCount();
                 ++voice) {
                const auto note = cell.noteVoice(voice);
                ++result.noteCount;
                ++pitchClasses[note % 12u];
                result.minimumNote = std::min(result.minimumNote, note);
                result.maximumNote = std::max(result.maximumNote, note);
            }
        }
    }
    if (result.noteCount == 0u) {
        result.minimumNote = 0u;
        result.maximumNote = 127u;
        return result;
    }
    for (const auto count : pitchClasses)
        if (count > 0u) ++result.uniquePitchClasses;

    std::size_t bestMatches = 0u;
    // Menu order doubles as a conservative prior: familiar tonal scales win
    // exact ties instead of an obscure superset with the same evidence.
    for (uint32_t menuIndex = 1u; menuIndex < s3g::kMusicalScaleCount;
         ++menuIndex) {
        const uint32_t scale = s3g::musicalScaleValueForMenuIndex(menuIndex);
        for (uint8_t root = 0u; root < 12u; ++root) {
            std::size_t matches = 0u;
            for (uint8_t pitchClass = 0u; pitchClass < 12u; ++pitchClass) {
                if (pitchClasses[pitchClass] > 0u
                    && scaleContains(scale, root, pitchClass))
                    matches += pitchClasses[pitchClass];
            }
            const bool stronger = matches > bestMatches;
            const bool rootEvidence = matches == bestMatches
                && pitchClasses[root] > 0u
                && pitchClasses[result.rootPitchClass] == 0u;
            if (!stronger && !rootEvidence) continue;
            bestMatches = matches;
            result.matchingNotes = matches;
            result.rootPitchClass = root;
            result.scale = scale;
        }
    }
    const float coverage = static_cast<float>(result.matchingNotes)
        / static_cast<float>(result.noteCount);
    const float evidence = std::min(1.0f,
        static_cast<float>(result.uniquePitchClasses) / 4.0f);
    result.confidence = coverage * evidence;
    return result;
}

PitchMapResult previewPitchMap(const Pattern& pattern, std::size_t lane,
    std::size_t firstRow, std::size_t lastRow,
    const PitchMapSettings& sourceSettings)
{
    PitchMapResult result;
    result.assignments = sourceAssignments(pattern, lane, firstRow, lastRow);
    if (result.assignments.empty()) return result;
    PitchMapSettings settings = sourceSettings;
    settings.rootPitchClass %= 12u;
    settings.maximumLeapDegrees = std::clamp<uint8_t>(
        settings.maximumLeapDegrees, 1u, 12u);
    settings.variation = std::clamp(settings.variation, 0.0f, 1.0f);
    const auto pool = pitchPool(settings);
    if (pool.empty()) return result;

    std::vector<std::size_t> fitted;
    fitted.reserve(result.assignments.size());
    for (const auto& assignment : result.assignments)
        fitted.push_back(nearestPoolIndex(pool, assignment.originalNote));

    uint32_t random = settings.seed;
    std::size_t cursor = fitted.front();
    int direction = settings.contour == PitchContour::Fall ? -1 : 1;
    for (std::size_t index = 0u; index < result.assignments.size(); ++index) {
        std::size_t destination = fitted[index];
        const uint32_t variableLeap = 1u + static_cast<uint32_t>(std::lround(
            settings.variation
                * static_cast<float>(settings.maximumLeapDegrees - 1u)));
        const uint32_t leap = 1u + randomRange(random, variableLeap);
        switch (settings.contour) {
        case PitchContour::Fit:
            break;
        case PitchContour::Manual:
            result.assignments[index].note
                = result.assignments[index].originalNote;
            continue;
        case PitchContour::Rise:
            if (index > 0u) cursor = wrappedIndex(
                static_cast<int64_t>(cursor) + leap, pool.size());
            destination = cursor;
            break;
        case PitchContour::Fall:
            if (index > 0u) cursor = wrappedIndex(
                static_cast<int64_t>(cursor) - leap, pool.size());
            destination = cursor;
            break;
        case PitchContour::Pendulum:
            if (index > 0u) {
                int64_t candidate = static_cast<int64_t>(cursor)
                    + static_cast<int64_t>(direction)
                        * static_cast<int64_t>(leap);
                if (candidate >= static_cast<int64_t>(pool.size())) {
                    direction = -1;
                    candidate = static_cast<int64_t>(pool.size()) - 1
                        - (candidate - static_cast<int64_t>(pool.size()) + 1);
                } else if (candidate < 0) {
                    direction = 1;
                    candidate = -candidate;
                }
                cursor = static_cast<std::size_t>(std::clamp<int64_t>(
                    candidate, 0, static_cast<int64_t>(pool.size() - 1u)));
            }
            destination = cursor;
            break;
        case PitchContour::RandomWalk:
            if (index > 0u) {
                const int sign = randomRange(random, 2u) == 0u ? -1 : 1;
                cursor = static_cast<std::size_t>(std::clamp<int64_t>(
                    static_cast<int64_t>(cursor)
                        + sign * static_cast<int64_t>(leap),
                    0, static_cast<int64_t>(pool.size() - 1u)));
            }
            destination = cursor;
            break;
        case PitchContour::VaryExisting: {
            const uint32_t span = static_cast<uint32_t>(std::lround(
                settings.variation
                    * static_cast<float>(settings.maximumLeapDegrees)));
            if (span > 0u) {
                const int offset = static_cast<int>(randomRange(
                    random, span * 2u + 1u)) - static_cast<int>(span);
                destination = static_cast<std::size_t>(std::clamp<int64_t>(
                    static_cast<int64_t>(fitted[index]) + offset,
                    0, static_cast<int64_t>(pool.size() - 1u)));
            }
            break;
        }
        }
        if (settings.preserveEndpoints
            && settings.contour != PitchContour::Fit
            && settings.contour != PitchContour::Manual
            && (index == 0u || index + 1u == result.assignments.size()))
            destination = fitted[index];
        result.assignments[index].note = pool[destination];
    }

    if (settings.reversePitchOrder) {
        for (std::size_t left = 0u, right = result.assignments.size() - 1u;
             left < right; ++left, --right)
            std::swap(result.assignments[left].note,
                result.assignments[right].note);
    }
    if (settings.invertScaleDegrees) {
        const auto axis = nearestPoolIndex(pool,
            result.assignments.front().note);
        for (auto& assignment : result.assignments) {
            const auto source = nearestPoolIndex(pool, assignment.note);
            const auto inverted = std::clamp<int64_t>(
                static_cast<int64_t>(axis) * 2
                    - static_cast<int64_t>(source),
                0, static_cast<int64_t>(pool.size() - 1u));
            assignment.note = pool[static_cast<std::size_t>(inverted)];
        }
    }
    const int transpose = std::clamp<int>(
        settings.transposeSemitones, -24, 24);
    for (auto& assignment : result.assignments) {
        assignment.note = static_cast<uint8_t>(std::clamp(
            static_cast<int>(assignment.note) + transpose, 0, 127));
        retargetPitchMapVoicing(assignment, settings);
        bool assignmentChanged = false;
        for (std::size_t voice = 0u; voice < assignment.voiceCount; ++voice)
            assignmentChanged |= assignment.notes[voice]
                != assignment.originalNotes[voice];
        if (assignmentChanged) ++result.changed;
    }
    return result;
}

std::size_t applyPitchMap(Pattern& pattern, std::size_t lane,
    std::size_t firstRow, std::size_t lastRow,
    const PitchMapSettings& settings)
{
    const auto preview = previewPitchMap(pattern, lane, firstRow, lastRow,
        settings);
    if (lane >= pattern.tracks.size()) return 0u;
    auto& notes = pattern.tracks[lane].notes;
    std::size_t changed = 0u;
    for (const auto& assignment : preview.assignments) {
        if (assignment.row >= notes.size()
            || notes[assignment.row].state != NoteCellState::Note) continue;
        auto& cell = notes[assignment.row];
        bool rowChanged = cell.noteVoiceCount() != assignment.voiceCount;
        for (std::size_t voice = 0u;
             voice < assignment.voiceCount && !rowChanged; ++voice)
            rowChanged = cell.noteVoice(voice) != assignment.notes[voice];
        if (!rowChanged) continue;
        cell = NoteCell::withNotes(assignment.notes,
            assignment.voiceCount);
        ++changed;
    }
    return changed;
}

} // namespace s3g::tracker
