#pragma once

#include "s3g_ambi_neural_ecology.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiNeuralLatticeWidth = 4u;
constexpr uint32_t kAmbiNeuralLatticeCellsPerPlane = 16u;
constexpr uint32_t kAmbiNeuralLatticeMaxPlanes = 8u;
constexpr uint32_t kAmbiNeuralLatticeCells =
    kAmbiNeuralLatticeCellsPerPlane * kAmbiNeuralLatticeMaxPlanes;
constexpr uint32_t kAmbiNeuralLatticeDirections = 8u;
constexpr uint32_t kAmbiNeuralLatticeMacros = 8u;
constexpr uint32_t kAmbiNeuralLatticeTrail = 32u;

enum class AmbiNeuralLatticeMacro : uint32_t {
    Activity = 0u,
    Feedback = 1u,
    Time = 2u,
    Field = 3u,
    Listening = 4u,
    Space = 5u,
    Heredity = 6u,
    Mutation = 7u,
};

struct AmbiNeuralLatticeCell {
    std::array<float, kAmbiNeuralLatticeMacros> macro {};
};

struct AmbiNeuralLatticeStorage {
    std::array<AmbiNeuralLatticeCell, kAmbiNeuralLatticeCells> cells {};
    std::array<uint32_t, kAmbiNeuralLatticeCells * kAmbiNeuralLatticeDirections> edges {};
    std::array<uint32_t, kAmbiNeuralLatticeTrail> trail {};
    uint32_t planeCount = 1u;
    uint32_t trailCount = 1u;
    uint32_t currentCell = 5u;
    uint32_t selectedCell = 5u;
};

inline uint32_t sanitizeAmbiNeuralLatticePlaneCount(uint32_t planes)
{
    if (planes >= 8u) return 8u;
    if (planes >= 4u) return 4u;
    if (planes >= 2u) return 2u;
    return 1u;
}

inline uint32_t ambiNeuralLatticeLocalNeighbor(uint32_t cell, uint32_t direction)
{
    static constexpr std::array<int32_t, kAmbiNeuralLatticeDirections> dx {{
        0, 1, 1, 1, 0, -1, -1, -1
    }};
    static constexpr std::array<int32_t, kAmbiNeuralLatticeDirections> dy {{
        -1, -1, 0, 1, 1, 1, 0, -1
    }};
    const uint32_t plane = cell / kAmbiNeuralLatticeCellsPerPlane;
    const uint32_t local = cell % kAmbiNeuralLatticeCellsPerPlane;
    const int32_t x = static_cast<int32_t>(local % kAmbiNeuralLatticeWidth);
    const int32_t y = static_cast<int32_t>(local / kAmbiNeuralLatticeWidth);
    const int32_t nextX = (x + dx[direction % kAmbiNeuralLatticeDirections]
        + static_cast<int32_t>(kAmbiNeuralLatticeWidth))
        % static_cast<int32_t>(kAmbiNeuralLatticeWidth);
    const int32_t nextY = (y + dy[direction % kAmbiNeuralLatticeDirections]
        + static_cast<int32_t>(kAmbiNeuralLatticeWidth))
        % static_cast<int32_t>(kAmbiNeuralLatticeWidth);
    return plane * kAmbiNeuralLatticeCellsPerPlane
        + static_cast<uint32_t>(nextY) * kAmbiNeuralLatticeWidth
        + static_cast<uint32_t>(nextX);
}

inline void resetAmbiNeuralLatticeEdges(AmbiNeuralLatticeStorage& storage)
{
    for (uint32_t cell = 0u; cell < kAmbiNeuralLatticeCells; ++cell) {
        for (uint32_t direction = 0u; direction < kAmbiNeuralLatticeDirections; ++direction) {
            storage.edges[cell * kAmbiNeuralLatticeDirections + direction] =
                ambiNeuralLatticeLocalNeighbor(cell, direction);
        }
    }
}

inline void connectAmbiNeuralLatticePlanes(
    AmbiNeuralLatticeStorage& storage, uint32_t salt = 0u)
{
    const uint32_t planes = sanitizeAmbiNeuralLatticePlaneCount(storage.planeCount);
    for (uint32_t plane = 0u; plane + 1u < planes; ++plane) {
        const uint32_t sourceLocal = (3u + plane * 5u + (salt >> ((plane % 4u) * 4u))) & 15u;
        const uint32_t targetLocal = (11u + plane * 7u + (salt >> (((plane + 2u) % 4u) * 4u))) & 15u;
        const uint32_t direction = (2u + plane * 3u + ((salt >> (plane % 16u)) & 7u)) & 7u;
        const uint32_t reverse = (direction + 4u) & 7u;
        const uint32_t source = plane * kAmbiNeuralLatticeCellsPerPlane + sourceLocal;
        const uint32_t target = (plane + 1u) * kAmbiNeuralLatticeCellsPerPlane + targetLocal;
        storage.edges[source * kAmbiNeuralLatticeDirections + direction] = target;
        storage.edges[target * kAmbiNeuralLatticeDirections + reverse] = source;
    }
}

inline AmbiNeuralLatticeCell sanitizeAmbiNeuralLatticeCell(AmbiNeuralLatticeCell cell)
{
    for (float& value : cell.macro) {
        value = std::isfinite(value) ? clamp(value, 0.0f, 1.0f) : 0.5f;
    }
    return cell;
}

inline AmbiNeuralLatticeStorage defaultAmbiNeuralLattice(uint32_t planeCount = 1u)
{
    AmbiNeuralLatticeStorage storage {};
    storage.planeCount = sanitizeAmbiNeuralLatticePlaneCount(planeCount);
    for (uint32_t plane = 0u; plane < kAmbiNeuralLatticeMaxPlanes; ++plane) {
        const float z = static_cast<float>(plane) / 7.0f;
        for (uint32_t row = 0u; row < kAmbiNeuralLatticeWidth; ++row) {
            for (uint32_t column = 0u; column < kAmbiNeuralLatticeWidth; ++column) {
                const uint32_t index = plane * kAmbiNeuralLatticeCellsPerPlane
                    + row * kAmbiNeuralLatticeWidth + column;
                const float x = static_cast<float>(column) / 3.0f;
                const float y = static_cast<float>(row) / 3.0f;
                const float diagonal = static_cast<float>((row + column + plane) % 4u) / 3.0f;
                auto& macro = storage.cells[index].macro;
                macro[0] = clamp(0.24f + y * 0.62f + (x - 0.5f) * 0.08f + z * 0.12f, 0.0f, 1.0f);
                macro[1] = clamp(0.18f + x * 0.72f - z * 0.10f, 0.0f, 1.0f);
                macro[2] = clamp(0.86f - y * 0.62f + (diagonal - 0.5f) * 0.18f - z * 0.16f, 0.0f, 1.0f);
                macro[3] = clamp(0.18f + (x * 0.52f + y * 0.48f) * 0.72f + z * 0.08f, 0.0f, 1.0f);
                macro[4] = clamp(0.16f + diagonal * 0.72f, 0.0f, 1.0f);
                macro[5] = clamp(0.20f + (1.0f - std::fabs(x - y)) * 0.68f - z * 0.08f, 0.0f, 1.0f);
                macro[6] = clamp(0.12f + (1.0f - x) * 0.72f + z * 0.12f, 0.0f, 1.0f);
                macro[7] = clamp(0.10f + (x * 0.44f + y * 0.56f) * 0.78f + z * 0.14f, 0.0f, 1.0f);
            }
        }
    }
    resetAmbiNeuralLatticeEdges(storage);
    connectAmbiNeuralLatticePlanes(storage);
    storage.trail.fill(storage.currentCell);
    return storage;
}

inline float ambiNeuralLatticeRandomUnit(uint32_t& seed)
{
    seed += 0x9e3779b9u;
    uint32_t value = seed;
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return static_cast<float>(value & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

inline AmbiNeuralLatticeStorage randomAmbiNeuralLattice(
    uint32_t& seed, uint32_t planeCount = 1u)
{
    AmbiNeuralLatticeStorage storage {};
    storage.planeCount = sanitizeAmbiNeuralLatticePlaneCount(planeCount);
    for (uint32_t plane = 0u; plane < kAmbiNeuralLatticeMaxPlanes; ++plane) {
        for (uint32_t macro = 0u; macro < kAmbiNeuralLatticeMacros; ++macro) {
            const float center = 0.24f + ambiNeuralLatticeRandomUnit(seed) * 0.52f;
            const float horizontal = (ambiNeuralLatticeRandomUnit(seed) * 2.0f - 1.0f) * 0.68f;
            const float vertical = (ambiNeuralLatticeRandomUnit(seed) * 2.0f - 1.0f) * 0.68f;
            const float diagonal = (ambiNeuralLatticeRandomUnit(seed) * 2.0f - 1.0f) * 0.28f;
            const float texture = 0.04f + ambiNeuralLatticeRandomUnit(seed) * 0.14f;
            for (uint32_t row = 0u; row < kAmbiNeuralLatticeWidth; ++row) {
                for (uint32_t column = 0u; column < kAmbiNeuralLatticeWidth; ++column) {
                    const float x = static_cast<float>(column) / 3.0f - 0.5f;
                    const float y = static_cast<float>(row) / 3.0f - 0.5f;
                    const float checker =
                        ((row + column + macro + plane) % 2u) == 0u ? 1.0f : -1.0f;
                    const float jitter =
                        (ambiNeuralLatticeRandomUnit(seed) * 2.0f - 1.0f) * texture;
                    const float value = center + x * horizontal + y * vertical
                        + x * y * diagonal + checker * texture * 0.32f + jitter;
                    const uint32_t cell = plane * kAmbiNeuralLatticeCellsPerPlane
                        + row * kAmbiNeuralLatticeWidth + column;
                    storage.cells[cell].macro[macro] = clamp(value, 0.02f, 0.98f);
                }
            }
        }
    }
    resetAmbiNeuralLatticeEdges(storage);
    (void)ambiNeuralLatticeRandomUnit(seed);
    const uint32_t foldSalt = seed ^ 0x464f4c44u;
    connectAmbiNeuralLatticePlanes(storage, foldSalt);
    const uint32_t activeCells = storage.planeCount * kAmbiNeuralLatticeCellsPerPlane;
    storage.currentCell = std::min<uint32_t>(
        static_cast<uint32_t>(ambiNeuralLatticeRandomUnit(seed)
            * static_cast<float>(activeCells)),
        activeCells - 1u);
    storage.selectedCell = storage.currentCell;
    storage.trail.fill(storage.currentCell);
    storage.trailCount = 1u;
    return storage;
}

inline AmbiNeuralLatticeStorage sanitizeAmbiNeuralLatticeStorage(AmbiNeuralLatticeStorage storage)
{
    for (auto& cell : storage.cells) cell = sanitizeAmbiNeuralLatticeCell(cell);
    storage.planeCount = sanitizeAmbiNeuralLatticePlaneCount(storage.planeCount);
    const uint32_t activeCells = storage.planeCount * kAmbiNeuralLatticeCellsPerPlane;
    for (uint32_t cell = 0u; cell < kAmbiNeuralLatticeCells; ++cell) {
        for (uint32_t direction = 0u; direction < kAmbiNeuralLatticeDirections; ++direction) {
            uint32_t& target = storage.edges[cell * kAmbiNeuralLatticeDirections + direction];
            if (cell >= activeCells || target >= activeCells) {
                target = ambiNeuralLatticeLocalNeighbor(cell, direction);
            }
        }
    }
    storage.currentCell = std::min<uint32_t>(storage.currentCell, activeCells - 1u);
    storage.selectedCell = std::min<uint32_t>(storage.selectedCell, activeCells - 1u);
    storage.trailCount = std::clamp<uint32_t>(storage.trailCount, 1u, kAmbiNeuralLatticeTrail);
    for (uint32_t& cell : storage.trail) {
        cell = std::min<uint32_t>(cell, activeCells - 1u);
    }
    return storage;
}

inline AmbiNeuralLatticeStorage resizeAmbiNeuralLattice(
    AmbiNeuralLatticeStorage storage, uint32_t planeCount)
{
    storage = sanitizeAmbiNeuralLatticeStorage(storage);
    storage.planeCount = sanitizeAmbiNeuralLatticePlaneCount(planeCount);
    resetAmbiNeuralLatticeEdges(storage);
    connectAmbiNeuralLatticePlanes(storage);
    const uint32_t activeCells = storage.planeCount * kAmbiNeuralLatticeCellsPerPlane;
    storage.currentCell = std::min<uint32_t>(storage.currentCell, activeCells - 1u);
    storage.selectedCell = std::min<uint32_t>(storage.selectedCell, activeCells - 1u);
    for (uint32_t& cell : storage.trail) cell = std::min<uint32_t>(cell, activeCells - 1u);
    return sanitizeAmbiNeuralLatticeStorage(storage);
}

inline AmbiNeuralLatticeCell lerpAmbiNeuralLatticeCell(
    const AmbiNeuralLatticeCell& a, const AmbiNeuralLatticeCell& b, float amount)
{
    AmbiNeuralLatticeCell result {};
    amount = clamp(amount, 0.0f, 1.0f);
    const float shaped = amount * amount * (3.0f - 2.0f * amount);
    for (uint32_t index = 0u; index < result.macro.size(); ++index) {
        result.macro[index] = lerp(a.macro[index], b.macro[index], shaped);
    }
    return result;
}

inline float normalizedRange(float value, float minimum, float maximum)
{
    return clamp((value - minimum) / std::max(1.0e-6f, maximum - minimum), 0.0f, 1.0f);
}

inline AmbiNeuralLatticeCell inscribeAmbiNeuralLatticeCell(const AmbiNeuralEcologyParams& source)
{
    const auto params = sanitizeAmbiNeuralEcologyParams(source);
    AmbiNeuralLatticeCell cell {};
    cell.macro[0] = 0.62f * normalizedRange(params.activity, 0.34f, 0.72f)
        + 0.38f * normalizedRange(params.drive, 1.2f, 3.8f);
    cell.macro[1] = 0.52f * normalizedRange(params.ringFeedback, 0.54f, 1.12f)
        + 0.48f * normalizedRange(params.matrixCoupling, 0.08f, 1.02f);
    cell.macro[2] = 0.48f * normalizedRange(params.registerSemitones, -28.0f, 20.0f)
        + 0.52f * normalizedRange(params.timeSpread, 0.36f, 1.55f);
    const float propagation = normalizedRange(std::log2(std::max(2.0f, params.propagationMs)),
        1.0f, std::log2(150.0f));
    cell.macro[3] = 0.62f * normalizedRange(params.fieldReturn, 0.08f, 0.88f)
        + 0.38f * propagation;
    cell.macro[4] = 0.46f * params.pickupAdapt + 0.34f * params.auditoryPlasticity
        + 0.20f * params.adaptation;
    cell.macro[5] = 0.44f * params.fieldWidth + 0.36f * params.mobility
        + 0.20f * params.cellWidth;
    cell.macro[6] = 0.44f * params.genomeMorph + 0.56f * params.heredity;
    cell.macro[7] = 0.30f * params.brownian + 0.30f * params.drift
        + 0.28f * params.plasticity + 0.12f * params.diversity;
    return sanitizeAmbiNeuralLatticeCell(cell);
}

inline AmbiNeuralEcologyParams applyAmbiNeuralLatticeCell(
    const AmbiNeuralEcologyParams& source, const AmbiNeuralLatticeCell& sourceCell, float amount)
{
    AmbiNeuralEcologyParams result = sanitizeAmbiNeuralEcologyParams(source);
    const auto cell = sanitizeAmbiNeuralLatticeCell(sourceCell);
    amount = clamp(amount, 0.0f, 1.0f);
    auto climate = [amount](float base, float target) { return lerp(base, target, amount); };
    const float activity = cell.macro[0];
    const float feedback = cell.macro[1];
    const float time = cell.macro[2];
    const float field = cell.macro[3];
    const float listening = cell.macro[4];
    const float space = cell.macro[5];
    const float heredity = cell.macro[6];
    const float mutation = cell.macro[7];

    result.activity = climate(result.activity, lerp(0.34f, 0.72f, activity));
    result.drive = climate(result.drive, lerp(1.20f, 3.80f, activity));
    result.ringFeedback = climate(result.ringFeedback, lerp(0.54f, 1.12f, feedback));
    result.matrixCoupling = climate(result.matrixCoupling, lerp(0.08f, 1.02f, feedback));
    result.hierarchy = climate(result.hierarchy, lerp(0.18f, 0.92f, feedback));

    result.registerSemitones = climate(result.registerSemitones, lerp(-28.0f, 20.0f, time));
    result.timeSpread = climate(result.timeSpread, lerp(0.36f, 1.55f, time));
    result.phaseShift = climate(result.phaseShift, lerp(0.04f, 0.88f, 1.0f - time));
    result.selfModulation = climate(result.selfModulation, lerp(0.08f, 0.92f, time));

    result.fieldReturn = climate(result.fieldReturn, lerp(0.08f, 0.88f, field));
    result.propagationMs = climate(result.propagationMs,
        std::exp2(lerp(std::log2(2.0f), std::log2(150.0f), field)));
    result.pickupFocus = climate(result.pickupFocus, lerp(0.42f, 0.98f, field));

    result.pickupAdapt = climate(result.pickupAdapt, lerp(0.0f, 0.82f, listening));
    result.pickupAnchor = climate(result.pickupAnchor, lerp(0.90f, 0.08f, listening));
    result.auditoryPlasticity = climate(result.auditoryPlasticity, lerp(0.01f, 0.52f, listening));
    result.adaptation = climate(result.adaptation, lerp(0.03f, 0.58f, listening));
    result.metabolism = climate(result.metabolism, lerp(0.14f, 0.68f, listening));

    result.fieldWidth = climate(result.fieldWidth, lerp(0.32f, 1.0f, space));
    result.cellWidth = climate(result.cellWidth, lerp(0.16f, 0.94f, space));
    result.mobility = climate(result.mobility, lerp(0.02f, 0.82f, space));
    result.spatialInertia = climate(result.spatialInertia, lerp(0.96f, 0.48f, space));

    result.genomeMorph = climate(result.genomeMorph, heredity);
    result.heredity = climate(result.heredity, heredity * 0.82f);

    result.brownian = climate(result.brownian, mutation * 0.62f);
    result.drift = climate(result.drift, mutation * 0.72f);
    result.plasticity = climate(result.plasticity, mutation * 0.48f);
    result.diversity = climate(result.diversity, lerp(0.08f, 0.92f, mutation));
    result.mutationDepth = climate(result.mutationDepth, lerp(0.12f, 0.88f, mutation));
    return sanitizeAmbiNeuralEcologyParams(result);
}

class AmbiNeuralFieldLattice {
public:
    AmbiNeuralFieldLattice() { setStorage(defaultAmbiNeuralLattice()); }

    void setStorage(AmbiNeuralLatticeStorage storage)
    {
        storage_ = sanitizeAmbiNeuralLatticeStorage(storage);
        currentCell_ = storage_.currentCell;
        targetCell_ = currentCell_;
        fromClimate_ = storage_.cells[currentCell_];
        transitionProgress_ = 1.0f;
        dwellElapsed_ = 0.0f;
        pickupEnergy_.fill(0.0f);
        midiBias_.fill(0.0f);
    }

    AmbiNeuralLatticeStorage storage() const
    {
        auto copy = storage_;
        copy.currentCell = currentCell_;
        return copy;
    }

    void setCell(uint32_t index, AmbiNeuralLatticeCell cell)
    {
        index = std::min<uint32_t>(
            index, storage_.planeCount * kAmbiNeuralLatticeCellsPerPlane - 1u);
        storage_.cells[index] = sanitizeAmbiNeuralLatticeCell(cell);
        if (index == currentCell_ && !transitioning()) fromClimate_ = storage_.cells[index];
    }

    const AmbiNeuralLatticeCell& cell(uint32_t index) const
    {
        return storage_.cells[std::min<uint32_t>(
            index, storage_.planeCount * kAmbiNeuralLatticeCellsPerPlane - 1u)];
    }

    void setEdge(uint32_t cell, uint32_t direction, uint32_t target)
    {
        const uint32_t activeCells =
            storage_.planeCount * kAmbiNeuralLatticeCellsPerPlane;
        cell = std::min<uint32_t>(cell, activeCells - 1u);
        target = std::min<uint32_t>(target, activeCells - 1u);
        storage_.edges[cell * kAmbiNeuralLatticeDirections
            + direction % kAmbiNeuralLatticeDirections] = target;
    }

    AmbiNeuralLatticeCell climate() const
    {
        if (!transitioning()) return storage_.cells[currentCell_];
        return lerpAmbiNeuralLatticeCell(fromClimate_, storage_.cells[targetCell_], transitionProgress_);
    }

    uint32_t currentCell() const { return currentCell_; }
    uint32_t targetCell() const { return targetCell_; }
    uint32_t planeCount() const { return storage_.planeCount; }
    uint32_t edge(uint32_t cell, uint32_t direction) const
    {
        const uint32_t activeCells =
            storage_.planeCount * kAmbiNeuralLatticeCellsPerPlane;
        cell = std::min<uint32_t>(cell, activeCells - 1u);
        return storage_.edges[cell * kAmbiNeuralLatticeDirections
            + direction % kAmbiNeuralLatticeDirections];
    }
    bool transitioning() const { return transitionProgress_ < 1.0f; }
    float transitionProgress() const { return transitionProgress_; }
    float dwellProgress(float dwellSeconds) const
    {
        return clamp(dwellElapsed_ / std::max(0.25f, dwellSeconds), 0.0f, 1.0f);
    }
    float pickupVote(uint32_t direction) const
    {
        return direction < pickupEnergy_.size() ? pickupEnergy_[direction] : 0.0f;
    }

    void stop()
    {
        targetCell_ = currentCell_;
        fromClimate_ = storage_.cells[currentCell_];
        transitionProgress_ = 1.0f;
        dwellElapsed_ = 0.0f;
        midiBias_.fill(0.0f);
    }

    void advanceTransition(float seconds)
    {
        if (!transitioning()) return;
        seconds = clamp(seconds, 0.0f, 1.0f);
        transitionProgress_ = std::min(1.0f,
            transitionProgress_ + seconds / std::max(0.05f, transitionDuration_));
        if (!transitioning()) {
            currentCell_ = targetCell_;
            storage_.currentCell = currentCell_;
            appendTrail(currentCell_);
            dwellElapsed_ = 0.0f;
        }
    }

    void requestCell(uint32_t cell, float force, float transitionSeconds)
    {
        cell = std::min<uint32_t>(
            cell, storage_.planeCount * kAmbiNeuralLatticeCellsPerPlane - 1u);
        if (cell == targetCell_ && transitioning()) return;
        if (cell == currentCell_ && !transitioning()) return;
        fromClimate_ = climate();
        targetCell_ = cell;
        transitionProgress_ = 0.0f;
        transitionDuration_ = std::max(0.05f,
            transitionSeconds * lerp(1.45f, 0.55f, clamp(force, 0.0f, 1.0f)));
        dwellElapsed_ = 0.0f;
    }

    void requestDirection(uint32_t direction, float force, AmbiNeuralScoreMode mode,
        float transitionSeconds)
    {
        direction %= 8u;
        force = clamp(force, 0.0f, 1.0f);
        if (mode == AmbiNeuralScoreMode::Midi) {
            requestCell(edge(currentCell_, direction), force, transitionSeconds);
        } else if (mode == AmbiNeuralScoreMode::Coupled) {
            midiBias_[direction] = std::max(midiBias_[direction], 0.12f + force * 1.25f);
            if (force > 0.82f) requestCell(edge(currentCell_, direction), force, transitionSeconds);
        }
    }

    void advance(float seconds, const std::array<float, kAmbiNeuralEcologyPickups>& pickups,
        uint32_t pickupCount, AmbiNeuralScoreMode mode, float dwellSeconds, float transitionSeconds)
    {
        seconds = clamp(seconds, 0.0f, 1.0f);
        pickupCount = pickupCount >= 8u ? 8u : 4u;
        const float envelope = 1.0f - std::exp(-seconds / 0.36f);
        for (uint32_t direction = 0u; direction < pickupEnergy_.size(); ++direction) {
            pickupEnergy_[direction] += (0.0f - pickupEnergy_[direction]) * envelope;
        }
        if (pickupCount == 4u) {
            static constexpr std::array<uint32_t, 4> direction {{ 0u, 2u, 4u, 6u }};
            for (uint32_t pickup = 0u; pickup < 4u; ++pickup) {
                const float heard = std::fabs(pickups[pickup]);
                pickupEnergy_[direction[pickup]] += (heard - pickupEnergy_[direction[pickup]]) * envelope;
            }
        } else {
            for (uint32_t pickup = 0u; pickup < 8u; ++pickup) {
                const float heard = std::fabs(pickups[pickup]);
                pickupEnergy_[pickup] += (heard - pickupEnergy_[pickup]) * envelope;
            }
        }
        const float biasDecay = std::exp(-seconds / std::max(0.5f, dwellSeconds));
        for (float& bias : midiBias_) bias *= biasDecay;

        if (transitioning()) {
            advanceTransition(seconds);
            return;
        }

        dwellElapsed_ += seconds;
        if (mode != AmbiNeuralScoreMode::Field && mode != AmbiNeuralScoreMode::Coupled) return;
        if (dwellElapsed_ < std::max(0.25f, dwellSeconds)) return;

        uint32_t bestDirection = 0u;
        float bestScore = -1.0f;
        float heardTotal = 0.0f;
        for (uint32_t direction = 0u; direction < 8u; ++direction) {
            const bool available = pickupCount == 8u || (direction % 2u) == 0u;
            if (!available) continue;
            heardTotal += pickupEnergy_[direction];
            const float score = pickupEnergy_[direction]
                + (mode == AmbiNeuralScoreMode::Coupled ? midiBias_[direction] : 0.0f)
                + 1.0e-5f * static_cast<float>((decisionCounter_ + direction * 5u) % 11u);
            if (score > bestScore) {
                bestScore = score;
                bestDirection = direction;
            }
        }
        if (heardTotal > 1.0e-6f || bestScore > 0.05f) {
            const float force = clamp(bestScore * 3.0f, 0.15f, 1.0f);
            requestCell(edge(currentCell_, bestDirection), force, transitionSeconds);
            midiBias_.fill(0.0f);
            ++decisionCounter_;
        }
    }

private:
    void appendTrail(uint32_t cell)
    {
        if (storage_.trailCount < kAmbiNeuralLatticeTrail) {
            storage_.trail[storage_.trailCount++] = cell;
            return;
        }
        for (uint32_t index = 1u; index < kAmbiNeuralLatticeTrail; ++index) {
            storage_.trail[index - 1u] = storage_.trail[index];
        }
        storage_.trail.back() = cell;
    }

    AmbiNeuralLatticeStorage storage_ {};
    AmbiNeuralLatticeCell fromClimate_ {};
    std::array<float, 8> pickupEnergy_ {};
    std::array<float, 8> midiBias_ {};
    uint32_t currentCell_ = 5u;
    uint32_t targetCell_ = 5u;
    uint32_t decisionCounter_ = 0u;
    float transitionProgress_ = 1.0f;
    float transitionDuration_ = 1.0f;
    float dwellElapsed_ = 0.0f;
};

} // namespace s3g
