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
constexpr uint32_t kAmbiNeuralLatticeTrail = 32u;
constexpr uint32_t kAmbiNeuralLatticeExpressionValues = 8u;

enum class AmbiNeuralLatticeExpression : uint32_t {
    Activity = 0u,
    RingFeedback,
    MatrixCoupling,
    Diversity,
    FieldReturn,
    PickupAdapt,
    FieldWidth,
    Mobility,
};

struct AmbiNeuralLatticeCell {
    std::array<float, kAmbiNeuralEcologyGenomeValues> genome {};
    std::array<float, kAmbiNeuralLatticeExpressionValues> expression {};
    uint32_t generation = 0u;
};

struct AmbiNeuralLatticeOffspring {
    std::array<float, kAmbiNeuralEcologyGenomeValues> genome {};
    std::array<float, kAmbiNeuralLatticeExpressionValues> expression {};
};

struct AmbiNeuralLatticeStorage {
    std::array<AmbiNeuralLatticeCell, kAmbiNeuralLatticeCells> cells {};
    std::array<uint32_t, kAmbiNeuralLatticeCells * kAmbiNeuralLatticeDirections> edges {};
    std::array<uint32_t, kAmbiNeuralLatticeMaxPlanes> ingressCells {};
    std::array<uint32_t, kAmbiNeuralLatticeMaxPlanes> egressCells {};
    std::array<uint32_t, kAmbiNeuralLatticeTrail> trail {};
    uint32_t planeCount = 1u;
    uint32_t trailCount = 1u;
    uint32_t currentCell = 5u;
    uint32_t selectedCell = 5u;
    uint32_t breedingSeed = 0x4c415454u;
    uint32_t birthCount = 0u;
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

inline void placeAmbiNeuralLatticePortals(
    AmbiNeuralLatticeStorage& storage, uint32_t salt = 0u)
{
    for (uint32_t plane = 0u; plane < kAmbiNeuralLatticeMaxPlanes; ++plane) {
        const uint32_t base = plane * kAmbiNeuralLatticeCellsPerPlane;
        const uint32_t ingressLocal =
            (5u + plane * 7u
                + ((salt >> ((plane % 4u) * 8u)) & 15u)) & 15u;
        uint32_t egressLocal =
            (10u + plane * 5u
                + ((salt >> (((plane + 2u) % 4u) * 8u)) & 15u)) & 15u;
        if (egressLocal == ingressLocal) egressLocal = (egressLocal + 5u) & 15u;
        storage.ingressCells[plane] = base + ingressLocal;
        storage.egressCells[plane] = base + egressLocal;
    }
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
    return static_cast<float>(value & 0x00ffffffu)
        / static_cast<float>(0x01000000u);
}

inline float ambiNeuralGenomeRange(uint32_t index)
{
    if (index < 64u) return 0.42f;
    if (index < 80u) return 0.38f;
    if (index < 112u) return 0.65f;
    if (index < 116u) return 0.24f;
    if (index == 116u) return 0.5f;
    return 1.0f;
}

inline AmbiNeuralLatticeCell sanitizeAmbiNeuralLatticeCell(
    AmbiNeuralLatticeCell cell)
{
    for (uint32_t index = 0u; index < cell.genome.size(); ++index) {
        float& value = cell.genome[index];
        value = std::isfinite(value) ? value : 0.0f;
        if (index < 64u) value = clamp(value, -0.42f, 0.42f);
        else if (index < 80u) value = clamp(value, -0.38f, 0.38f);
        else if (index < 112u) value = clamp(value, -0.65f, 0.65f);
        else if (index < 116u) value = clamp(value, -0.24f, 0.24f);
        else if (index == 116u) value -= std::floor(value);
        else value = clamp(value, -1.0f, 1.0f);
    }
    for (float& value : cell.expression) {
        value = std::isfinite(value) ? clamp(value, 0.0f, 1.0f) : 0.5f;
    }
    return cell;
}

inline std::array<float, kAmbiNeuralLatticeExpressionValues>
ambiNeuralLatticeExpressionFromParams(const AmbiNeuralEcologyParams& source)
{
    const auto params = sanitizeAmbiNeuralEcologyParams(source);
    return {{
        params.activity,
        params.ringFeedback / 1.25f,
        params.matrixCoupling / 1.25f,
        params.diversity,
        params.fieldReturn,
        params.pickupAdapt,
        params.fieldWidth,
        params.mobility,
    }};
}

inline AmbiNeuralEcologyParams applyAmbiNeuralLatticeExpression(
    const AmbiNeuralEcologyParams& source,
    const std::array<float, kAmbiNeuralLatticeExpressionValues>& expression,
    float amount)
{
    auto result = sanitizeAmbiNeuralEcologyParams(source);
    std::array<float, kAmbiNeuralLatticeExpressionValues> values = expression;
    for (float& value : values) {
        value = std::isfinite(value) ? clamp(value, 0.0f, 1.0f) : 0.5f;
    }
    amount = clamp(amount, 0.0f, 1.0f);
    result.activity = lerp(result.activity,
        values[static_cast<uint32_t>(AmbiNeuralLatticeExpression::Activity)], amount);
    result.ringFeedback = lerp(result.ringFeedback,
        values[static_cast<uint32_t>(AmbiNeuralLatticeExpression::RingFeedback)] * 1.25f,
        amount);
    result.matrixCoupling = lerp(result.matrixCoupling,
        values[static_cast<uint32_t>(AmbiNeuralLatticeExpression::MatrixCoupling)] * 1.25f,
        amount);
    result.diversity = lerp(result.diversity,
        values[static_cast<uint32_t>(AmbiNeuralLatticeExpression::Diversity)], amount);
    result.fieldReturn = lerp(result.fieldReturn,
        values[static_cast<uint32_t>(AmbiNeuralLatticeExpression::FieldReturn)], amount);
    result.pickupAdapt = lerp(result.pickupAdapt,
        values[static_cast<uint32_t>(AmbiNeuralLatticeExpression::PickupAdapt)], amount);
    result.fieldWidth = lerp(result.fieldWidth,
        values[static_cast<uint32_t>(AmbiNeuralLatticeExpression::FieldWidth)], amount);
    result.mobility = lerp(result.mobility,
        values[static_cast<uint32_t>(AmbiNeuralLatticeExpression::Mobility)], amount);
    return sanitizeAmbiNeuralEcologyParams(result);
}

inline std::array<float, kAmbiNeuralEcologyGenomeValues> sanitizeAmbiNeuralGenome(
    const std::array<float, kAmbiNeuralEcologyGenomeValues>& source)
{
    AmbiNeuralLatticeCell cell {};
    cell.genome = source;
    return sanitizeAmbiNeuralLatticeCell(cell).genome;
}

inline std::array<float, kAmbiNeuralEcologyGenomeValues> blendAmbiNeuralGenomes(
    const std::array<float, kAmbiNeuralEcologyGenomeValues>& source,
    const std::array<float, kAmbiNeuralEcologyGenomeValues>& target, float amount)
{
    const auto a = sanitizeAmbiNeuralGenome(source);
    const auto b = sanitizeAmbiNeuralGenome(target);
    std::array<float, kAmbiNeuralEcologyGenomeValues> result {};
    amount = clamp(amount, 0.0f, 1.0f);
    for (uint32_t index = 0u; index < result.size(); ++index) {
        if (index == 116u) {
            float delta = b[index] - a[index];
            delta -= std::round(delta);
            result[index] = a[index] + delta * amount;
            result[index] -= std::floor(result[index]);
        } else {
            result[index] = lerp(a[index], b[index], amount);
        }
    }
    return sanitizeAmbiNeuralGenome(result);
}

inline std::array<float, kAmbiNeuralEcologyGenomeValues> breedAmbiNeuralGenomes(
    const std::array<float, kAmbiNeuralEcologyGenomeValues>& source,
    const std::array<float, kAmbiNeuralEcologyGenomeValues>& resident,
    float recombine, float variation, uint32_t& seed)
{
    const auto parent = sanitizeAmbiNeuralGenome(source);
    const auto mate = sanitizeAmbiNeuralGenome(resident);
    std::array<float, kAmbiNeuralEcologyGenomeValues> child {};
    recombine = clamp(recombine, 0.0f, 1.0f);
    variation = clamp(variation, 0.0f, 1.0f);
    bool inheritMate = false;
    for (uint32_t index = 0u; index < child.size(); ++index) {
        const bool blockStart = index < 80u
            ? (index % 4u) == 0u
            : (index < 112u
                ? ((index - 80u) % 8u) == 0u
                : (index < 116u
                    ? index == 112u
                    : (index == 116u
                        || ((index - 117u) % 2u) == 0u)));
        if (blockStart) {
            inheritMate = ambiNeuralLatticeRandomUnit(seed) >= 0.5f;
        }
        const float inherited = inheritMate ? mate[index] : parent[index];
        if (index == 116u) {
            float delta = inherited - parent[index];
            delta -= std::round(delta);
            child[index] = parent[index] + delta * recombine
                + (ambiNeuralLatticeRandomUnit(seed) * 2.0f - 1.0f)
                    * variation * 0.30f;
            child[index] -= std::floor(child[index]);
        } else {
            child[index] = lerp(parent[index], inherited, recombine)
                + (ambiNeuralLatticeRandomUnit(seed) * 2.0f - 1.0f)
                    * ambiNeuralGenomeRange(index) * variation * 0.52f;
        }
    }
    return sanitizeAmbiNeuralGenome(child);
}

inline std::array<float, kAmbiNeuralLatticeExpressionValues>
breedAmbiNeuralLatticeExpressions(
    const std::array<float, kAmbiNeuralLatticeExpressionValues>& source,
    const std::array<float, kAmbiNeuralLatticeExpressionValues>& resident,
    float recombine, float variation, uint32_t& seed)
{
    std::array<float, kAmbiNeuralLatticeExpressionValues> child {};
    recombine = clamp(recombine, 0.0f, 1.0f);
    variation = clamp(variation, 0.0f, 1.0f);
    for (uint32_t index = 0u; index < child.size(); ++index) {
        const float parent = clamp(source[index], 0.0f, 1.0f);
        const float mate = clamp(resident[index], 0.0f, 1.0f);
        const float inherited =
            ambiNeuralLatticeRandomUnit(seed) >= 0.5f ? mate : parent;
        child[index] = clamp(
            lerp(parent, inherited, recombine)
                + (ambiNeuralLatticeRandomUnit(seed) * 2.0f - 1.0f)
                    * variation * 0.46f,
            0.0f, 1.0f);
    }
    return child;
}

inline std::array<float, 4u> ambiNeuralGenomeSignature(
    const std::array<float, kAmbiNeuralEcologyGenomeValues>& source)
{
    const auto genome = sanitizeAmbiNeuralGenome(source);
    std::array<float, 4u> signature {};
    for (uint32_t index = 0u; index < 64u; ++index) {
        signature[0u] += std::fabs(genome[index]) / (64.0f * 0.42f);
    }
    for (uint32_t index = 64u; index < 80u; ++index) {
        signature[1u] += std::fabs(genome[index]) / (16.0f * 0.38f);
    }
    for (uint32_t index = 80u; index < 116u; ++index) {
        signature[2u] += std::fabs(genome[index])
            / (36.0f * ambiNeuralGenomeRange(index));
    }
    signature[2u] += std::fabs(genome[116u] - 0.5f) * 2.0f / 37.0f;
    for (uint32_t index = 117u; index < genome.size(); ++index) {
        signature[3u] += std::fabs(genome[index]) / 16.0f;
    }
    for (float& value : signature) value = clamp(value, 0.0f, 1.0f);
    return signature;
}

inline AmbiNeuralLatticeStorage defaultAmbiNeuralLattice(
    uint32_t planeCount = 1u)
{
    AmbiNeuralLatticeStorage storage {};
    storage.planeCount = sanitizeAmbiNeuralLatticePlaneCount(planeCount);
    resetAmbiNeuralLatticeEdges(storage);
    placeAmbiNeuralLatticePortals(storage);
    storage.currentCell = storage.ingressCells[0u];
    storage.selectedCell = storage.currentCell;
    storage.trail.fill(storage.currentCell);
    return storage;
}

inline AmbiNeuralLatticeStorage growAmbiNeuralLattice(
    const std::array<float, kAmbiNeuralEcologyGenomeValues>& founder,
    const std::array<float, kAmbiNeuralLatticeExpressionValues>& founderExpression,
    uint32_t seed, uint32_t planeCount, float variation, float recombine)
{
    auto storage = defaultAmbiNeuralLattice(planeCount);
    storage.breedingSeed = seed == 0u ? 1u : seed;
    resetAmbiNeuralLatticeEdges(storage);
    placeAmbiNeuralLatticePortals(
        storage, storage.breedingSeed ^ 0x504f5254u);
    storage.currentCell = storage.ingressCells[0u];
    storage.selectedCell = storage.currentCell;
    storage.trail.fill(storage.currentCell);
    storage.trailCount = 1u;
    storage.cells[storage.currentCell].genome =
        sanitizeAmbiNeuralGenome(founder);
    storage.cells[storage.currentCell].expression = founderExpression;

    uint32_t breedSeed = storage.breedingSeed;
    for (uint32_t plane = 0u; plane < storage.planeCount; ++plane) {
        const uint32_t root = storage.ingressCells[plane];
        if (plane > 0u) {
            const uint32_t parent = storage.egressCells[plane - 1u];
            const uint32_t mate = storage.ingressCells[plane - 1u];
            storage.cells[root].genome = breedAmbiNeuralGenomes(
                storage.cells[parent].genome, storage.cells[mate].genome,
                recombine, variation, breedSeed);
            storage.cells[root].expression =
                breedAmbiNeuralLatticeExpressions(
                    storage.cells[parent].expression,
                    storage.cells[mate].expression,
                    recombine, variation, breedSeed);
            storage.cells[root].generation =
                std::max(storage.cells[parent].generation,
                    storage.cells[mate].generation) + 1u;
        }

        std::array<uint32_t, kAmbiNeuralLatticeCellsPerPlane> queue {};
        std::array<bool, kAmbiNeuralLatticeCellsPerPlane> visited {};
        uint32_t read = 0u;
        uint32_t write = 1u;
        queue[0u] = root;
        visited[root % kAmbiNeuralLatticeCellsPerPlane] = true;
        while (read < write) {
            const uint32_t parentCell = queue[read++];
            for (uint32_t direction = 0u;
                direction < kAmbiNeuralLatticeDirections; ++direction) {
                const uint32_t childCell =
                    storage.edges[
                        parentCell * kAmbiNeuralLatticeDirections + direction];
                const uint32_t childLocal =
                    childCell % kAmbiNeuralLatticeCellsPerPlane;
                if (childCell / kAmbiNeuralLatticeCellsPerPlane != plane
                    || visited[childLocal]) {
                    continue;
                }
                visited[childLocal] = true;
                queue[write++] = childCell;
                const uint32_t mateCell =
                    read > 1u ? queue[read - 2u] : root;
                storage.cells[childCell].genome = breedAmbiNeuralGenomes(
                    storage.cells[parentCell].genome,
                    storage.cells[mateCell].genome,
                    recombine, variation, breedSeed);
                storage.cells[childCell].expression =
                    breedAmbiNeuralLatticeExpressions(
                        storage.cells[parentCell].expression,
                        storage.cells[mateCell].expression,
                        recombine, variation, breedSeed);
                storage.cells[childCell].generation =
                    storage.cells[parentCell].generation + 1u;
            }
        }
    }
    storage.breedingSeed = breedSeed;
    return storage;
}

inline AmbiNeuralLatticeStorage sanitizeAmbiNeuralLatticeStorage(
    AmbiNeuralLatticeStorage storage)
{
    for (auto& cell : storage.cells) {
        cell = sanitizeAmbiNeuralLatticeCell(cell);
    }
    storage.planeCount =
        sanitizeAmbiNeuralLatticePlaneCount(storage.planeCount);
    const uint32_t activeCells =
        storage.planeCount * kAmbiNeuralLatticeCellsPerPlane;
    auto fallbackPortals = storage;
    placeAmbiNeuralLatticePortals(
        fallbackPortals, storage.breedingSeed ^ 0x504f5254u);
    for (uint32_t plane = 0u;
        plane < kAmbiNeuralLatticeMaxPlanes; ++plane) {
        const uint32_t first = plane * kAmbiNeuralLatticeCellsPerPlane;
        const uint32_t last = first + kAmbiNeuralLatticeCellsPerPlane;
        if (storage.ingressCells[plane] < first
            || storage.ingressCells[plane] >= last) {
            storage.ingressCells[plane] =
                fallbackPortals.ingressCells[plane];
        }
        if (storage.egressCells[plane] < first
            || storage.egressCells[plane] >= last
            || storage.egressCells[plane]
                == storage.ingressCells[plane]) {
            storage.egressCells[plane] =
                fallbackPortals.egressCells[plane];
        }
    }
    for (uint32_t cell = 0u; cell < kAmbiNeuralLatticeCells; ++cell) {
        for (uint32_t direction = 0u;
            direction < kAmbiNeuralLatticeDirections; ++direction) {
            uint32_t& target =
                storage.edges[cell * kAmbiNeuralLatticeDirections + direction];
            if (cell >= activeCells || target >= activeCells
                || target / kAmbiNeuralLatticeCellsPerPlane
                    != cell / kAmbiNeuralLatticeCellsPerPlane) {
                target = ambiNeuralLatticeLocalNeighbor(cell, direction);
            }
        }
    }
    storage.currentCell =
        std::min<uint32_t>(storage.currentCell, activeCells - 1u);
    storage.selectedCell =
        std::min<uint32_t>(storage.selectedCell, activeCells - 1u);
    storage.trailCount =
        std::clamp<uint32_t>(storage.trailCount, 1u, kAmbiNeuralLatticeTrail);
    for (uint32_t& cell : storage.trail) {
        cell = std::min<uint32_t>(cell, activeCells - 1u);
    }
    if (storage.breedingSeed == 0u) storage.breedingSeed = 1u;
    return storage;
}

inline AmbiNeuralLatticeStorage resizeAmbiNeuralLattice(
    AmbiNeuralLatticeStorage storage, uint32_t planeCount)
{
    storage = sanitizeAmbiNeuralLatticeStorage(storage);
    storage.planeCount = sanitizeAmbiNeuralLatticePlaneCount(planeCount);
    resetAmbiNeuralLatticeEdges(storage);
    const uint32_t activeCells =
        storage.planeCount * kAmbiNeuralLatticeCellsPerPlane;
    storage.currentCell =
        std::min<uint32_t>(storage.currentCell, activeCells - 1u);
    storage.selectedCell =
        std::min<uint32_t>(storage.selectedCell, activeCells - 1u);
    for (uint32_t& cell : storage.trail) {
        cell = std::min<uint32_t>(cell, activeCells - 1u);
    }
    return sanitizeAmbiNeuralLatticeStorage(storage);
}

class AmbiNeuralFieldLattice {
public:
    AmbiNeuralFieldLattice() { setStorage(defaultAmbiNeuralLattice()); }

    void setStorage(AmbiNeuralLatticeStorage storage)
    {
        storage_ = sanitizeAmbiNeuralLatticeStorage(storage);
        currentCell_ = storage_.currentCell;
        targetCell_ = currentCell_;
        transitionProgress_ = 1.0f;
        dwellElapsed_ = 0.0f;
        pickupEnergy_.fill(0.0f);
        midiBias_.fill(0.0f);
        movesOnPlane_ = 0u;
        refreshPortalMoveLimit();
    }

    const AmbiNeuralLatticeStorage& storage() const { return storage_; }

    void setCell(uint32_t index, AmbiNeuralLatticeCell cell)
    {
        index = std::min<uint32_t>(
            index, storage_.planeCount * kAmbiNeuralLatticeCellsPerPlane - 1u);
        storage_.cells[index] = sanitizeAmbiNeuralLatticeCell(cell);
    }

    const AmbiNeuralLatticeCell& cell(uint32_t index) const
    {
        return storage_.cells[std::min<uint32_t>(
            index, storage_.planeCount * kAmbiNeuralLatticeCellsPerPlane - 1u)];
    }

    AmbiNeuralLatticeOffspring performBirth(
        float recombine, float variation, float memory)
    {
        const uint32_t activeCells =
            storage_.planeCount * kAmbiNeuralLatticeCellsPerPlane;
        const uint32_t source =
            std::min<uint32_t>(eventSourceCell_, activeCells - 1u);
        const uint32_t target =
            std::min<uint32_t>(eventTargetCell_, activeCells - 1u);
        AmbiNeuralLatticeOffspring child {};
        child.genome = breedAmbiNeuralGenomes(
            storage_.cells[source].genome, storage_.cells[target].genome,
            recombine, variation, storage_.breedingSeed);
        child.expression = breedAmbiNeuralLatticeExpressions(
            storage_.cells[source].expression,
            storage_.cells[target].expression,
            recombine, variation, storage_.breedingSeed);
        memory = clamp(memory, 0.0f, 1.0f);
        if (memory > 1.0e-6f) {
            auto resident = storage_.cells[target];
            resident.genome =
                blendAmbiNeuralGenomes(resident.genome, child.genome, memory);
            for (uint32_t index = 0u; index < resident.expression.size(); ++index) {
                resident.expression[index] = lerp(
                    resident.expression[index], child.expression[index], memory);
            }
            resident.generation =
                std::max(storage_.cells[source].generation, resident.generation) + 1u;
            storage_.cells[target] = sanitizeAmbiNeuralLatticeCell(resident);
        }
        ++storage_.birthCount;
        return child;
    }

    void setEdge(uint32_t cell, uint32_t direction, uint32_t target)
    {
        const uint32_t activeCells =
            storage_.planeCount * kAmbiNeuralLatticeCellsPerPlane;
        cell = std::min<uint32_t>(cell, activeCells - 1u);
        target = std::min<uint32_t>(target, activeCells - 1u);
        if (target / kAmbiNeuralLatticeCellsPerPlane
            != cell / kAmbiNeuralLatticeCellsPerPlane) {
            target = ambiNeuralLatticeLocalNeighbor(cell, direction);
        }
        storage_.edges[cell * kAmbiNeuralLatticeDirections
            + direction % kAmbiNeuralLatticeDirections] = target;
    }

    uint32_t currentCell() const { return currentCell_; }
    uint32_t targetCell() const { return targetCell_; }
    uint32_t planeCount() const { return storage_.planeCount; }
    uint32_t ingressCell(uint32_t plane) const
    {
        return storage_.ingressCells[
            std::min<uint32_t>(plane, storage_.planeCount - 1u)];
    }
    uint32_t egressCell(uint32_t plane) const
    {
        return storage_.egressCells[
            std::min<uint32_t>(plane, storage_.planeCount - 1u)];
    }
    uint32_t portalDestination(uint32_t cell) const
    {
        if (storage_.planeCount <= 1u) return cell;
        const uint32_t plane = std::min<uint32_t>(
            cell / kAmbiNeuralLatticeCellsPerPlane,
            storage_.planeCount - 1u);
        if (cell != storage_.egressCells[plane]) return cell;
        return storage_.ingressCells[(plane + 1u) % storage_.planeCount];
    }
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
        return clamp(
            dwellElapsed_ / std::max(0.25f, dwellSeconds), 0.0f, 1.0f);
    }
    float pickupVote(uint32_t direction) const
    {
        return direction < pickupEnergy_.size()
            ? pickupEnergy_[direction] : 0.0f;
    }
    uint32_t eventSerial() const { return eventSerial_; }
    uint32_t eventSourceCell() const { return eventSourceCell_; }
    uint32_t eventTargetCell() const { return eventTargetCell_; }
    bool eventIsReproductive() const { return eventReproductive_; }
    float eventTransitionDuration() const { return eventTransitionDuration_; }

    void stop()
    {
        targetCell_ = currentCell_;
        transitionProgress_ = 1.0f;
        dwellElapsed_ = 0.0f;
        midiBias_.fill(0.0f);
    }

    void advanceTransition(float seconds)
    {
        if (!transitioning()) return;
        seconds = clamp(seconds, 0.0f, 1.0f);
        transitionProgress_ = std::min(1.0f,
            transitionProgress_
                + seconds / std::max(0.05f, transitionDuration_));
        if (!transitioning()) {
            const uint32_t previousPlane =
                currentCell_ / kAmbiNeuralLatticeCellsPerPlane;
            currentCell_ = targetCell_;
            storage_.currentCell = currentCell_;
            appendTrail(currentCell_);
            dwellElapsed_ = 0.0f;
            const uint32_t currentPlane =
                currentCell_ / kAmbiNeuralLatticeCellsPerPlane;
            if (currentPlane != previousPlane) {
                movesOnPlane_ = 0u;
                refreshPortalMoveLimit();
            } else {
                ++movesOnPlane_;
            }
        }
    }

    void requestCell(
        uint32_t cell, float force, float transitionSeconds,
        bool reproductive = true)
    {
        cell = std::min<uint32_t>(
            cell, storage_.planeCount * kAmbiNeuralLatticeCellsPerPlane - 1u);
        if (cell == targetCell_ && transitioning()) return;
        if (cell == currentCell_ && !transitioning()) return;
        targetCell_ = cell;
        transitionProgress_ = 0.0f;
        transitionDuration_ = std::max(0.05f,
            transitionSeconds
                * lerp(1.45f, 0.55f, clamp(force, 0.0f, 1.0f)));
        dwellElapsed_ = 0.0f;
        eventSourceCell_ = currentCell_;
        eventTargetCell_ = targetCell_;
        eventReproductive_ = reproductive;
        eventTransitionDuration_ = transitionDuration_;
        ++eventSerial_;
    }

    void requestDirection(
        uint32_t direction, float force, AmbiNeuralScoreMode mode,
        float transitionSeconds)
    {
        direction %= kAmbiNeuralLatticeDirections;
        force = clamp(force, 0.0f, 1.0f);
        const uint32_t verticalTarget = portalDestination(currentCell_);
        if (verticalTarget != currentCell_ && !transitioning()) {
            requestCell(verticalTarget, std::max(force, 0.68f),
                transitionSeconds);
            return;
        }
        if (mode == AmbiNeuralScoreMode::Midi) {
            requestCell(edge(currentCell_, direction), force, transitionSeconds);
        } else if (mode == AmbiNeuralScoreMode::Coupled) {
            midiBias_[direction] =
                std::max(midiBias_[direction], 0.12f + force * 1.25f);
            if (force > 0.82f) {
                requestCell(
                    edge(currentCell_, direction), force, transitionSeconds);
            }
        }
    }

    void advance(
        float seconds,
        const std::array<float, kAmbiNeuralEcologyPickups>& pickups,
        uint32_t pickupCount, AmbiNeuralScoreMode mode,
        float dwellSeconds, float transitionSeconds)
    {
        seconds = clamp(seconds, 0.0f, 1.0f);
        pickupCount = pickupCount >= 8u ? 8u : 4u;
        const float envelope = 1.0f - std::exp(-seconds / 0.36f);
        for (float& energy : pickupEnergy_) {
            energy += (0.0f - energy) * envelope;
        }
        if (pickupCount == 4u) {
            static constexpr std::array<uint32_t, 4u> direction {{
                0u, 2u, 4u, 6u
            }};
            for (uint32_t pickup = 0u; pickup < 4u; ++pickup) {
                const float heard = std::fabs(pickups[pickup]);
                pickupEnergy_[direction[pickup]] +=
                    (heard - pickupEnergy_[direction[pickup]]) * envelope;
            }
        } else {
            for (uint32_t pickup = 0u; pickup < 8u; ++pickup) {
                const float heard = std::fabs(pickups[pickup]);
                pickupEnergy_[pickup] +=
                    (heard - pickupEnergy_[pickup]) * envelope;
            }
        }
        const float biasDecay =
            std::exp(-seconds / std::max(0.5f, dwellSeconds));
        for (float& bias : midiBias_) bias *= biasDecay;

        if (transitioning()) {
            advanceTransition(seconds);
            return;
        }

        dwellElapsed_ += seconds;
        if (mode != AmbiNeuralScoreMode::Field
            && mode != AmbiNeuralScoreMode::Coupled) {
            return;
        }
        if (dwellElapsed_ < std::max(0.25f, dwellSeconds)) return;

        const uint32_t verticalTarget = portalDestination(currentCell_);
        if (verticalTarget != currentCell_) {
            requestCell(verticalTarget, 0.72f, transitionSeconds);
            midiBias_.fill(0.0f);
            ++decisionCounter_;
            return;
        }

        uint32_t bestDirection = 0u;
        float bestScore = -1.0f;
        float heardTotal = 0.0f;
        for (uint32_t direction = 0u;
            direction < kAmbiNeuralLatticeDirections; ++direction) {
            const bool available =
                pickupCount == 8u || (direction % 2u) == 0u;
            if (!available) continue;
            heardTotal += pickupEnergy_[direction];
            const float score = pickupEnergy_[direction]
                + (mode == AmbiNeuralScoreMode::Coupled
                    ? midiBias_[direction] : 0.0f)
                + 1.0e-5f
                    * static_cast<float>(
                        (decisionCounter_ + direction * 5u) % 11u);
            if (score > bestScore) {
                bestScore = score;
                bestDirection = direction;
            }
        }
        const bool portalSeeking =
            storage_.planeCount > 1u && movesOnPlane_ >= portalMoveLimit_;
        if (portalSeeking) {
            const uint32_t plane =
                currentCell_ / kAmbiNeuralLatticeCellsPerPlane;
            bestDirection = directionToward(
                currentCell_, storage_.egressCells[plane], pickupCount);
            bestScore = std::max(bestScore, 0.24f);
        }
        if (heardTotal > 1.0e-6f || bestScore > 0.05f) {
            const float force = clamp(bestScore * 3.0f, 0.15f, 1.0f);
            requestCell(
                edge(currentCell_, bestDirection), force, transitionSeconds);
            midiBias_.fill(0.0f);
            ++decisionCounter_;
        }
    }

private:
    uint32_t directionToward(
        uint32_t source, uint32_t target, uint32_t directionCount) const
    {
        const int32_t sourceX = static_cast<int32_t>(
            source % kAmbiNeuralLatticeCellsPerPlane
                % kAmbiNeuralLatticeWidth);
        const int32_t sourceY = static_cast<int32_t>(
            source % kAmbiNeuralLatticeCellsPerPlane
                / kAmbiNeuralLatticeWidth);
        const int32_t targetX = static_cast<int32_t>(
            target % kAmbiNeuralLatticeCellsPerPlane
                % kAmbiNeuralLatticeWidth);
        const int32_t targetY = static_cast<int32_t>(
            target % kAmbiNeuralLatticeCellsPerPlane
                / kAmbiNeuralLatticeWidth);
        auto wrappedStep = [](int32_t from, int32_t to) {
            int32_t delta = to - from;
            if (delta > 2) delta -= 4;
            if (delta < -2) delta += 4;
            return delta == 0 ? 0 : (delta > 0 ? 1 : -1);
        };
        int32_t dx = wrappedStep(sourceX, targetX);
        int32_t dy = wrappedStep(sourceY, targetY);
        if (directionCount < 8u && dx != 0 && dy != 0) {
            if ((decisionCounter_ & 1u) == 0u) dy = 0;
            else dx = 0;
        }
        if (dx == 0 && dy < 0) return 0u;
        if (dx > 0 && dy < 0) return 1u;
        if (dx > 0 && dy == 0) return 2u;
        if (dx > 0 && dy > 0) return 3u;
        if (dx == 0 && dy > 0) return 4u;
        if (dx < 0 && dy > 0) return 5u;
        if (dx < 0 && dy == 0) return 6u;
        if (dx < 0 && dy < 0) return 7u;
        return 0u;
    }

    void refreshPortalMoveLimit()
    {
        uint32_t value = storage_.breedingSeed
            ^ (currentCell_ / kAmbiNeuralLatticeCellsPerPlane
                * 0x9e3779b9u)
            ^ (storage_.birthCount * 0x85ebca6bu);
        value ^= value >> 16u;
        portalMoveLimit_ = 6u + value % 7u;
    }

    void appendTrail(uint32_t cell)
    {
        if (storage_.trailCount < kAmbiNeuralLatticeTrail) {
            storage_.trail[storage_.trailCount++] = cell;
            return;
        }
        for (uint32_t index = 1u;
            index < kAmbiNeuralLatticeTrail; ++index) {
            storage_.trail[index - 1u] = storage_.trail[index];
        }
        storage_.trail.back() = cell;
    }

    AmbiNeuralLatticeStorage storage_ {};
    std::array<float, kAmbiNeuralLatticeDirections> pickupEnergy_ {};
    std::array<float, kAmbiNeuralLatticeDirections> midiBias_ {};
    uint32_t currentCell_ = 5u;
    uint32_t targetCell_ = 5u;
    uint32_t decisionCounter_ = 0u;
    uint32_t eventSerial_ = 0u;
    uint32_t eventSourceCell_ = 5u;
    uint32_t eventTargetCell_ = 5u;
    bool eventReproductive_ = false;
    float eventTransitionDuration_ = 1.0f;
    float transitionProgress_ = 1.0f;
    float transitionDuration_ = 1.0f;
    float dwellElapsed_ = 0.0f;
    uint32_t movesOnPlane_ = 0u;
    uint32_t portalMoveLimit_ = 8u;
};

} // namespace s3g
