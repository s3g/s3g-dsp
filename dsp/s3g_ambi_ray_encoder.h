#pragma once

#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace s3g {

constexpr uint32_t kAmbiRayFormatVersion = 1u;
constexpr uint32_t kAmbiRayMaxOrder = 7u;
constexpr uint32_t kAmbiRayMaxChannels = 64u;
constexpr uint32_t kAmbiRayRoomOrder = 3u;
constexpr uint32_t kAmbiRayRoomChannels = 16u;
constexpr uint32_t kAmbiRayMaxCells = 256u;
constexpr uint32_t kAmbiRayMaxReflections = 32u;
constexpr uint32_t kAmbiRayLateLines = 8u;
constexpr float kAmbiRayMaximumDelaySeconds = 6.0f;

struct AmbiRayReflection {
    uint32_t slot = 0u;
    float delayMs = 20.0f;
    float gain = 0.1f;
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float damping = 0.25f;
    Vec3 bouncePositionMetres {};
    bool hasBouncePosition = false;
};

struct AmbiRayLateProfile {
    float startMs = 45.0f;
    float decaySeconds = 1.8f;
    float level = 0.18f;
    float diffusion = 0.72f;
    float damping = 0.38f;
};

struct AmbiRayCell {
    Vec3 positionMetres { 4.0f, 3.0f, 1.4f };
    std::vector<AmbiRayReflection> reflections;
    AmbiRayLateProfile late {};
};

struct AmbiRayRoom {
    float widthMetres = 8.0f;
    float depthMetres = 10.0f;
    float heightMetres = 3.0f;
    Vec3 navigationMinimumMetres { 0.25f, 0.25f, 0.15f };
    Vec3 navigationMaximumMetres { 7.75f, 9.75f, 2.85f };
    std::vector<std::array<float, 2>> polygon;
    std::vector<std::array<float, 2>> ceilingProfile;
};

struct AmbiRayDescriptor {
    uint32_t version = kAmbiRayFormatVersion;
    float durationSeconds = 3.0f;
    AmbiRayRoom room {};
    Vec3 listenerPositionMetres { 4.0f, 5.0f, 1.5f };
    Vec3 defaultSourcePositionMetres { 4.0f, 2.5f, 1.4f };
    std::vector<AmbiRayCell> cells;
};

struct AmbiRayEncoderParams {
    uint32_t order = 3u;
    float sourceX = 0.5f;
    float sourceY = 0.25f;
    float sourceZ = 0.5f;
    float direct = 1.0f;
    float early = 0.72f;
    float late = 0.42f;
    float size = 1.0f;
    float scatter = 0.45f;
    float width = 1.0f;
    float air = 0.20f;
    float movementMs = 60.0f;
    float outputGainDb = -6.0f;
    bool bypassRoom = false;
};

inline AmbiRayEncoderParams sanitizeAmbiRayEncoderParams(AmbiRayEncoderParams params)
{
    params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiRayMaxOrder);
    params.sourceX = clamp(params.sourceX, 0.0f, 1.0f);
    params.sourceY = clamp(params.sourceY, 0.0f, 1.0f);
    params.sourceZ = clamp(params.sourceZ, 0.0f, 1.0f);
    params.direct = clamp(params.direct, 0.0f, 1.5f);
    params.early = clamp(params.early, 0.0f, 1.5f);
    params.late = clamp(params.late, 0.0f, 1.5f);
    params.size = clamp(params.size, 0.5f, 2.0f);
    params.scatter = clamp(params.scatter, 0.0f, 1.0f);
    params.width = clamp(params.width, 0.0f, 1.5f);
    params.air = clamp(params.air, 0.0f, 1.0f);
    params.movementMs = clamp(params.movementMs, 10.0f, 500.0f);
    params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
    return params;
}

namespace ambi_ray_detail {

inline float finiteOr(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

inline Vec3 finiteVec(Vec3 value, Vec3 fallback)
{
    value.x = finiteOr(value.x, fallback.x);
    value.y = finiteOr(value.y, fallback.y);
    value.z = finiteOr(value.z, fallback.z);
    return value;
}

inline float component(float x, float y, float amount)
{
    return x + (y - x) * amount;
}

inline Vec3 mixVec(Vec3 a, Vec3 b, float amount)
{
    return {
        component(a.x, b.x, amount),
        component(a.y, b.y, amount),
        component(a.z, b.z, amount)
    };
}

inline float distance(Vec3 a, Vec3 b)
{
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    const float z = a.z - b.z;
    return std::sqrt(x * x + y * y + z * z);
}

inline std::array<float, 2> aedFromWorldVector(Vec3 world)
{
    const float length = std::max(0.000001f, std::sqrt(world.x * world.x + world.y * world.y + world.z * world.z));
    return {
        std::atan2(world.x, world.y) * 180.0f / kPi,
        std::asin(clamp(world.z / length, -1.0f, 1.0f)) * 180.0f / kPi
    };
}

inline float normalizedPosition(float value, float minimum, float maximum)
{
    return clamp((value - minimum) / std::max(0.0001f, maximum - minimum), 0.0f, 1.0f);
}

inline Vec3 positionFromNormalized(const AmbiRayDescriptor& descriptor, Vec3 normalized)
{
    const auto& minimum = descriptor.room.navigationMinimumMetres;
    const auto& maximum = descriptor.room.navigationMaximumMetres;
    return {
        component(minimum.x, maximum.x, clamp(normalized.x, 0.0f, 1.0f)),
        component(minimum.y, maximum.y, clamp(normalized.y, 0.0f, 1.0f)),
        component(minimum.z, maximum.z, clamp(normalized.z, 0.0f, 1.0f))
    };
}

inline Vec3 normalizedFromPosition(const AmbiRayDescriptor& descriptor, Vec3 position)
{
    const auto& minimum = descriptor.room.navigationMinimumMetres;
    const auto& maximum = descriptor.room.navigationMaximumMetres;
    return {
        normalizedPosition(position.x, minimum.x, maximum.x),
        normalizedPosition(position.y, minimum.y, maximum.y),
        normalizedPosition(position.z, minimum.z, maximum.z)
    };
}

class FractionalDelay {
public:
    void prepare(double sampleRate, float maximumSeconds)
    {
        const uint32_t frames = std::max<uint32_t>(8u,
            static_cast<uint32_t>(std::ceil(std::max(0.01f, maximumSeconds) * sampleRate)) + 4u);
        data_.assign(frames, 0.0f);
        position_ = 0u;
    }

    void reset()
    {
        std::fill(data_.begin(), data_.end(), 0.0f);
        position_ = 0u;
    }

    void write(float value)
    {
        if (!data_.empty()) data_[position_] = flushDenormal(value);
    }

    float read(float delayFrames) const
    {
        if (data_.size() < 4u) return 0.0f;
        delayFrames = clamp(delayFrames, 0.0f, static_cast<float>(data_.size() - 3u));
        float readPosition = static_cast<float>(position_) - delayFrames;
        while (readPosition < 0.0f) readPosition += static_cast<float>(data_.size());
        while (readPosition >= static_cast<float>(data_.size())) readPosition -= static_cast<float>(data_.size());
        const uint32_t first = static_cast<uint32_t>(readPosition);
        const uint32_t second = (first + 1u) % static_cast<uint32_t>(data_.size());
        return component(data_[first], data_[second], readPosition - static_cast<float>(first));
    }

    void advance()
    {
        if (!data_.empty()) position_ = (position_ + 1u) % static_cast<uint32_t>(data_.size());
    }

private:
    std::vector<float> data_;
    uint32_t position_ = 0u;
};

inline void hadamard8(const std::array<float, kAmbiRayLateLines>& input,
                      std::array<float, kAmbiRayLateLines>& output)
{
    const float a0 = input[0] + input[1];
    const float a1 = input[0] - input[1];
    const float a2 = input[2] + input[3];
    const float a3 = input[2] - input[3];
    const float a4 = input[4] + input[5];
    const float a5 = input[4] - input[5];
    const float a6 = input[6] + input[7];
    const float a7 = input[6] - input[7];
    const float b0 = a0 + a2;
    const float b1 = a1 + a3;
    const float b2 = a0 - a2;
    const float b3 = a1 - a3;
    const float b4 = a4 + a6;
    const float b5 = a5 + a7;
    const float b6 = a4 - a6;
    const float b7 = a5 - a7;
    constexpr float scale = 0.35355339059f;
    output = {
        (b0 + b4) * scale,
        (b1 + b5) * scale,
        (b2 + b6) * scale,
        (b3 + b7) * scale,
        (b0 - b4) * scale,
        (b1 - b5) * scale,
        (b2 - b6) * scale,
        (b3 - b7) * scale
    };
}

class LateField {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        const uint32_t frames = static_cast<uint32_t>(std::ceil(sampleRate_ * 0.85)) + 4u;
        for (auto& line : lines_) line.assign(frames, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& line : lines_) std::fill(line.begin(), line.end(), 0.0f);
        filter_.fill(0.0f);
        position_ = 0u;
    }

    void setShape(float decaySeconds, float damping, float diffusion, float size)
    {
        decaySeconds = clamp(decaySeconds, 0.08f, 12.0f);
        damping = clamp(damping, 0.0f, 1.0f);
        diffusion_ = clamp(diffusion, 0.0f, 1.0f);
        size = clamp(size, 0.5f, 2.0f);
        constexpr std::array<float, kAmbiRayLateLines> baseMs {
            43.7f, 53.9f, 61.1f, 71.9f, 83.3f, 97.1f, 109.7f, 127.9f
        };
        const uint32_t maximumFrames = lines_[0].empty() ? 1u : static_cast<uint32_t>(lines_[0].size() - 3u);
        for (uint32_t line = 0; line < kAmbiRayLateLines; ++line) {
            delayFrames_[line] = std::min<float>(maximumFrames, baseMs[line] * 0.001f * static_cast<float>(sampleRate_) * size);
            const float delaySeconds = delayFrames_[line] / static_cast<float>(sampleRate_);
            feedback_[line] = clamp(std::pow(0.001f, delaySeconds / decaySeconds), 0.0f, 0.9975f);
        }
        const float cutoff = 650.0f + 18350.0f * std::pow(1.0f - damping, 2.2f);
        filterCoefficient_ = clamp(1.0f - std::exp(-2.0f * kPi * cutoff / static_cast<float>(sampleRate_)), 0.001f, 1.0f);
    }

    std::array<float, kAmbiRayLateLines> process(float input)
    {
        std::array<float, kAmbiRayLateLines> output {};
        if (lines_[0].empty()) return output;
        for (uint32_t line = 0; line < kAmbiRayLateLines; ++line) {
            const float size = static_cast<float>(lines_[line].size());
            float readPosition = static_cast<float>(position_) - delayFrames_[line];
            while (readPosition < 0.0f) readPosition += size;
            const uint32_t first = static_cast<uint32_t>(readPosition) % static_cast<uint32_t>(lines_[line].size());
            const uint32_t second = (first + 1u) % static_cast<uint32_t>(lines_[line].size());
            const float delayed = component(lines_[line][first], lines_[line][second], readPosition - std::floor(readPosition));
            filter_[line] += (delayed - filter_[line]) * filterCoefficient_;
            output[line] = flushDenormal(filter_[line]);
        }
        std::array<float, kAmbiRayLateLines> mixed {};
        hadamard8(output, mixed);
        constexpr std::array<float, kAmbiRayLateLines> injection { 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f };
        for (uint32_t line = 0; line < kAmbiRayLateLines; ++line) {
            const float feedbackSignal = component(output[line], mixed[line], diffusion_) * feedback_[line];
            lines_[line][position_] = flushDenormal(input * injection[line] * 0.24f + feedbackSignal);
        }
        position_ = (position_ + 1u) % static_cast<uint32_t>(lines_[0].size());
        return output;
    }

private:
    double sampleRate_ = 48000.0;
    std::array<std::vector<float>, kAmbiRayLateLines> lines_;
    std::array<float, kAmbiRayLateLines> delayFrames_ {};
    std::array<float, kAmbiRayLateLines> feedback_ {};
    std::array<float, kAmbiRayLateLines> filter_ {};
    float filterCoefficient_ = 0.5f;
    float diffusion_ = 0.7f;
    uint32_t position_ = 0u;
};

} // namespace ambi_ray_detail

inline AmbiRayDescriptor sanitizeAmbiRayDescriptor(AmbiRayDescriptor descriptor)
{
    descriptor.version = kAmbiRayFormatVersion;
    descriptor.durationSeconds = clamp(ambi_ray_detail::finiteOr(descriptor.durationSeconds, 3.0f), 0.1f, 12.0f);
    auto& room = descriptor.room;
    room.widthMetres = clamp(ambi_ray_detail::finiteOr(room.widthMetres, 8.0f), 0.5f, 1000.0f);
    room.depthMetres = clamp(ambi_ray_detail::finiteOr(room.depthMetres, 10.0f), 0.5f, 1000.0f);
    room.heightMetres = clamp(ambi_ray_detail::finiteOr(room.heightMetres, 3.0f), 0.5f, 1000.0f);
    room.navigationMinimumMetres = ambi_ray_detail::finiteVec(room.navigationMinimumMetres, { 0.0f, 0.0f, 0.0f });
    room.navigationMaximumMetres = ambi_ray_detail::finiteVec(room.navigationMaximumMetres,
        { room.widthMetres, room.depthMetres, room.heightMetres });
    if (room.navigationMaximumMetres.x < room.navigationMinimumMetres.x + 0.01f)
        room.navigationMaximumMetres.x = room.navigationMinimumMetres.x + 0.01f;
    if (room.navigationMaximumMetres.y < room.navigationMinimumMetres.y + 0.01f)
        room.navigationMaximumMetres.y = room.navigationMinimumMetres.y + 0.01f;
    if (room.navigationMaximumMetres.z < room.navigationMinimumMetres.z + 0.01f)
        room.navigationMaximumMetres.z = room.navigationMinimumMetres.z + 0.01f;
    if (room.polygon.size() > 64u) room.polygon.resize(64u);
    if (room.ceilingProfile.size() > 64u) room.ceilingProfile.resize(64u);
    for (auto& point : room.polygon) {
        point[0] = clamp(ambi_ray_detail::finiteOr(point[0], 0.0f), -1000.0f, 1000.0f);
        point[1] = clamp(ambi_ray_detail::finiteOr(point[1], 0.0f), -1000.0f, 1000.0f);
    }
    for (auto& point : room.ceilingProfile) {
        point[0] = clamp(ambi_ray_detail::finiteOr(point[0], 0.0f), -1000.0f, 1000.0f);
        point[1] = clamp(ambi_ray_detail::finiteOr(point[1], room.heightMetres), -1000.0f, 1000.0f);
    }
    descriptor.listenerPositionMetres = ambi_ray_detail::finiteVec(descriptor.listenerPositionMetres,
        { room.widthMetres * 0.5f, room.depthMetres * 0.5f, room.heightMetres * 0.5f });
    descriptor.defaultSourcePositionMetres = ambi_ray_detail::finiteVec(descriptor.defaultSourcePositionMetres,
        { room.widthMetres * 0.5f, room.depthMetres * 0.25f, room.heightMetres * 0.5f });
    if (descriptor.cells.size() > kAmbiRayMaxCells) descriptor.cells.resize(kAmbiRayMaxCells);
    for (auto& cell : descriptor.cells) {
        cell.positionMetres = ambi_ray_detail::finiteVec(cell.positionMetres, descriptor.defaultSourcePositionMetres);
        if (cell.reflections.size() > kAmbiRayMaxReflections) cell.reflections.resize(kAmbiRayMaxReflections);
        std::stable_sort(cell.reflections.begin(), cell.reflections.end(), [](const auto& a, const auto& b) {
            return a.slot < b.slot;
        });
        for (uint32_t index = 0u; index < cell.reflections.size(); ++index) {
            auto& event = cell.reflections[index];
            event.slot = std::min<uint32_t>(event.slot, kAmbiRayMaxReflections - 1u);
            event.delayMs = clamp(ambi_ray_detail::finiteOr(event.delayMs, 20.0f), 0.0f, kAmbiRayMaximumDelaySeconds * 1000.0f);
            event.gain = clamp(ambi_ray_detail::finiteOr(event.gain, 0.0f), -2.0f, 2.0f);
            event.azimuthDeg = std::remainder(ambi_ray_detail::finiteOr(event.azimuthDeg, 0.0f), 360.0f);
            event.elevationDeg = clamp(ambi_ray_detail::finiteOr(event.elevationDeg, 0.0f), -90.0f, 90.0f);
            event.damping = clamp(ambi_ray_detail::finiteOr(event.damping, 0.25f), 0.0f, 1.0f);
            if (event.hasBouncePosition) {
                event.hasBouncePosition = std::isfinite(event.bouncePositionMetres.x)
                    && std::isfinite(event.bouncePositionMetres.y)
                    && std::isfinite(event.bouncePositionMetres.z);
                if (event.hasBouncePosition) {
                    event.bouncePositionMetres.x = clamp(event.bouncePositionMetres.x, -1000.0f, 1000.0f);
                    event.bouncePositionMetres.y = clamp(event.bouncePositionMetres.y, -1000.0f, 1000.0f);
                    event.bouncePositionMetres.z = clamp(event.bouncePositionMetres.z, -1000.0f, 1000.0f);
                }
            }
        }
        auto& late = cell.late;
        late.startMs = clamp(ambi_ray_detail::finiteOr(late.startMs, 45.0f), 0.0f, kAmbiRayMaximumDelaySeconds * 1000.0f);
        late.decaySeconds = clamp(ambi_ray_detail::finiteOr(late.decaySeconds, 1.8f), 0.08f, 12.0f);
        late.level = clamp(ambi_ray_detail::finiteOr(late.level, 0.18f), 0.0f, 1.5f);
        late.diffusion = clamp(ambi_ray_detail::finiteOr(late.diffusion, 0.72f), 0.0f, 1.0f);
        late.damping = clamp(ambi_ray_detail::finiteOr(late.damping, 0.38f), 0.0f, 1.0f);
    }
    return descriptor;
}

inline AmbiRayDescriptor makeDefaultAmbiRayDescriptor()
{
    AmbiRayDescriptor descriptor;
    descriptor.room.polygon = { { 0.0f, 0.0f }, { 8.0f, 0.0f }, { 8.0f, 10.0f }, { 0.0f, 10.0f } };
    descriptor.room.ceilingProfile = { { 0.0f, 3.0f }, { 8.0f, 3.0f } };
    constexpr std::array<float, 2> xs { 1.5f, 6.5f };
    constexpr std::array<float, 2> ys { 1.5f, 8.5f };
    constexpr std::array<float, 2> zs { 0.9f, 2.1f };
    for (float z : zs) {
        for (float y : ys) {
            for (float x : xs) {
                AmbiRayCell cell;
                cell.positionMetres = { x, y, z };
                const std::array<Vec3, 6> images {{
                    { -x, y, z }, { 16.0f - x, y, z },
                    { x, -y, z }, { x, 20.0f - y, z },
                    { x, y, -z }, { x, y, 6.0f - z }
                }};
                const auto listener = descriptor.listenerPositionMetres;
                const auto bounceOnPlane = [listener](Vec3 image, uint32_t axis, float plane) {
                    const float start = axis == 0u ? listener.x : axis == 1u ? listener.y : listener.z;
                    const float finish = axis == 0u ? image.x : axis == 1u ? image.y : image.z;
                    const float delta = finish - start;
                    const float amount = clamp((plane - start) / (std::abs(delta) > 0.000001f ? delta : 0.000001f), 0.0f, 1.0f);
                    Vec3 bounce = ambi_ray_detail::mixVec(listener, image, amount);
                    if (axis == 0u) bounce.x = plane;
                    else if (axis == 1u) bounce.y = plane;
                    else bounce.z = plane;
                    return bounce;
                };
                const std::array<Vec3, 6> bounces {{
                    bounceOnPlane(images[0], 0u, 0.0f), bounceOnPlane(images[1], 0u, 8.0f),
                    bounceOnPlane(images[2], 1u, 0.0f), bounceOnPlane(images[3], 1u, 10.0f),
                    bounceOnPlane(images[4], 2u, 0.0f), bounceOnPlane(images[5], 2u, 3.0f)
                }};
                for (uint32_t index = 0u; index < images.size(); ++index) {
                    const Vec3 offset {
                        images[index].x - descriptor.listenerPositionMetres.x,
                        images[index].y - descriptor.listenerPositionMetres.y,
                        images[index].z - descriptor.listenerPositionMetres.z
                    };
                    const float path = std::max(0.1f, std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z));
                    const auto aed = ambi_ray_detail::aedFromWorldVector(offset);
                    cell.reflections.push_back({ index, path / 343.0f * 1000.0f,
                        (index < 4u ? 0.62f : 0.48f) / std::max(1.0f, path), aed[0], aed[1],
                        index < 4u ? 0.24f : 0.35f, bounces[index], true });
                }
                cell.late.decaySeconds = 1.65f + 0.25f * (y / 10.0f);
                cell.late.level = 0.18f;
                cell.late.diffusion = 0.74f;
                cell.late.damping = 0.36f;
                descriptor.cells.push_back(std::move(cell));
            }
        }
    }
    return sanitizeAmbiRayDescriptor(std::move(descriptor));
}

class AmbiRayEncoder {
public:
    bool prepare(double sampleRate, AmbiRayDescriptor descriptor)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        descriptor_ = sanitizeAmbiRayDescriptor(std::move(descriptor));
        if (descriptor_.cells.empty()) descriptor_ = makeDefaultAmbiRayDescriptor();
        delay_.prepare(sampleRate_, kAmbiRayMaximumDelaySeconds * 2.05f);
        roomDelay_.prepare(sampleRate_, kAmbiRayMaximumDelaySeconds * 2.05f);
        lateField_.prepare(sampleRate_);
        params_ = sanitizeAmbiRayEncoderParams(params_);
        reset();
        return !descriptor_.cells.empty();
    }

    void reset()
    {
        delay_.reset();
        roomDelay_.reset();
        lateField_.reset();
        dcInput_ = 0.0f;
        dcOutput_ = 0.0f;
        directFilter_ = 0.0f;
        eventFilters_.fill(0.0f);
        safetyGain_ = 1.0f;
        frame_.fill(0.0f);
        currentNormalized_ = { params_.sourceX, params_.sourceY, params_.sourceZ };
        currentDirect_ = params_.direct;
        currentEarly_ = params_.early;
        currentLate_ = params_.late;
        currentWidth_ = params_.width;
        currentOutput_ = dbToGain(params_.outputGainDb);
        currentSize_ = params_.size;
        currentScatter_ = params_.scatter;
        currentAir_ = params_.air;
        currentLateStartMs_ = 45.0f;
        currentLateDecay_ = 1.8f;
        currentLateLevel_ = 0.18f;
        currentLateDiffusion_ = 0.72f;
        currentLateDamping_ = 0.38f;
        for (auto& event : events_) event = RuntimeEvent {};
        updateControl(1u, true);
    }

    void setParams(AmbiRayEncoderParams params)
    {
        params_ = sanitizeAmbiRayEncoderParams(params);
    }

    const AmbiRayEncoderParams& params() const { return params_; }
    const AmbiRayDescriptor& descriptor() const { return descriptor_; }

    Vec3 sourcePositionMetres() const
    {
        return ambi_ray_detail::positionFromNormalized(descriptor_, currentNormalized_);
    }

    Vec3 targetSourcePositionMetres() const
    {
        return ambi_ray_detail::positionFromNormalized(descriptor_, { params_.sourceX, params_.sourceY, params_.sourceZ });
    }

    static Vec3 normalizedSourcePosition(const AmbiRayDescriptor& descriptor, Vec3 position)
    {
        return ambi_ray_detail::normalizedFromPosition(descriptor, position);
    }

    uint32_t tailFrames() const
    {
        float maximumDecay = 0.0f;
        for (const auto& cell : descriptor_.cells) maximumDecay = std::max(maximumDecay, cell.late.decaySeconds);
        return static_cast<uint32_t>(std::ceil(std::min(12.0f, maximumDecay * params_.size) * sampleRate_));
    }

    template <typename Sample>
    void process(const Sample* input, Sample** outputs, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiRayMaxChannels);
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            if (outputs[channel]) std::fill(outputs[channel], outputs[channel] + frames, Sample {});
        }
        constexpr uint32_t controlFrames = 32u;
        for (uint32_t chunkStart = 0u; chunkStart < frames; chunkStart += controlFrames) {
            const uint32_t chunkSize = std::min<uint32_t>(controlFrames, frames - chunkStart);
            updateControl(chunkSize, false);
            const uint32_t activeChannels = std::min<uint32_t>(outputChannels, ambiChannelsForOrder(params_.order));
            const uint32_t roomChannels = std::min<uint32_t>(activeChannels, kAmbiRayRoomChannels);
            for (uint32_t frame = chunkStart; frame < chunkStart + chunkSize; ++frame) {
                float in = input ? static_cast<float>(input[frame]) : 0.0f;
                if (!std::isfinite(in)) in = 0.0f;
                delay_.write(in);
                const float wetInput = flushDenormal(in - dcInput_ + 0.9975f * dcOutput_);
                dcInput_ = in;
                dcOutput_ = wetInput;
                roomDelay_.write(wetInput);
                frame_.fill(0.0f);

                const float directSample = delay_.read(directDelayFrames_);
                directFilter_ += (directSample - directFilter_) * directFilterCoefficient_;
                const float direct = directFilter_ * directDistanceGain_ * currentDirect_;
                for (uint32_t channel = 0u; channel < activeChannels; ++channel) {
                    frame_[channel] += direct * directBasis_[channel];
                }

                if (!params_.bypassRoom) {
                    for (uint32_t eventIndex = 0u; eventIndex < activeEventCount_; ++eventIndex) {
                        const auto& event = events_[eventIndex];
                        const float reflected = roomDelay_.read(event.delayFrames);
                        eventFilters_[eventIndex] += (reflected - eventFilters_[eventIndex]) * event.filterCoefficient;
                        const float value = eventFilters_[eventIndex] * event.gain * currentEarly_;
                        for (uint32_t channel = 0u; channel < roomChannels; ++channel) {
                            frame_[channel] += value * event.basis[channel] * roomOrderScale_[channel];
                        }
                    }

                    const float lateExcitation = roomDelay_.read(currentLateStartMs_ * 0.001f * static_cast<float>(sampleRate_) * currentSize_);
                    const auto lateLines = lateField_.process(lateExcitation);
                    const float lateGain = currentLate_ * currentLateLevel_ * 0.52f;
                    for (uint32_t line = 0u; line < kAmbiRayLateLines; ++line) {
                        const float value = lateLines[line] * lateGain;
                        for (uint32_t channel = 0u; channel < roomChannels; ++channel) {
                            frame_[channel] += value * lateBasis_[line][channel] * lateOrderScale_[channel];
                        }
                    }
                } else {
                    lateField_.process(0.0f);
                }

                float peak = 0.0f;
                for (uint32_t channel = 0u; channel < activeChannels; ++channel) {
                    const uint32_t order = static_cast<uint32_t>(std::sqrt(static_cast<float>(channel)));
                    const float width = order == 0u ? 1.0f
                        : std::pow(std::max(0.0f, currentWidth_), static_cast<float>(order) / static_cast<float>(params_.order));
                    frame_[channel] *= width * currentOutput_;
                    if (!std::isfinite(frame_[channel])) frame_[channel] = 0.0f;
                    peak = std::max(peak, std::abs(frame_[channel]));
                }
                const float safetyTarget = peak > 0.98f ? 0.98f / peak : 1.0f;
                if (safetyTarget < safetyGain_) safetyGain_ = safetyTarget;
                else safetyGain_ += (safetyTarget - safetyGain_) * safetyRelease_;
                for (uint32_t channel = 0u; channel < activeChannels; ++channel) {
                    if (outputs[channel]) outputs[channel][frame] = static_cast<Sample>(
                        std::clamp(frame_[channel] * safetyGain_, -8.0f, 8.0f));
                }
                delay_.advance();
                roomDelay_.advance();
            }
        }
    }

private:
    struct RuntimeEvent {
        float delayFrames = 0.0f;
        float gain = 0.0f;
        Vec3 direction { 1.0f, 0.0f, 0.0f };
        float damping = 0.25f;
        float filterCoefficient = 0.5f;
        std::array<float, kAmbiRayMaxChannels> basis {};
    };

    struct DesiredEvent {
        float delayMs = 0.0f;
        float gain = 0.0f;
        Vec3 direction {};
        float damping = 0.0f;
        float weight = 0.0f;
    };

    struct NearbyCell {
        uint32_t index = 0u;
        float distanceSquared = std::numeric_limits<float>::max();
        float weight = 0.0f;
    };

    std::array<NearbyCell, 4u> nearbyCells(Vec3 source) const
    {
        std::array<NearbyCell, 4u> result {};
        const Vec3 span {
            std::max(0.01f, descriptor_.room.navigationMaximumMetres.x - descriptor_.room.navigationMinimumMetres.x),
            std::max(0.01f, descriptor_.room.navigationMaximumMetres.y - descriptor_.room.navigationMinimumMetres.y),
            std::max(0.01f, descriptor_.room.navigationMaximumMetres.z - descriptor_.room.navigationMinimumMetres.z)
        };
        for (uint32_t cell = 0u; cell < descriptor_.cells.size(); ++cell) {
            const Vec3 delta {
                (source.x - descriptor_.cells[cell].positionMetres.x) / span.x,
                (source.y - descriptor_.cells[cell].positionMetres.y) / span.y,
                (source.z - descriptor_.cells[cell].positionMetres.z) / span.z
            };
            NearbyCell candidate { cell, delta.x * delta.x + delta.y * delta.y + delta.z * delta.z, 0.0f };
            for (uint32_t slot = 0u; slot < result.size(); ++slot) {
                if (candidate.distanceSquared >= result[slot].distanceSquared) continue;
                for (uint32_t move = static_cast<uint32_t>(result.size() - 1u); move > slot; --move) result[move] = result[move - 1u];
                result[slot] = candidate;
                break;
            }
        }
        if (result[0].distanceSquared < 1.0e-10f) {
            result[0].weight = 1.0f;
            for (uint32_t index = 1u; index < result.size(); ++index) result[index].weight = 0.0f;
            return result;
        }
        float sum = 0.0f;
        for (auto& cell : result) {
            if (cell.distanceSquared == std::numeric_limits<float>::max()) {
                cell.weight = 0.0f;
                continue;
            }
            cell.weight = 1.0f / std::pow(std::max(0.000001f, cell.distanceSquared), 1.1f);
            sum += cell.weight;
        }
        const float inverse = 1.0f / std::max(0.000001f, sum);
        for (auto& cell : result) cell.weight *= inverse;
        return result;
    }

    void updateControl(uint32_t frames, bool immediate)
    {
        const float movementSeconds = params_.movementMs * 0.001f;
        const float movementAlpha = immediate ? 1.0f
            : 1.0f - std::exp(-static_cast<float>(frames) / static_cast<float>(sampleRate_ * movementSeconds));
        const Vec3 targetNormalized { params_.sourceX, params_.sourceY, params_.sourceZ };
        currentNormalized_ = ambi_ray_detail::mixVec(currentNormalized_, targetNormalized, movementAlpha);
        const float parameterAlpha = immediate ? 1.0f
            : 1.0f - std::exp(-static_cast<float>(frames) / static_cast<float>(sampleRate_ * 0.035));
        currentDirect_ += (params_.direct - currentDirect_) * parameterAlpha;
        currentEarly_ += (params_.early - currentEarly_) * parameterAlpha;
        currentLate_ += (params_.late - currentLate_) * parameterAlpha;
        currentWidth_ += (params_.width - currentWidth_) * parameterAlpha;
        currentOutput_ += (dbToGain(params_.outputGainDb) - currentOutput_) * parameterAlpha;
        currentSize_ += (params_.size - currentSize_) * parameterAlpha;
        currentScatter_ += (params_.scatter - currentScatter_) * parameterAlpha;
        currentAir_ += (params_.air - currentAir_) * parameterAlpha;

        const Vec3 source = sourcePositionMetres();
        const Vec3 directWorld {
            source.x - descriptor_.listenerPositionMetres.x,
            source.y - descriptor_.listenerPositionMetres.y,
            source.z - descriptor_.listenerPositionMetres.z
        };
        const float directDistance = std::max(0.02f, std::sqrt(
            directWorld.x * directWorld.x + directWorld.y * directWorld.y + directWorld.z * directWorld.z));
        const auto directAed = ambi_ray_detail::aedFromWorldVector(directWorld);
        directBasis_ = acnSn3dBasis7(directionFromAed(directAed[0], directAed[1]));
        directDelayFrames_ = directDistance / 343.0f * static_cast<float>(sampleRate_) * currentSize_;
        directDistanceGain_ = 1.0f / std::sqrt(std::max(1.0f, directDistance));
        const float directCutoff = clamp(20000.0f * std::exp(-currentAir_ * directDistance * 0.10f), 900.0f, 20000.0f);
        directFilterCoefficient_ = clamp(1.0f - std::exp(-2.0f * kPi * directCutoff / static_cast<float>(sampleRate_)), 0.001f, 1.0f);

        const auto nearby = nearbyCells(source);
        std::array<DesiredEvent, kAmbiRayMaxReflections> desired {};
        uint32_t desiredCount = 0u;
        float lateStart = 0.0f;
        float lateDecay = 0.0f;
        float lateLevel = 0.0f;
        float lateDiffusion = 0.0f;
        float lateDamping = 0.0f;
        for (const auto& weightedCell : nearby) {
            if (weightedCell.weight <= 0.0f || weightedCell.index >= descriptor_.cells.size()) continue;
            const auto& cell = descriptor_.cells[weightedCell.index];
            lateStart += cell.late.startMs * weightedCell.weight;
            lateDecay += cell.late.decaySeconds * weightedCell.weight;
            lateLevel += cell.late.level * weightedCell.weight;
            lateDiffusion += cell.late.diffusion * weightedCell.weight;
            lateDamping += cell.late.damping * weightedCell.weight;
            for (const auto& reflection : cell.reflections) {
                const uint32_t slot = std::min<uint32_t>(reflection.slot, kAmbiRayMaxReflections - 1u);
                auto& event = desired[slot];
                const Vec3 direction = directionFromAed(reflection.azimuthDeg, reflection.elevationDeg);
                event.delayMs += reflection.delayMs * weightedCell.weight;
                event.gain += reflection.gain * weightedCell.weight;
                event.direction.x += direction.x * weightedCell.weight;
                event.direction.y += direction.y * weightedCell.weight;
                event.direction.z += direction.z * weightedCell.weight;
                event.damping += reflection.damping * weightedCell.weight;
                event.weight += weightedCell.weight;
                desiredCount = std::max<uint32_t>(desiredCount, slot + 1u);
            }
        }
        currentLateStartMs_ += (lateStart - currentLateStartMs_) * movementAlpha;
        currentLateDecay_ += (lateDecay - currentLateDecay_) * movementAlpha;
        currentLateLevel_ += (lateLevel - currentLateLevel_) * movementAlpha;
        currentLateDiffusion_ += (lateDiffusion - currentLateDiffusion_) * movementAlpha;
        currentLateDamping_ += (lateDamping - currentLateDamping_) * movementAlpha;
        lateField_.setShape(currentLateDecay_, currentLateDamping_ + currentAir_ * 0.35f,
            currentLateDiffusion_, currentSize_);

        activeEventCount_ = std::min<uint32_t>(desiredCount, kAmbiRayMaxReflections);
        for (uint32_t index = 0u; index < kAmbiRayMaxReflections; ++index) {
            auto& runtime = events_[index];
            auto& target = desired[index];
            if (target.weight > 0.000001f) {
                const float inverse = 1.0f / target.weight;
                target.delayMs *= inverse;
                target.direction.x *= inverse;
                target.direction.y *= inverse;
                target.direction.z *= inverse;
                target.damping *= inverse;
            } else {
                target.gain = 0.0f;
                target.direction = runtime.direction;
                target.damping = runtime.damping;
            }
            const float eventAlpha = immediate ? 1.0f : movementAlpha;
            runtime.delayFrames += (target.delayMs * 0.001f * static_cast<float>(sampleRate_) * currentSize_ - runtime.delayFrames) * eventAlpha;
            runtime.gain += (target.gain - runtime.gain) * eventAlpha;
            runtime.direction = normalize(ambi_ray_detail::mixVec(runtime.direction, target.direction, eventAlpha));
            runtime.damping += (target.damping - runtime.damping) * eventAlpha;
            runtime.basis = acnSn3dBasis7(runtime.direction);
            const float distanceSeconds = runtime.delayFrames / static_cast<float>(sampleRate_);
            const float cutoff = clamp(19000.0f * std::exp(-currentAir_ * distanceSeconds * 18.0f)
                    * (1.0f - runtime.damping * 0.78f),
                700.0f, 20000.0f);
            runtime.filterCoefficient = clamp(1.0f - std::exp(-2.0f * kPi * cutoff / static_cast<float>(sampleRate_)), 0.001f, 1.0f);
        }

        for (uint32_t channel = 0u; channel < kAmbiRayRoomChannels; ++channel) {
            const uint32_t order = static_cast<uint32_t>(std::sqrt(static_cast<float>(channel)));
            roomOrderScale_[channel] = order == 0u ? 1.0f : std::pow(1.0f - currentScatter_ * 0.56f, static_cast<float>(order));
            lateOrderScale_[channel] = order == 0u ? 1.0f : std::pow(1.0f - currentScatter_ * 0.32f, static_cast<float>(order));
        }
        if (lateBasis_[0][0] == 0.0f) {
            constexpr std::array<std::array<float, 2>, kAmbiRayLateLines> directions {{
                { 45.0f, 35.264f }, { -45.0f, 35.264f }, { 135.0f, 35.264f }, { -135.0f, 35.264f },
                { 45.0f, -35.264f }, { -45.0f, -35.264f }, { 135.0f, -35.264f }, { -135.0f, -35.264f }
            }};
            for (uint32_t line = 0u; line < kAmbiRayLateLines; ++line)
                lateBasis_[line] = acnSn3dBasis7(directionFromAed(directions[line][0], directions[line][1]));
        }
        safetyRelease_ = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.100));
    }

    double sampleRate_ = 48000.0;
    AmbiRayDescriptor descriptor_ {};
    AmbiRayEncoderParams params_ {};
    ambi_ray_detail::FractionalDelay delay_;
    ambi_ray_detail::FractionalDelay roomDelay_;
    ambi_ray_detail::LateField lateField_;
    std::array<RuntimeEvent, kAmbiRayMaxReflections> events_ {};
    std::array<float, kAmbiRayMaxReflections> eventFilters_ {};
    std::array<float, kAmbiRayMaxChannels> directBasis_ {};
    std::array<std::array<float, kAmbiRayMaxChannels>, kAmbiRayLateLines> lateBasis_ {};
    std::array<float, kAmbiRayRoomChannels> roomOrderScale_ {};
    std::array<float, kAmbiRayRoomChannels> lateOrderScale_ {};
    std::array<float, kAmbiRayMaxChannels> frame_ {};
    Vec3 currentNormalized_ { 0.5f, 0.25f, 0.5f };
    uint32_t activeEventCount_ = 0u;
    float directDelayFrames_ = 0.0f;
    float directDistanceGain_ = 1.0f;
    float directFilterCoefficient_ = 1.0f;
    float directFilter_ = 0.0f;
    float dcInput_ = 0.0f;
    float dcOutput_ = 0.0f;
    float currentDirect_ = 1.0f;
    float currentEarly_ = 0.72f;
    float currentLate_ = 0.42f;
    float currentSize_ = 1.0f;
    float currentScatter_ = 0.45f;
    float currentWidth_ = 1.0f;
    float currentAir_ = 0.2f;
    float currentOutput_ = 0.5f;
    float currentLateStartMs_ = 45.0f;
    float currentLateDecay_ = 1.8f;
    float currentLateLevel_ = 0.18f;
    float currentLateDiffusion_ = 0.72f;
    float currentLateDamping_ = 0.38f;
    float safetyGain_ = 1.0f;
    float safetyRelease_ = 0.0002f;
};

} // namespace s3g
