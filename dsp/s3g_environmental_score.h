#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

inline constexpr uint32_t kEnvironmentalScoreMaxEntities = 64u;

enum class EnvironmentalScoreStage : uint8_t {
    Rest = 0u,
    Accumulate,
    Initiate,
    Propagate,
    Consequence,
    Aftermath,
};

struct EnvironmentalScoreParams {
    float pace = 0.48f;
    float occupancy = 0.32f;
    float cascade = 0.52f;
    float memory = 0.66f;
    float rest = 0.58f;
};

struct EnvironmentalScoreDirective {
    EnvironmentalScoreStage stage = EnvironmentalScoreStage::Rest;
    float activity = 0.0f;
    float drive = 0.0f;
    float propagation = 0.0f;
    float consequence = 0.0f;
    float aftermath = 0.0f;
    float resource = 0.0f;
    bool arcStarted = false;
    bool onset = false;
    bool cascadeArrival = false;
    bool consequenceStarted = false;
};

// A fixed-allocation, control-rate score for environmental instruments.
// It creates finite active/rest arcs, keeps global occupancy near a target,
// and propagates events to spatial neighbours after physical travel delays.
// The score emits force and consequence directives only: instrument-specific
// entities retain ownership of their material state and sound generation.
class EnvironmentalScore {
public:
    void prepare(double sampleRate, uint32_t seed)
    {
        sampleRate_ = std::max(1000.0, sampleRate);
        seed_ = seed ? seed : 1u;
        reset(kEnvironmentalScoreMaxEntities);
    }

    void reset(uint32_t entityCount)
    {
        entityCount_ = std::clamp<uint32_t>(
            entityCount, 1u, kEnvironmentalScoreMaxEntities);
        rng_ = seed_;
        sceneRng_ = seed_ ^ 0x7f4a7c15u;
        sceneCurrent_ = {};
        sceneTarget_ = {};
        sceneRemainingSeconds_ = 4.0f;
        globalActivity_ = 0.0f;
        arcCount_ = 0u;
        cascadeCount_ = 0u;
        consequenceCount_ = 0u;
        lastScene_ = 0u;
        for (uint32_t index = 0u;
            index < kEnvironmentalScoreMaxEntities; ++index) {
            auto& entity = entities_[index];
            entity = {};
            entity.rng = seed_ ^ ((index + 1u) * 0x9e3779b9u);
            if (entity.rng == 0u) entity.rng = index + 1u;
            entity.stage = EnvironmentalScoreStage::Rest;
            entity.stageDuration = restSeconds(entity);
            entity.remaining = entity.stageDuration
                * (0.15f + randomUnit(entity.rng) * 0.85f);
            entity.resource = 0.45f + randomUnit(entity.rng) * 0.55f;
            entity.x = randomSigned(entity.rng);
            entity.y = randomSigned(entity.rng);
            entity.z = randomSigned(entity.rng);
        }
        // Start one physical arc promptly after reset so sparse scores do not
        // open with an arbitrarily long silence.
        startArc(0u, 0.62f + randomUnit(entities_[0].rng) * 0.30f,
            false, true);
        entities_[0].remaining = std::min(
            entities_[0].remaining, 0.18f);
    }

    void setParams(EnvironmentalScoreParams params)
    {
        params.pace = finiteUnit(params.pace, params_.pace);
        params.occupancy = finiteUnit(params.occupancy, params_.occupancy);
        params.cascade = finiteUnit(params.cascade, params_.cascade);
        params.memory = finiteUnit(params.memory, params_.memory);
        params.rest = finiteUnit(params.rest, params_.rest);
        params_ = params;
    }

    // Opts an instrument into a wider interpretation of the same five score
    // controls without changing their public parameter or serialized layout.
    // At zero the original score is bit-for-bit unchanged. At one, pace can
    // create true geological gaps, occupancy spans an empty-to-full field,
    // cascade can cross several neighbours, memory retains entity state, and
    // rest reaches much longer aftermaths. Cryosphere's appended aperiodic
    // lattice process uses the expanded range; established processes do not.
    void setRangeExpansion(float amount)
    {
        rangeExpansion_ = finiteUnit(amount, rangeExpansion_);
    }

    float rangeExpansion() const { return rangeExpansion_; }

    const EnvironmentalScoreParams& params() const { return params_; }

    void setEntityPosition(uint32_t index, float x, float y, float z)
    {
        if (index >= kEnvironmentalScoreMaxEntities) return;
        auto& entity = entities_[index];
        entity.x = std::isfinite(x) ? x : entity.x;
        entity.y = std::isfinite(y) ? y : entity.y;
        entity.z = std::isfinite(z) ? z : entity.z;
    }

    void update(float deltaSeconds, uint32_t entityCount)
    {
        const float dt = std::clamp(
            std::isfinite(deltaSeconds) ? deltaSeconds : 0.0f,
            0.0f, 0.25f);
        entityCount_ = std::clamp<uint32_t>(
            entityCount, 1u, kEnvironmentalScoreMaxEntities);
        if (dt <= 0.0f) return;

        updateMacroScene(dt);
        for (uint32_t index = 0u; index < entityCount_; ++index) {
            auto& directive = directives_[index];
            directive.arcStarted = false;
            directive.onset = false;
            directive.cascadeArrival = false;
            directive.consequenceStarted = false;
        }

        deliverCascades(dt);

        uint32_t activeEntities = 0u;
        for (uint32_t index = 0u; index < entityCount_; ++index) {
            if (entities_[index].stage != EnvironmentalScoreStage::Rest) {
                ++activeEntities;
            }
        }
        const float target = targetActiveEntities();
        const float homeostasis = std::clamp(
            (target - static_cast<float>(activeEntities))
                / std::max(1.0f, target),
            -0.75f, 1.0f);

        for (uint32_t index = 0u; index < entityCount_; ++index) {
            auto& entity = entities_[index];
            entity.remaining -= dt;
            if (entity.remaining <= 0.0f) {
                advanceEntity(index, activeEntities, target, homeostasis);
            }
            updateDirective(index, dt);
        }

        float activitySum = 0.0f;
        for (uint32_t index = 0u; index < entityCount_; ++index) {
            activitySum += directives_[index].activity;
        }
        const float measured = activitySum
            / static_cast<float>(entityCount_);
        const float smoothing = 1.0f - std::exp(-dt / 0.18f);
        globalActivity_ += (measured - globalActivity_) * smoothing;
    }

    // Feeds a material event back into the score. Renderers call this when a
    // branch, plate, or detached mass actually fails, allowing the physical
    // consequence to seed delayed activity in nearby entities.
    void exciteCascade(uint32_t sourceIndex, float strength)
    {
        if (sourceIndex >= entityCount_) return;
        strength = std::clamp(
            std::isfinite(strength) ? strength : 0.0f, 0.0f, 1.0f);
        if (strength <= 0.001f) return;
        auto& source = entities_[sourceIndex];
        if (randomUnit(source.rng) >= params_.cascade
                * (0.22f + strength * 0.76f)) return;
        source.intensity = std::max(source.intensity,
            0.28f + strength * 0.72f);
        scheduleCascadeNeighbours(sourceIndex, strength);
    }

    EnvironmentalScoreDirective directive(uint32_t index) const
    {
        return directives_[std::min<uint32_t>(
            index, kEnvironmentalScoreMaxEntities - 1u)];
    }

    float globalActivity() const { return globalActivity_; }
    uint32_t activeEntityCount() const
    {
        uint32_t active = 0u;
        for (uint32_t index = 0u; index < entityCount_; ++index) {
            if (entities_[index].stage != EnvironmentalScoreStage::Rest) {
                ++active;
            }
        }
        return active;
    }
    uint64_t arcCount() const { return arcCount_; }
    uint64_t cascadeCount() const { return cascadeCount_; }
    uint64_t consequenceCount() const { return consequenceCount_; }

private:
    struct MacroScene {
        float pace = 1.0f;
        float occupancy = 1.0f;
        float cascade = 1.0f;
    };

    struct Entity {
        EnvironmentalScoreStage stage = EnvironmentalScoreStage::Rest;
        uint32_t rng = 1u;
        float remaining = 1.0f;
        float stageDuration = 1.0f;
        float intensity = 0.0f;
        float resource = 0.0f;
        float pendingCascade = 0.0f;
        float cascadeDelay = 0.0f;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    static float finiteUnit(float value, float fallback)
    {
        return std::clamp(std::isfinite(value) ? value : fallback,
            0.0f, 1.0f);
    }

    static uint32_t nextRandom(uint32_t& state)
    {
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        return state;
    }

    static float randomUnit(uint32_t& state)
    {
        return static_cast<float>(nextRandom(state) & 0x00ffffffu)
            / static_cast<float>(0x01000000u);
    }

    static float randomSigned(uint32_t& state)
    {
        return randomUnit(state) * 2.0f - 1.0f;
    }

    static float smoothstep(float value)
    {
        value = std::clamp(value, 0.0f, 1.0f);
        return value * value * (3.0f - 2.0f * value);
    }

    float effectivePace() const
    {
        const float expandedScene = sceneCurrent_.pace > 0.0001f
            ? sceneCurrent_.pace : 1.0f;
        const float scene = sceneCurrent_.pace
            + (expandedScene - sceneCurrent_.pace) * rangeExpansion_;
        return std::clamp(params_.pace * scene,
            0.0f, 1.0f);
    }

    static float exponentialMap(float amount, float slow, float fast)
    {
        return std::exp(std::log(std::max(0.001f, slow))
            + (std::log(std::max(0.001f, fast))
                - std::log(std::max(0.001f, slow)))
                * std::clamp(amount, 0.0f, 1.0f));
    }

    float variedSeconds(Entity& entity, float base, float spread)
    {
        const float random = randomUnit(entity.rng);
        const float retained = 0.84f + params_.memory * 0.14f;
        const float legacyVariation = retained
            + random * spread
                * (1.0f - params_.memory * 0.62f);
        const float expandedRetained = 0.48f + params_.memory * 0.51f;
        const float expandedVariation = expandedRetained
            + random * spread
                * (1.0f - params_.memory * 0.90f);
        const float variation = legacyVariation
            + (expandedVariation - legacyVariation) * rangeExpansion_;
        return std::max(0.015f, base * variation);
    }

    float restSeconds(Entity& entity)
    {
        const float legacy = exponentialMap(effectivePace(), 24.0f, 0.18f)
            * (0.34f + params_.rest * 1.86f);
        const float occupancyCompression = 1.0f
            - std::pow(params_.occupancy, 4.0f) * 0.72f;
        const float expanded = exponentialMap(
                effectivePace(), 240.0f, 0.03f)
            * exponentialMap(params_.rest, 0.65f, 2.2f)
            * occupancyCompression;
        const float base = legacy + (expanded - legacy) * rangeExpansion_;
        return variedSeconds(entity, base, 0.78f);
    }

    float accumulationSeconds(Entity& entity)
    {
        const float legacy = exponentialMap(effectivePace(), 8.0f, 0.16f);
        const float expanded = exponentialMap(effectivePace(), 4.0f, 0.20f);
        return variedSeconds(entity,
            legacy + (expanded - legacy) * rangeExpansion_, 0.62f);
    }

    float initiationSeconds(Entity& entity)
    {
        const float legacy = 0.045f
            + (1.0f - effectivePace()) * 0.32f;
        const float expanded = 0.025f
            + (1.0f - effectivePace()) * 0.42f;
        return variedSeconds(entity,
            legacy + (expanded - legacy) * rangeExpansion_, 0.38f);
    }

    float propagationSeconds(Entity& entity)
    {
        const float legacy = exponentialMap(effectivePace(), 4.2f, 0.075f);
        const float expanded = exponentialMap(effectivePace(), 2.4f, 0.08f);
        return variedSeconds(entity,
            legacy + (expanded - legacy) * rangeExpansion_, 0.72f);
    }

    float consequenceSeconds(Entity& entity)
    {
        const float legacy = 0.10f
            + (1.0f - effectivePace()) * 0.86f
            + entity.intensity * 0.24f;
        const float expanded = 0.06f
            + (1.0f - effectivePace()) * 0.72f
            + entity.intensity * 0.18f;
        return variedSeconds(entity,
            legacy + (expanded - legacy) * rangeExpansion_, 0.48f);
    }

    float aftermathSeconds(Entity& entity)
    {
        const float legacy = exponentialMap(effectivePace(), 7.5f, 0.22f)
            * (0.40f + params_.rest * 1.24f);
        const float expanded = exponentialMap(effectivePace(), 5.5f, 0.16f)
            * exponentialMap(params_.rest, 0.04f, 8.0f);
        const float base = legacy + (expanded - legacy) * rangeExpansion_;
        return variedSeconds(entity, base, 0.68f);
    }

    float targetActiveEntities() const
    {
        const float shaped = std::pow(params_.occupancy, 1.35f)
            * sceneCurrent_.occupancy;
        const float legacy = std::clamp(1.0f + shaped
                * static_cast<float>(entityCount_ - 1u),
            1.0f, static_cast<float>(entityCount_));
        const float sceneOccupancy = sceneCurrent_.occupancy > 0.0001f
            ? sceneCurrent_.occupancy : 1.0f;
        const float sceneMix = 4.0f * params_.occupancy
            * (1.0f - params_.occupancy);
        const float expandedScene = 1.0f
            + (sceneOccupancy - 1.0f) * sceneMix;
        const float expanded = std::clamp(
            std::pow(params_.occupancy, 1.18f)
                * static_cast<float>(entityCount_) * expandedScene,
            0.0f, static_cast<float>(entityCount_));
        return legacy + (expanded - legacy) * rangeExpansion_;
    }

    void setStage(Entity& entity, EnvironmentalScoreStage stage,
        float duration)
    {
        entity.stage = stage;
        entity.stageDuration = std::max(0.015f, duration);
        entity.remaining = entity.stageDuration;
    }

    void startArc(uint32_t index, float intensity, bool cascade,
        bool initial = false)
    {
        auto& entity = entities_[index];
        entity.intensity = std::clamp(intensity, 0.12f, 1.35f);
        entity.resource = 0.52f + randomUnit(entity.rng) * 0.48f;
        setStage(entity, EnvironmentalScoreStage::Accumulate,
            accumulationSeconds(entity));
        auto& directive = directives_[index];
        directive.arcStarted = true;
        directive.cascadeArrival = cascade;
        if (!initial) ++arcCount_;
    }

    void advanceEntity(uint32_t index, uint32_t& activeEntities,
        float target, float homeostasis)
    {
        auto& entity = entities_[index];
        auto& directive = directives_[index];
        switch (entity.stage) {
        case EnvironmentalScoreStage::Rest: {
            const float vacancy = std::clamp(
                (target - static_cast<float>(activeEntities))
                    / std::max(1.0f, target),
                -1.0f, 1.0f);
            const float legacyProbability = std::clamp(
                0.10f + vacancy * 0.72f + homeostasis * 0.22f
                    + params_.occupancy * 0.16f,
                0.015f, 0.98f);
            const float expandedProbability = static_cast<float>(activeEntities)
                    >= std::ceil(target)
                ? 0.0f
                : std::clamp(0.02f + vacancy * 0.92f
                        + params_.occupancy * 0.12f,
                    0.0f, 1.0f);
            const float probability = legacyProbability
                + (expandedProbability - legacyProbability)
                    * rangeExpansion_;
            if (randomUnit(entity.rng) < probability) {
                startArc(index,
                    0.38f + randomUnit(entity.rng) * 0.72f,
                    false);
                if (rangeExpansion_ > 0.0f) ++activeEntities;
            } else {
                entity.stageDuration = std::max(0.025f,
                    restSeconds(entity)
                        * (0.12f + randomUnit(entity.rng) * 0.24f));
                entity.remaining = entity.stageDuration;
            }
            break;
        }
        case EnvironmentalScoreStage::Accumulate:
            setStage(entity, EnvironmentalScoreStage::Initiate,
                initiationSeconds(entity));
            directive.onset = true;
            break;
        case EnvironmentalScoreStage::Initiate:
            setStage(entity, EnvironmentalScoreStage::Propagate,
                propagationSeconds(entity));
            scheduleCascade(index);
            break;
        case EnvironmentalScoreStage::Propagate: {
            const float probability = std::clamp(
                0.22f + entity.intensity * 0.38f
                    + params_.cascade * 0.26f,
                0.08f, 0.96f);
            if (randomUnit(entity.rng) < probability) {
                setStage(entity, EnvironmentalScoreStage::Consequence,
                    consequenceSeconds(entity));
                directive.consequenceStarted = true;
                ++consequenceCount_;
            } else {
                setStage(entity, EnvironmentalScoreStage::Aftermath,
                    aftermathSeconds(entity));
            }
            break;
        }
        case EnvironmentalScoreStage::Consequence:
            setStage(entity, EnvironmentalScoreStage::Aftermath,
                aftermathSeconds(entity));
            break;
        case EnvironmentalScoreStage::Aftermath:
        default:
            {
                const float memoryRise = smoothstep(
                    (params_.memory - 0.66f) / 0.34f);
                const float memoryFall = std::clamp(
                    params_.memory / 0.66f, 0.0f, 1.0f);
                const float expandedRetention = params_.memory < 0.66f
                    ? 0.12f * memoryFall
                    : 0.12f + memoryRise * 0.60f;
                const float intensityRetention = 0.12f
                    + (expandedRetention - 0.12f) * rangeExpansion_;
                const float resourceRetention = memoryRise * 0.65f
                    * rangeExpansion_;
                entity.intensity *= intensityRetention;
                entity.resource *= resourceRetention;
            }
            setStage(entity, EnvironmentalScoreStage::Rest,
                restSeconds(entity));
            break;
        }
    }

    void updateDirective(uint32_t index, float dt)
    {
        auto& entity = entities_[index];
        auto& directive = directives_[index];
        const float phase = 1.0f - std::clamp(entity.remaining
            / std::max(0.015f, entity.stageDuration), 0.0f, 1.0f);
        float activity = 0.0f;
        float drive = 0.0f;
        float propagation = 0.0f;
        float consequence = 0.0f;
        float aftermath = 0.0f;
        switch (entity.stage) {
        case EnvironmentalScoreStage::Accumulate:
            activity = 0.06f + smoothstep(phase) * 0.46f;
            drive = 0.04f + smoothstep(phase) * 0.52f;
            break;
        case EnvironmentalScoreStage::Initiate:
            activity = 0.72f + entity.intensity * 0.24f;
            drive = 0.78f + entity.intensity * 0.18f;
            break;
        case EnvironmentalScoreStage::Propagate:
            activity = 0.58f + entity.intensity * 0.30f;
            drive = 0.52f + entity.intensity * 0.34f;
            propagation = (0.34f + smoothstep(phase) * 0.66f)
                * entity.intensity;
            break;
        case EnvironmentalScoreStage::Consequence:
            activity = 0.76f + entity.intensity * 0.22f;
            drive = 0.44f + entity.intensity * 0.34f;
            consequence = (0.36f + smoothstep(phase) * 0.64f)
                * entity.intensity;
            break;
        case EnvironmentalScoreStage::Aftermath:
            activity = (1.0f - smoothstep(phase))
                * (0.34f + entity.intensity * 0.28f);
            drive = activity * 0.38f;
            aftermath = activity;
            break;
        case EnvironmentalScoreStage::Rest:
        default:
            break;
        }
        entity.resource = std::max(0.0f,
            entity.resource - dt * drive
                * (0.035f + entity.intensity * 0.095f));
        const float resourceShape = 0.24f + entity.resource * 0.76f;
        directive.stage = entity.stage;
        directive.activity = std::clamp(activity * resourceShape,
            0.0f, 1.35f);
        directive.drive = std::clamp(drive * resourceShape,
            0.0f, 1.35f);
        directive.propagation = std::clamp(propagation, 0.0f, 1.35f);
        directive.consequence = std::clamp(consequence, 0.0f, 1.35f);
        directive.aftermath = std::clamp(aftermath, 0.0f, 1.0f);
        directive.resource = entity.resource;
    }

    void scheduleCascade(uint32_t sourceIndex)
    {
        auto& source = entities_[sourceIndex];
        const float probability = std::clamp(params_.cascade
                * sceneCurrent_.cascade * source.intensity,
            0.0f, 0.98f);
        if (entityCount_ < 2u
            || randomUnit(source.rng) >= probability) return;

        scheduleCascadeNeighbours(sourceIndex, 1.0f);
    }

    void scheduleCascadeNeighbours(uint32_t sourceIndex,
        float propagationStrength)
    {
        if (sourceIndex >= entityCount_ || entityCount_ < 2u) return;
        auto& source = entities_[sourceIndex];

        constexpr uint32_t kWideNeighbourCount = 8u;
        std::array<uint32_t, kWideNeighbourCount> nearest {};
        nearest.fill(sourceIndex);
        std::array<float, kWideNeighbourCount> distances {};
        distances.fill(1.0e9f);
        uint32_t farthest = sourceIndex;
        float farthestDistance = -1.0f;
        for (uint32_t target = 0u; target < entityCount_; ++target) {
            if (target == sourceIndex) continue;
            const auto& candidate = entities_[target];
            const float dx = source.x - candidate.x;
            const float dy = source.y - candidate.y;
            const float dz = source.z - candidate.z;
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance > farthestDistance) {
                farthestDistance = distance;
                farthest = target;
            }
            for (uint32_t rank = 0u; rank < kWideNeighbourCount; ++rank) {
                if (distance >= distances[rank]) continue;
                for (uint32_t shifted = kWideNeighbourCount - 1u;
                    shifted > rank; --shifted) {
                    distances[shifted] = distances[shifted - 1u];
                    nearest[shifted] = nearest[shifted - 1u];
                }
                distances[rank] = distance;
                nearest[rank] = target;
                break;
            }
        }

        scheduleCascadeTarget(source, nearest[0], distances[0],
            propagationStrength);
        if (nearest[1] != sourceIndex
            && randomUnit(source.rng) < params_.cascade * 0.58f) {
            scheduleCascadeTarget(source, nearest[1], distances[1],
                propagationStrength * 0.58f);
        }
        const float extent = rangeExpansion_ * smoothstep(
            (params_.cascade - 0.58f) / 0.42f);
        const uint32_t extraCount = static_cast<uint32_t>(
            std::lround(extent * 6.0f));
        for (uint32_t extra = 0u; extra < extraCount; ++extra) {
            const uint32_t rank = extra + 2u;
            if (nearest[rank] == sourceIndex) break;
            const float reach = extent
                * (0.96f - static_cast<float>(extra) * 0.08f);
            if (randomUnit(source.rng) < reach) {
                scheduleCascadeTarget(source, nearest[rank], distances[rank],
                    propagationStrength * (0.54f
                        - static_cast<float>(extra) * 0.045f));
            }
        }
        if (extent > 0.72f && farthest != sourceIndex
            && std::find(nearest.begin(), nearest.end(), farthest)
                == nearest.end()
            && randomUnit(source.rng) < extent) {
            scheduleCascadeTarget(source, farthest, farthestDistance,
                propagationStrength * (0.26f + extent * 0.22f));
        }
    }

    void scheduleCascadeTarget(Entity& source, uint32_t targetIndex,
        float distance, float scale)
    {
        if (targetIndex >= entityCount_) return;
        auto& target = entities_[targetIndex];
        const float expandedCascade = 0.34f + params_.cascade * 0.66f;
        const float cascadeGain = params_.cascade
            + (expandedCascade - params_.cascade) * rangeExpansion_;
        const float expandedSceneCascade = sceneCurrent_.cascade > 0.0001f
            ? sceneCurrent_.cascade : 1.0f;
        const float sceneCascade = sceneCurrent_.cascade
            + (expandedSceneCascade - sceneCurrent_.cascade)
                * rangeExpansion_;
        const float strength = std::clamp(source.intensity
                * cascadeGain * sceneCascade * scale
                * (1.0f / (1.0f + distance * 0.42f)),
            0.0f, 1.25f);
        if (strength < 0.025f) return;
        const float delay = 0.018f + distance * 0.16f
            + randomUnit(source.rng) * 0.11f;
        if (target.pendingCascade <= 0.0001f
            || delay < target.cascadeDelay) {
            target.cascadeDelay = delay;
        }
        target.pendingCascade = 1.0f
            - (1.0f - std::clamp(target.pendingCascade, 0.0f, 1.0f))
                * (1.0f - std::clamp(strength, 0.0f, 1.0f));
    }

    void deliverCascades(float dt)
    {
        for (uint32_t index = 0u; index < entityCount_; ++index) {
            auto& entity = entities_[index];
            if (entity.pendingCascade <= 0.0001f) continue;
            entity.cascadeDelay -= dt;
            if (entity.cascadeDelay > 0.0f) continue;
            const float strength = entity.pendingCascade;
            entity.pendingCascade = 0.0f;
            entity.cascadeDelay = 0.0f;
            if (entity.stage == EnvironmentalScoreStage::Rest
                || entity.stage == EnvironmentalScoreStage::Aftermath) {
                startArc(index, 0.28f + strength * 0.82f, true);
                entity.remaining *= 0.34f + params_.memory * 0.28f;
            } else {
                entity.intensity = std::clamp(
                    entity.intensity + strength * 0.42f, 0.0f, 1.35f);
                directives_[index].cascadeArrival = true;
                if (entity.stage == EnvironmentalScoreStage::Accumulate) {
                    entity.remaining = std::min(entity.remaining,
                        entity.stageDuration * 0.24f);
                }
            }
            ++cascadeCount_;
        }
    }

    void updateMacroScene(float dt)
    {
        sceneRemainingSeconds_ -= dt;
        if (sceneRemainingSeconds_ <= 0.0f) {
            static constexpr std::array<MacroScene, 6> scenes {{
                { 0.56f, 0.62f, 0.38f },
                { 1.24f, 1.28f, 1.34f },
                { 0.72f, 0.86f, 1.42f },
                { 1.34f, 0.72f, 0.56f },
                { 0.88f, 1.12f, 0.92f },
                { 0.48f, 0.52f, 0.28f },
            }};
            uint32_t selected = nextRandom(sceneRng_)
                % static_cast<uint32_t>(scenes.size());
            if (selected == lastScene_ && params_.memory < 0.92f) {
                selected = (selected + 1u
                    + nextRandom(sceneRng_) % (scenes.size() - 1u))
                    % scenes.size();
            }
            lastScene_ = selected;
            const auto candidate = scenes[selected];
            const float legacyReach = 0.22f
                + (1.0f - params_.memory) * 0.70f;
            const float expandedReach = 0.03f
                + 0.97f * (1.0f
                    - std::pow(params_.memory, 1.4f));
            const float reach = legacyReach
                + (expandedReach - legacyReach) * rangeExpansion_;
            const float lowerSceneBound = 0.42f
                + (0.18f - 0.42f) * rangeExpansion_;
            const float upperPaceBound = 1.42f
                + (1.82f - 1.42f) * rangeExpansion_;
            const float upperOccupancyBound = 1.38f
                + (1.72f - 1.38f) * rangeExpansion_;
            const float lowerCascadeBound = 0.24f
                + (0.08f - 0.24f) * rangeExpansion_;
            const float upperCascadeBound = 1.48f
                + (1.92f - 1.48f) * rangeExpansion_;
            sceneTarget_.pace = std::clamp(
                sceneCurrent_.pace
                    + (candidate.pace - sceneCurrent_.pace) * reach,
                lowerSceneBound, upperPaceBound);
            sceneTarget_.occupancy = std::clamp(
                sceneCurrent_.occupancy
                    + (candidate.occupancy - sceneCurrent_.occupancy) * reach,
                lowerSceneBound, upperOccupancyBound);
            sceneTarget_.cascade = std::clamp(
                sceneCurrent_.cascade
                    + (candidate.cascade - sceneCurrent_.cascade) * reach,
                lowerCascadeBound, upperCascadeBound);
            const float legacyMacroBase = exponentialMap(params_.pace,
                52.0f, 7.0f);
            const float expandedMacroBase = exponentialMap(params_.pace,
                120.0f, 3.0f);
            const float macroBase = legacyMacroBase
                + (expandedMacroBase - legacyMacroBase) * rangeExpansion_;
            const float legacyMemoryScale = 0.76f
                + params_.memory * 0.48f;
            const float expandedMemoryScale = exponentialMap(
                params_.memory, 0.18f, 3.2f);
            const float memoryScale = legacyMemoryScale
                + (expandedMemoryScale - legacyMemoryScale)
                    * rangeExpansion_;
            sceneRemainingSeconds_ += macroBase
                * (0.72f + randomUnit(sceneRng_) * 0.76f)
                * memoryScale;
        }
        const float legacyTime = 0.8f + params_.memory * 3.2f;
        const float expandedTime = exponentialMap(params_.memory,
            0.10f, 18.0f);
        const float smoothingTime = legacyTime
            + (expandedTime - legacyTime) * rangeExpansion_;
        const float smoothing = 1.0f - std::exp(-dt / smoothingTime);
        sceneCurrent_.pace +=
            (sceneTarget_.pace - sceneCurrent_.pace) * smoothing;
        sceneCurrent_.occupancy +=
            (sceneTarget_.occupancy - sceneCurrent_.occupancy) * smoothing;
        sceneCurrent_.cascade +=
            (sceneTarget_.cascade - sceneCurrent_.cascade) * smoothing;
    }

    EnvironmentalScoreParams params_ {};
    std::array<Entity, kEnvironmentalScoreMaxEntities> entities_ {};
    std::array<EnvironmentalScoreDirective,
        kEnvironmentalScoreMaxEntities> directives_ {};
    double sampleRate_ = 48000.0;
    uint32_t seed_ = 1u;
    uint32_t rng_ = 1u;
    uint32_t sceneRng_ = 1u;
    uint32_t entityCount_ = 1u;
    uint32_t lastScene_ = 0u;
    MacroScene sceneCurrent_ {};
    MacroScene sceneTarget_ {};
    float sceneRemainingSeconds_ = 4.0f;
    float globalActivity_ = 0.0f;
    float rangeExpansion_ = 0.0f;
    uint64_t arcCount_ = 0u;
    uint64_t cascadeCount_ = 0u;
    uint64_t consequenceCount_ = 0u;
};

} // namespace s3g
