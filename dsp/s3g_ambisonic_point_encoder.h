#pragma once

#include "s3g_3oafx.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiPointEncoderMaxPoints = 64;
constexpr uint32_t kAmbiPointEncoderPrototypePoints = 16;

enum class AmbiPointMotionMode : uint32_t {
    Off = 0,
    Drift = 1,
    Orbit = 2,
    Swarm = 3,
    Pulse = 4,
    Phys = 5,
};

struct AmbiPoint {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
    float gain = 1.0f;
    bool enabled = true;
    bool solo = false;
};

struct AmbiPointEncoderParams {
    uint32_t activePoints = kAmbiPointEncoderPrototypePoints;
    uint32_t selectedPoint = 0;
    float selectedAzimuthDeg = 0.0f;
    float selectedElevationDeg = 0.0f;
    float selectedDistance = 1.0f;
    float selectedGain = 1.0f;
    bool selectedEnabled = true;
    bool upperHemisphereOnly = false;
    uint32_t motionScene = 0;
    AmbiPointMotionMode motionMode = AmbiPointMotionMode::Off;
    float motionAmount = 0.0f;
    float rateHz = 0.035f;
    float attract = 0.05f;
    float repel = 0.03f;
    float drag = 0.94f;
    float swirl = 0.0f;
    float brownian = 0.0f;
    float collision = 0.0f;
    float impact = 0.0f;
    float physicsScale = 1.0f;
    float poltergeist = 0.0f;
    float poltergeistRate = 1.0f;
    float poltergeistReach = 0.55f;
    float poltergeistRadius = 0.28f;
    float poltergeistChaos = 0.0f;
    float outputGainDb = -6.0f;
};

class AmbiPointEncoder {
public:
    void prepare(double sampleRate, uint32_t pointCount)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        pointCount_ = std::clamp<uint32_t>(pointCount, 1u, kAmbiPointEncoderMaxPoints);
        params_.activePoints = std::min(params_.activePoints, pointCount_);
        resetScene();
        resetMotion();
    }

    void resetScene()
    {
        const uint32_t count = std::max<uint32_t>(1u, pointCount_);
        for (uint32_t i = 0; i < kAmbiPointEncoderMaxPoints; ++i) {
            const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(std::max<uint32_t>(1u, count));
            const float az = wrapSignedDeg(i * 137.507764f);
            const float el = std::asin(clamp(1.0f - 2.0f * u, -1.0f, 1.0f)) * 180.0f / kPi;
            points_[i] = { az, el, 1.0f, 1.0f, i < count };
            basePoints_[i] = points_[i];
            smoothPoints_[i] = points_[i];
            prevPositions_[i] = toVec(points_[i]);
            collisionEnergy_[i] = 0.0f;
        }
        syncSelectedParamsFromPoint();
    }

    void resetMotion()
    {
        for (auto& v : velocities_) {
            v = {};
        }
        for (uint32_t i = 0; i < kAmbiPointEncoderMaxPoints; ++i) {
            prevPositions_[i] = toVec(points_[i]);
            collisionEnergy_[i] = 0.0f;
            bondRelease_[i] = 0.0f;
        }
        phase_ = 0.0f;
        motionTime_ = 0.0f;
        seed_ = 0x1234abcdU;
        perturbationSource_ = {};
        prevPerturbationSource_ = {};
        perturbationPrimed_ = false;
    }

    void setParams(const AmbiPointEncoderParams& params)
    {
        const uint32_t previousSelected = params_.selectedPoint;
        const uint32_t previousScene = params_.motionScene;
        const AmbiPointMotionMode previousMode = params_.motionMode;
        params_ = sanitize(params, pointCount_);
        const bool reseedMotion = previousScene != params_.motionScene || previousMode != params_.motionMode;
        if (params_.selectedPoint != previousSelected) {
            syncSelectedParamsFromPoint();
        } else {
            auto& point = points_[params_.selectedPoint];
            point.azimuthDeg = params_.selectedAzimuthDeg;
            point.elevationDeg = params_.selectedElevationDeg;
            if (params_.upperHemisphereOnly) {
                point.elevationDeg = std::max(0.0f, point.elevationDeg);
            }
            point.distance = params_.selectedDistance;
            point.gain = params_.selectedGain;
            point.enabled = params_.selectedEnabled && params_.selectedPoint < params_.activePoints;
            basePoints_[params_.selectedPoint] = point;
        }
        if (params_.upperHemisphereOnly) {
            constrainUpperHemisphere();
        }
        if (reseedMotion) {
            seedSceneMotion();
        }
    }

    AmbiPointEncoderParams params() const { return params_; }
    const std::array<AmbiPoint, kAmbiPointEncoderMaxPoints>& points() const { return smoothPoints_; }
    const std::array<AmbiPoint, kAmbiPointEncoderMaxPoints>& editPoints() const { return points_; }
    const std::array<float, kAmbiPointEncoderMaxPoints>& collisionEnergy() const { return collisionEnergy_; }
    const std::array<float, kAmbiPointEncoderMaxPoints>& bondRelease() const { return bondRelease_; }
    Vec3 perturbationSource() const { return perturbationSource_; }
    Vec3 previousPerturbationSource() const { return prevPerturbationSource_; }

    AmbiPoint editPoint(uint32_t index) const
    {
        return points_[std::min<uint32_t>(index, kAmbiPointEncoderMaxPoints - 1u)];
    }

    void setPoint(uint32_t index, AmbiPoint point)
    {
        if (index >= pointCount_) return;
        point.azimuthDeg = wrapSignedDeg(point.azimuthDeg);
        point.elevationDeg = clamp(point.elevationDeg, params_.upperHemisphereOnly ? 0.0f : -90.0f, 90.0f);
        point.distance = clamp(point.distance, 0.15f, 2.0f);
        point.gain = clamp(point.gain, 0.0f, 2.0f);
        point.enabled = point.enabled && index < params_.activePoints;
        points_[index] = point;
        basePoints_[index] = point;
        prevPositions_[index] = toVec(point);
        collisionEnergy_[index] = 0.0f;
        bondRelease_[index] = 0.0f;
        if (index == params_.selectedPoint) {
            syncSelectedParamsFromPoint();
        }
    }

    void setPointAzimuth(uint32_t index, float value)
    {
        auto point = editPoint(index);
        point.azimuthDeg = value;
        setPoint(index, point);
    }

    void setPointElevation(uint32_t index, float value)
    {
        auto point = editPoint(index);
        point.elevationDeg = value;
        setPoint(index, point);
    }

    void setPointDistance(uint32_t index, float value)
    {
        auto point = editPoint(index);
        point.distance = value;
        setPoint(index, point);
    }

    void setPointGain(uint32_t index, float value)
    {
        auto point = editPoint(index);
        point.gain = value;
        setPoint(index, point);
    }

    void setPointEnabled(uint32_t index, bool value)
    {
        auto point = editPoint(index);
        point.enabled = value;
        setPoint(index, point);
    }

    void setPointSolo(uint32_t index, bool value)
    {
        auto point = editPoint(index);
        point.solo = value;
        setPoint(index, point);
    }

    void setScene(const std::array<AmbiPoint, kAmbiPointEncoderMaxPoints>& points)
    {
        points_ = points;
        for (uint32_t i = 0; i < kAmbiPointEncoderMaxPoints; ++i) {
            points_[i].azimuthDeg = wrapSignedDeg(points_[i].azimuthDeg);
            points_[i].elevationDeg = clamp(points_[i].elevationDeg, params_.upperHemisphereOnly ? 0.0f : -90.0f, 90.0f);
            points_[i].distance = clamp(points_[i].distance, 0.15f, 2.0f);
            points_[i].gain = clamp(points_[i].gain, 0.0f, 2.0f);
            points_[i].enabled = points_[i].enabled && i < pointCount_;
            points_[i].solo = points_[i].solo && i < pointCount_;
            basePoints_[i] = points_[i];
            smoothPoints_[i] = points_[i];
            prevPositions_[i] = toVec(points_[i]);
            collisionEnergy_[i] = 0.0f;
        }
        if (params_.upperHemisphereOnly) {
            constrainUpperHemisphere();
        }
        syncSelectedParamsFromPoint();
    }

    void processBlock(const float* const* inputs, float* const* outputs, uint32_t frames)
    {
        if (!outputs || frames == 0) {
            return;
        }
        for (uint32_t ch = 0; ch < k3OaChannels; ++ch) {
            if (outputs[ch]) {
                std::fill(outputs[ch], outputs[ch] + frames, 0.0f);
            }
        }

        advanceMotion(static_cast<float>(frames / sampleRate_));
        smoothScene();

        const uint32_t active = std::min<uint32_t>(params_.activePoints, pointCount_);
        const float gain = dbToGain(params_.outputGainDb);
        bool anySolo = false;
        for (uint32_t p = 0; p < std::min<uint32_t>(active, kAmbiPointEncoderPrototypePoints); ++p) {
            anySolo = anySolo || smoothPoints_[p].solo;
        }
        std::array<std::array<float, k3OaChannels>, kAmbiPointEncoderPrototypePoints> basis {};
        std::array<float, kAmbiPointEncoderPrototypePoints> laneGain {};
        for (uint32_t p = 0; p < std::min<uint32_t>(active, kAmbiPointEncoderPrototypePoints); ++p) {
            const auto& point = smoothPoints_[p];
            const Vec3 dir = directionFromAed(point.azimuthDeg, point.elevationDeg);
            basis[p] = acnSn3dBasis(dir);
            const float distanceGain = 1.0f / std::max(0.15f, point.distance);
            const bool audible = point.enabled && (!anySolo || point.solo);
            laneGain[p] = (audible ? point.gain : 0.0f) * distanceGain * gain;
        }

        for (uint32_t i = 0; i < frames; ++i) {
            for (uint32_t p = 0; p < std::min<uint32_t>(active, kAmbiPointEncoderPrototypePoints); ++p) {
                const float sample = inputs && inputs[p] ? inputs[p][i] * laneGain[p] : 0.0f;
                if (sample == 0.0f) {
                    continue;
                }
                for (uint32_t ch = 0; ch < k3OaChannels; ++ch) {
                    if (outputs[ch]) {
                        outputs[ch][i] = flushDenormal(outputs[ch][i] + sample * basis[p][ch]);
                    }
                }
            }
        }
    }

private:
    static float wrapSignedDeg(float value)
    {
        while (value > 180.0f) value -= 360.0f;
        while (value <= -180.0f) value += 360.0f;
        return value;
    }

    static AmbiPointEncoderParams sanitize(AmbiPointEncoderParams params, uint32_t pointCount)
    {
        params.activePoints = std::clamp<uint32_t>(params.activePoints, 1u, std::max<uint32_t>(1u, pointCount));
        params.selectedPoint = std::min<uint32_t>(params.selectedPoint, params.activePoints - 1u);
        params.selectedAzimuthDeg = wrapSignedDeg(params.selectedAzimuthDeg);
        params.selectedElevationDeg = clamp(params.selectedElevationDeg, params.upperHemisphereOnly ? 0.0f : -90.0f, 90.0f);
        params.selectedDistance = clamp(params.selectedDistance, 0.15f, 2.0f);
        params.selectedGain = clamp(params.selectedGain, 0.0f, 2.0f);
        params.selectedEnabled = params.selectedEnabled && params.selectedPoint < params.activePoints;
        params.motionScene = std::min<uint32_t>(params.motionScene, 7u);
        params.motionAmount = clamp(params.motionAmount, 0.0f, 1.0f);
        params.rateHz = clamp(params.rateHz, 0.005f, 0.50f);
        params.attract = clamp(params.attract, 0.0f, 0.24f);
        params.repel = clamp(params.repel, 0.0f, 0.24f);
        params.drag = clamp(params.drag, 0.45f, 0.995f);
        params.swirl = clamp(params.swirl, -0.24f, 0.24f);
        params.brownian = clamp(params.brownian, 0.0f, 0.24f);
        params.collision = clamp(params.collision, 0.0f, 1.0f);
        params.impact = clamp(params.impact, 0.0f, 1.0f);
        params.physicsScale = clamp(params.physicsScale, 0.25f, 2.0f);
        params.poltergeist = clamp(params.poltergeist, 0.0f, 1.0f);
        params.poltergeistRate = clamp(params.poltergeistRate, 0.05f, 4.0f);
        params.poltergeistReach = clamp(params.poltergeistReach, 0.0f, 1.0f);
        params.poltergeistRadius = clamp(params.poltergeistRadius, 0.04f, 1.0f);
        params.poltergeistChaos = clamp(params.poltergeistChaos, 0.0f, 1.0f);
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
        return params;
    }

    static float randSigned(uint32_t& seed)
    {
        seed = seed * 1664525u + 1013904223u;
        const float value = static_cast<float>((seed >> 8u) & 0xffffffu) / 8388607.5f - 1.0f;
        return clamp(value, -1.0f, 1.0f);
    }

    static Vec3 toVec(const AmbiPoint& point)
    {
        const Vec3 dir = directionFromAed(point.azimuthDeg, point.elevationDeg);
        return { dir.x * point.distance, dir.y * point.distance, dir.z * point.distance };
    }

    static AmbiPoint fromVec(Vec3 v, const AmbiPoint& previous)
    {
        const float dist = clamp(std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z), 0.15f, 2.0f);
        if (dist < 0.0001f) {
            return previous;
        }
        const float az = std::atan2(v.y, v.x) * 180.0f / kPi;
        const float el = std::asin(clamp(v.z / dist, -1.0f, 1.0f)) * 180.0f / kPi;
        AmbiPoint out = previous;
        out.azimuthDeg = wrapSignedDeg(az);
        out.elevationDeg = clamp(el, -90.0f, 90.0f);
        out.distance = dist;
        return out;
    }

    AmbiPoint scaledForMotionView(const AmbiPoint& point) const
    {
        if (params_.motionMode == AmbiPointMotionMode::Off || params_.motionAmount <= 0.0001f) {
            return point;
        }
        const float scale = params_.physicsScale;
        if (std::fabs(scale - 1.0f) <= 0.0001f) {
            return point;
        }
        Vec3 v = toVec(point);
        const float r = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (r < 0.0001f) {
            return point;
        }
        float targetR = r;
        if (scale >= 1.0f) {
            const float towardUnit = std::min(1.0f, scale - 1.0f);
            targetR = r + (1.0f - r) * towardUnit;
            if (r > 1.0f) {
                targetR = r;
            }
        } else {
            targetR = r * scale;
        }
        targetR = clamp(targetR, 0.15f, 2.0f);
        const float mul = targetR / r;
        v.x *= mul;
        v.y *= mul;
        v.z *= mul;
        return fromVec(v, point);
    }

    void syncSelectedParamsFromPoint()
    {
        params_.selectedPoint = std::min<uint32_t>(params_.selectedPoint, std::max<uint32_t>(1u, params_.activePoints) - 1u);
        const auto& point = points_[params_.selectedPoint];
        params_.selectedAzimuthDeg = point.azimuthDeg;
        params_.selectedElevationDeg = params_.upperHemisphereOnly ? std::max(0.0f, point.elevationDeg) : point.elevationDeg;
        params_.selectedDistance = point.distance;
        params_.selectedGain = point.gain;
        params_.selectedEnabled = point.enabled;
    }

    void constrainUpperHemisphere()
    {
        for (uint32_t i = 0; i < pointCount_; ++i) {
            points_[i].elevationDeg = std::max(0.0f, points_[i].elevationDeg);
            basePoints_[i].elevationDeg = std::max(0.0f, basePoints_[i].elevationDeg);
            smoothPoints_[i].elevationDeg = std::max(0.0f, smoothPoints_[i].elevationDeg);
            velocities_[i].z = std::max(0.0f, velocities_[i].z);
        }
        if (perturbationPrimed_) {
            perturbationSource_ = clampPerturbationToUpperHemisphere(perturbationSource_, physicsBoundary());
            prevPerturbationSource_ = perturbationSource_;
        }
    }

    void seedSceneMotion()
    {
        for (auto& v : velocities_) {
            v = {};
        }
        if (params_.motionMode == AmbiPointMotionMode::Off || params_.motionAmount <= 0.0001f) {
            return;
        }
        const uint32_t active = std::min<uint32_t>(params_.activePoints, pointCount_);
        const float amount = std::max(0.12f, params_.motionAmount);
        for (uint32_t i = 0; i < active; ++i) {
            Vec3 p = toVec(points_[i]);
            const float r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            const float inv = 1.0f / std::max(0.0001f, r);
            const Vec3 radial { p.x * inv, p.y * inv, p.z * inv };
            const float sign = (i & 1u) ? -1.0f : 1.0f;
            const float phase = static_cast<float>(i) * 1.61803398875f;
            if (params_.motionMode == AmbiPointMotionMode::Orbit || params_.motionScene == 2u) {
                velocities_[i].x = -p.y * (0.010f + amount * 0.012f) + std::sin(phase) * 0.0035f;
                velocities_[i].y = p.x * (0.010f + amount * 0.012f) + std::cos(phase) * 0.0035f;
                velocities_[i].z = sign * (0.0025f + amount * 0.0030f);
            } else if (params_.motionScene == 3u) {
                velocities_[i].x = -radial.x * (0.006f + amount * 0.010f) + std::sin(phase) * 0.006f;
                velocities_[i].y = -radial.y * (0.006f + amount * 0.010f) + std::cos(phase) * 0.006f;
                velocities_[i].z = -0.010f - std::fabs(radial.z) * 0.004f + sign * 0.004f;
            } else if (params_.motionScene == 4u) {
                velocities_[i].x = radial.x * (0.010f + amount * 0.018f) + std::sin(phase * 1.7f) * 0.007f;
                velocities_[i].y = radial.y * (0.010f + amount * 0.018f) + std::cos(phase * 1.3f) * 0.007f;
                velocities_[i].z = radial.z * (0.006f + amount * 0.010f) + sign * 0.006f;
            } else if (params_.motionScene == 5u) {
                velocities_[i].x = randSigned(seed_) * (0.012f + amount * 0.022f);
                velocities_[i].y = randSigned(seed_) * (0.012f + amount * 0.022f);
                velocities_[i].z = randSigned(seed_) * (0.010f + amount * 0.018f);
            }
            const Vec3 seeded {
                p.x - velocities_[i].x,
                p.y - velocities_[i].y,
                p.z - velocities_[i].z
            };
            prevPositions_[i] = seeded;
            collisionEnergy_[i] = 0.0f;
            bondRelease_[i] = 0.0f;
        }
    }

    float sceneSpringScale() const
    {
        if (params_.motionMode != AmbiPointMotionMode::Phys) return 0.0f;
        const float geistDissolve = 1.0f - clamp(params_.poltergeist, 0.0f, 1.0f);
        switch (params_.motionScene) {
        case 3: return 0.0f;  // BOUNCE: boundary and point collisions, not net tension.
        case 4: return 0.34f * geistDissolve; // COLLIDE: temporary, easily broken links.
        case 5: return 0.0f;  // SCATTER: free impulse field.
        case 7: return 1.0f * geistDissolve;  // ELASTIC: explicit nearest-neighbor net.
        case 6: return 0.55f * geistDissolve; // CUSTOM: moderate default after hand edits.
        default: return 0.18f * geistDissolve;
        }
    }

    static float dist3(Vec3 a, Vec3 b)
    {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        const float dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    float physicsBoundary() const
    {
        return clamp(params_.physicsScale, 0.25f, 2.0f);
    }

    float poltergeistInfluenceRadius() const
    {
        return 0.16f + params_.poltergeistRadius * 0.78f;
    }

    static Vec3 limitToRadius(Vec3 v, float radius)
    {
        const float r = std::sqrt(std::max(0.000001f, v.x * v.x + v.y * v.y + v.z * v.z));
        if (r <= radius) {
            return v;
        }
        const float mul = radius / r;
        return { v.x * mul, v.y * mul, v.z * mul };
    }

    Vec3 clampPerturbationToUpperHemisphere(Vec3 v, float boundary) const
    {
        if (!params_.upperHemisphereOnly) {
            return v;
        }
        v.z = std::max(0.0f, v.z);
        return limitToRadius(v, boundary);
    }

    Vec3 adaptPerturbationTargetToUpperHemisphere(Vec3 v, float boundary) const
    {
        if (!params_.upperHemisphereOnly) {
            return v;
        }
        const float radius = poltergeistInfluenceRadius();
        const float minZ = std::min(boundary * 0.55f, radius * 0.35f);
        v.z = minZ + std::fabs(v.z) * 0.65f;
        v = limitToRadius(v, boundary);
        v.z = std::max(0.0f, v.z);
        return v;
    }

    void applySpringConstraint(uint32_t a, uint32_t b, float stiffness)
    {
        Vec3 pa = toVec(points_[a]);
        Vec3 pb = toVec(points_[b]);
        const Vec3 ba = toVec(basePoints_[a]);
        const Vec3 bb = toVec(basePoints_[b]);
        const float timedRelease = clamp(std::max(bondRelease_[a], bondRelease_[b]) / 2.0f, 0.0f, 1.0f);
        const float release = std::max(std::max(collisionEnergy_[a], collisionEnergy_[b]), timedRelease);
        stiffness *= (1.0f - clamp(release * 0.98f, 0.0f, 0.98f));
        if (stiffness <= 0.0001f) return;
        const float rest = clamp(dist3(ba, bb) * 0.92f, 0.18f, 1.15f);
        const float dx = pb.x - pa.x;
        const float dy = pb.y - pa.y;
        const float dz = pb.z - pa.z;
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d < 0.0001f) return;
        const float correction = (d - rest) / d * stiffness * 0.5f;
        const Vec3 c { dx * correction, dy * correction, dz * correction };
        pa.x += c.x;
        pa.y += c.y;
        pa.z += c.z;
        pb.x -= c.x;
        pb.y -= c.y;
        pb.z -= c.z;
        if (params_.upperHemisphereOnly) {
            pa.z = std::max(0.0f, pa.z);
            pb.z = std::max(0.0f, pb.z);
        }
        points_[a] = fromVec(pa, points_[a]);
        points_[b] = fromVec(pb, points_[b]);
    }

    void applyNearestNeighborSprings(uint32_t active, float stiffness)
    {
        if (stiffness <= 0.0001f || active < 2u) return;
        for (uint32_t i = 0; i < active; ++i) {
            if (!points_[i].enabled) continue;
            float bestA = 999999.0f;
            float bestB = 999999.0f;
            int neighborA = -1;
            int neighborB = -1;
            const Vec3 pi = toVec(basePoints_[i]);
            for (uint32_t j = 0; j < active; ++j) {
                if (i == j || !points_[j].enabled) continue;
                const float d = dist3(pi, toVec(basePoints_[j]));
                if (d < bestA) {
                    bestB = bestA;
                    neighborB = neighborA;
                    bestA = d;
                    neighborA = static_cast<int>(j);
                } else if (d < bestB) {
                    bestB = d;
                    neighborB = static_cast<int>(j);
                }
            }
            if (neighborA >= 0 && i < static_cast<uint32_t>(neighborA)) {
                applySpringConstraint(i, static_cast<uint32_t>(neighborA), stiffness);
            }
            if (neighborB >= 0 && i < static_cast<uint32_t>(neighborB)) {
                applySpringConstraint(i, static_cast<uint32_t>(neighborB), stiffness * 0.72f);
            }
        }
    }

    Vec3 scenePerturbationTarget(float boundary) const
    {
        const float t = motionTime_ * params_.poltergeistRate * 6.28318530718f;
        const float reachNorm = params_.poltergeistReach;
        const float reach = 0.12f + reachNorm * 0.74f;
        const float zReach = boundary * (0.06f + reachNorm * (params_.upperHemisphereOnly ? 0.76f : 0.64f));
        const float chaos = params_.poltergeistChaos;
        const float r = boundary * reach;
        if (params_.motionScene == 3u) {
            const float z = params_.upperHemisphereOnly ? zReach * 0.22f : -zReach * 0.60f;
            return {
                std::sin(t * 0.77f + std::sin(t * 0.19f) * chaos) * r * 0.72f,
                std::cos(t * 0.61f + std::cos(t * 0.23f) * chaos) * r * 0.72f,
                z + std::sin(t * 0.43f + chaos) * zReach * (0.36f + chaos * 0.18f)
            };
        }
        if (params_.motionScene == 4u) {
            return {
                std::sin(t * 0.53f + std::sin(t * 0.31f) * chaos * 1.40f) * r,
                std::cos(t * 0.71f + std::cos(t * 0.27f) * chaos * 1.20f) * r,
                std::sin(t * 0.37f + std::sin(t * 0.17f) * chaos) * zReach * (params_.upperHemisphereOnly ? 0.92f : 1.0f)
            };
        }
        if (params_.motionScene == 5u) {
            return {
                std::sin(t * (0.83f + chaos * 0.42f) + std::sin(t * 0.29f) * (1.0f + chaos)) * r,
                std::cos(t * (0.71f + chaos * 0.36f) + std::sin(t * 0.41f) * (1.0f + chaos)) * r,
                std::sin(t * (0.63f + chaos * 0.58f) + std::cos(t * 0.31f) * (1.0f + chaos)) * zReach * (params_.upperHemisphereOnly ? 0.96f : 1.10f)
            };
        }
        return {
            std::sin(t * 0.41f + chaos * std::sin(t * 0.13f)) * r * 0.55f,
            std::cos(t * 0.37f + chaos * std::cos(t * 0.17f)) * r * 0.55f,
            std::sin(t * 0.23f + chaos) * zReach * (params_.upperHemisphereOnly ? 0.62f : 0.78f)
        };
    }

    Vec3 updatePerturbationSource(float dt, float boundary)
    {
        Vec3 target = adaptPerturbationTargetToUpperHemisphere(scenePerturbationTarget(boundary), boundary);
        if (!perturbationPrimed_) {
            perturbationSource_ = target;
            prevPerturbationSource_ = target;
            perturbationPrimed_ = true;
            return {};
        }

        prevPerturbationSource_ = perturbationSource_;
        const float chaseHz = 1.4f + params_.poltergeistRate * 1.3f + params_.poltergeistChaos * 0.9f;
        const float alpha = 1.0f - std::exp(-std::max(0.0f, dt) * chaseHz);
        Vec3 delta {
            target.x - perturbationSource_.x,
            target.y - perturbationSource_.y,
            target.z - perturbationSource_.z
        };
        const float d = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        const float maxStep = boundary * (0.18f + params_.poltergeistRate * 0.20f) * std::max(0.0f, dt);
        float move = alpha;
        if (maxStep > 0.0f && d * move > maxStep) {
            move = maxStep / std::max(0.0001f, d);
        }
        perturbationSource_.x += delta.x * move;
        perturbationSource_.y += delta.y * move;
        perturbationSource_.z += delta.z * move;
        if (params_.upperHemisphereOnly) {
            perturbationSource_ = clampPerturbationToUpperHemisphere(perturbationSource_, boundary);
            if (prevPerturbationSource_.z < 0.0f) {
                prevPerturbationSource_ = perturbationSource_;
            }
        }
        return {
            perturbationSource_.x - prevPerturbationSource_.x,
            perturbationSource_.y - prevPerturbationSource_.y,
            perturbationSource_.z - prevPerturbationSource_.z
        };
    }

    Vec3 perturbationKick(Vec3 p, Vec3 sourcePrev, Vec3 source, Vec3 sourceVelocity, uint32_t index, float physicsAmount, float& energy) const
    {
        energy = 0.0f;
        if (params_.poltergeist <= 0.0001f) return {};
        const float radius = poltergeistInfluenceRadius();
        const Vec3 sweep {
            source.x - sourcePrev.x,
            source.y - sourcePrev.y,
            source.z - sourcePrev.z
        };
        const float sweepLen2 = sweep.x * sweep.x + sweep.y * sweep.y + sweep.z * sweep.z;
        float t = 1.0f;
        if (sweepLen2 > 0.0000001f) {
            const Vec3 fromPrev {
                p.x - sourcePrev.x,
                p.y - sourcePrev.y,
                p.z - sourcePrev.z
            };
            t = clamp((fromPrev.x * sweep.x + fromPrev.y * sweep.y + fromPrev.z * sweep.z) / sweepLen2, 0.0f, 1.0f);
        }
        const Vec3 closest {
            sourcePrev.x + sweep.x * t,
            sourcePrev.y + sweep.y * t,
            sourcePrev.z + sweep.z * t
        };
        Vec3 sweepDelta { p.x - closest.x, p.y - closest.y, p.z - closest.z };
        const float sweepD2 = sweepDelta.x * sweepDelta.x + sweepDelta.y * sweepDelta.y + sweepDelta.z * sweepDelta.z;
        const float sweepD = std::sqrt(std::max(0.000001f, sweepD2));
        Vec3 sourceDelta { p.x - source.x, p.y - source.y, p.z - source.z };
        const float sourceD2 = sourceDelta.x * sourceDelta.x + sourceDelta.y * sourceDelta.y + sourceDelta.z * sourceDelta.z;
        const float sourceD = std::sqrt(std::max(0.000001f, sourceD2));
        if (sweepD >= radius && sourceD >= radius) return {};
        const bool sourceInside = sourceD <= sweepD;
        Vec3 delta = sourceInside ? sourceDelta : sweepDelta;
        const float d = sourceInside ? sourceD : sweepD;
        const float nrm = d / std::max(0.0001f, radius);
        const float edgeDepth = clamp(1.0f - nrm, 0.0f, 1.0f);
        const float contactRaw = std::pow(edgeDepth, 0.42f);
        const float contact = sourceInside
            ? std::max(contactRaw, params_.poltergeist * std::pow(edgeDepth, 0.55f) * 0.92f)
            : contactRaw;
        const float pathSpeed = std::sqrt(sourceVelocity.x * sourceVelocity.x + sourceVelocity.y * sourceVelocity.y + sourceVelocity.z * sourceVelocity.z);
        Vec3 travel {};
        if (pathSpeed > 0.000001f) {
            travel = {
                sourceVelocity.x / pathSpeed,
                sourceVelocity.y / pathSpeed,
                sourceVelocity.z / pathSpeed
            };
        } else {
            travel = { 1.0f, 0.0f, 0.0f };
        }
        Vec3 n {};
        if (d > 0.0001f) {
            const float inv = 1.0f / d;
            n = { delta.x * inv, delta.y * inv, delta.z * inv };
        } else {
            n = { -travel.y, travel.x, 0.0f };
            const float nr = std::sqrt(std::max(0.000001f, n.x * n.x + n.y * n.y + n.z * n.z));
            n.x /= nr;
            n.y /= nr;
            n.z /= nr;
        }
        const float sceneBoost = params_.motionScene == 5u ? 1.20f : (params_.motionScene == 4u ? 1.05f : 0.90f);
        const float chaosLift = 0.65f + params_.poltergeistChaos * 0.70f;
        const float pathEnergy = sourceInside
            ? 1.85f + params_.poltergeist * 1.15f
            : 0.90f + std::min(1.80f, pathSpeed * 150.0f);
        const float strength = params_.poltergeist * physicsAmount * sceneBoost * contact * pathEnergy;
        const float normalKick = (0.018f + params_.impact * 0.032f) * strength;
        const float carryKick = (0.026f + params_.poltergeistChaos * 0.016f) * strength;
        const float glancingKick = (0.010f + std::fabs(params_.swirl) * 0.060f + params_.poltergeistChaos * 0.018f) * strength * chaosLift;

        (void)index;
        energy = clamp(strength * 1.2f, 0.0f, 1.0f);
        return {
            n.x * normalKick + travel.x * carryKick - n.y * glancingKick,
            n.y * normalKick + travel.y * carryKick + n.x * glancingKick,
            n.z * normalKick + travel.z * carryKick
        };
    }

    void advancePhysicsWorld(float dt, uint32_t active, float amount, float physicsAmount)
    {
        const uint32_t substeps = params_.impact > 0.55f ? 3u : 2u;
        const float step = clamp(dt / static_cast<float>(substeps), 0.0f, 0.025f);
        const float accelScale = step * step * (160.0f + params_.impact * 190.0f);
        const float damping = clamp(params_.drag, 0.45f, 0.985f);
        const float boundary = physicsBoundary();
        const float minDist = 0.055f + params_.collision * 0.130f;
        const float minDist2 = minDist * minDist;
        const float bounce = 0.54f + params_.impact * 0.38f;
        const float springBase = params_.motionScene == 4u ? 0.12f + params_.collision * 0.12f
            : (params_.motionScene == 3u ? 0.045f + params_.collision * 0.045f : 0.02f + params_.collision * 0.03f);
        const float spring = springBase * sceneSpringScale();
        const Vec3 perturbationVelocity = updatePerturbationSource(dt, boundary);

        for (uint32_t s = 0; s < substeps; ++s) {
            for (uint32_t i = 0; i < active; ++i) {
                if (!points_[i].enabled) continue;
                Vec3 p = toVec(points_[i]);
                Vec3 prev = prevPositions_[i];
                const Vec3 base = toVec(basePoints_[i]);
                const float r = std::sqrt(std::max(0.000001f, p.x * p.x + p.y * p.y + p.z * p.z));
                const Vec3 radial { p.x / r, p.y / r, p.z / r };
                float perturbationEnergy = 0.0f;
                const Vec3 kick = perturbationKick(p, prevPerturbationSource_, perturbationSource_, perturbationVelocity, i, physicsAmount, perturbationEnergy);
                const float timedRelease = clamp(bondRelease_[i] / 2.0f, 0.0f, 1.0f);
                const float localRelease = clamp(std::max(std::max(perturbationEnergy, collisionEnergy_[i]), timedRelease) * std::max(0.35f, params_.poltergeist), 0.0f, 0.98f);
                const float bondScale = 1.0f - localRelease * 0.82f;
                Vec3 force {
                    (-p.x * params_.attract - p.y * params_.swirl) * bondScale,
                    (-p.y * params_.attract + p.x * params_.swirl) * bondScale,
                    (-p.z * params_.attract) * bondScale
                };

                if (params_.motionScene == 3u) {
                    const float beat = std::max(0.0f, std::sin((phase_ * 1.41f + static_cast<float>(i) * 0.071f) * 6.28318530718f));
                    force.x += -radial.x * (0.10f + beat * 0.10f) * physicsAmount;
                    force.y += -radial.y * (0.10f + beat * 0.10f) * physicsAmount;
                    force.z += (-0.18f - p.z * 0.040f + beat * 0.12f) * physicsAmount;
                } else if (params_.motionScene == 4u) {
                    const float wave = std::sin((phase_ * 0.91f + static_cast<float>(i) * 0.173f) * 6.28318530718f);
                    force.x += (base.x - p.x) * 0.055f * physicsAmount * bondScale + wave * 0.070f;
                    force.y += (base.y - p.y) * 0.055f * physicsAmount * bondScale - wave * 0.056f;
                    force.z += (base.z - p.z) * 0.040f * physicsAmount * bondScale + std::sin(wave * 2.2f) * 0.040f;
                } else if (params_.motionScene == 5u) {
                    const float burst = std::sin((phase_ * 2.33f + static_cast<float>(i) * 0.389f) * 6.28318530718f);
                    force.x += randSigned(seed_) * 0.17f * physicsAmount + base.x * burst * 0.070f;
                    force.y += randSigned(seed_) * 0.17f * physicsAmount + base.y * burst * 0.070f;
                    force.z += randSigned(seed_) * 0.13f * physicsAmount + base.z * burst * 0.050f;
                } else {
                    force.x += (base.x - p.x) * 0.025f * physicsAmount * bondScale + randSigned(seed_) * params_.brownian * 0.10f;
                    force.y += (base.y - p.y) * 0.025f * physicsAmount * bondScale + randSigned(seed_) * params_.brownian * 0.10f;
                    force.z += (base.z - p.z) * 0.020f * physicsAmount * bondScale + randSigned(seed_) * params_.brownian * 0.08f;
                }

                const Vec3 velocity {
                    (p.x - prev.x) * damping,
                    (p.y - prev.y) * damping,
                    (p.z - prev.z) * damping
                };
                Vec3 nextVelocity = velocity;
                nextVelocity.x += kick.x;
                nextVelocity.y += kick.y;
                nextVelocity.z += kick.z;
                if (perturbationEnergy > 0.0f) {
                    collisionEnergy_[i] = std::max(collisionEnergy_[i], perturbationEnergy);
                    const float releaseMaxSeconds = 2.0f + params_.poltergeist * 2.0f + params_.poltergeistChaos * 1.0f;
                    const float releaseSeconds = std::max(2.0f, releaseMaxSeconds * clamp(0.45f + perturbationEnergy * 0.55f, 0.0f, 1.0f));
                    bondRelease_[i] = std::max(bondRelease_[i], releaseSeconds);
                }
                prevPositions_[i] = p;
                p.x += nextVelocity.x + force.x * accelScale;
                p.y += nextVelocity.y + force.y * accelScale;
                p.z += nextVelocity.z + force.z * accelScale;

                if (params_.upperHemisphereOnly && p.z < 0.0f) {
                    p.z = 0.0f;
                    prevPositions_[i].z = -prevPositions_[i].z * bounce;
                    collisionEnergy_[i] = std::max(collisionEnergy_[i], 0.45f + params_.impact * 0.35f);
                }

                const float pr = std::sqrt(std::max(0.000001f, p.x * p.x + p.y * p.y + p.z * p.z));
                if (pr > boundary) {
                    const Vec3 n { p.x / pr, p.y / pr, p.z / pr };
                    const Vec3 v { p.x - prevPositions_[i].x, p.y - prevPositions_[i].y, p.z - prevPositions_[i].z };
                    const float vn = v.x * n.x + v.y * n.y + v.z * n.z;
                    p.x = n.x * boundary;
                    p.y = n.y * boundary;
                    p.z = n.z * boundary;
                    if (vn > 0.0f) {
                        prevPositions_[i].x = p.x - (v.x - n.x * vn * (1.0f + bounce));
                        prevPositions_[i].y = p.y - (v.y - n.y * vn * (1.0f + bounce));
                        prevPositions_[i].z = p.z - (v.z - n.z * vn * (1.0f + bounce));
                        collisionEnergy_[i] = std::max(collisionEnergy_[i], clamp(vn * 8.0f, 0.25f, 1.0f));
                    }
                }
                points_[i] = fromVec(p, points_[i]);
            }

            if (params_.repel > 0.0f || params_.collision > 0.0f) {
                for (uint32_t a = 0; a < active; ++a) {
                    if (!points_[a].enabled) continue;
                    for (uint32_t b = a + 1u; b < active; ++b) {
                        if (!points_[b].enabled) continue;
                        Vec3 pa = toVec(points_[a]);
                        Vec3 pb = toVec(points_[b]);
                        const float dx = pb.x - pa.x;
                        const float dy = pb.y - pa.y;
                        const float dz = pb.z - pa.z;
                        const float d2 = dx * dx + dy * dy + dz * dz;
                        const float d = std::sqrt(std::max(0.0001f, d2));
                        const Vec3 n { dx / d, dy / d, dz / d };
                        float push = 0.0f;
                        if (d2 < minDist2) {
                            const float overlap = minDist - d;
                            push += overlap * (0.42f + params_.impact * 0.18f);
                            Vec3 va {
                                pa.x - prevPositions_[a].x,
                                pa.y - prevPositions_[a].y,
                                pa.z - prevPositions_[a].z
                            };
                            Vec3 vb {
                                pb.x - prevPositions_[b].x,
                                pb.y - prevPositions_[b].y,
                                pb.z - prevPositions_[b].z
                            };
                            const float rel = (vb.x - va.x) * n.x + (vb.y - va.y) * n.y + (vb.z - va.z) * n.z;
                            if (rel < 0.0f) {
                                const float restitution = 0.72f + params_.impact * 0.22f;
                                const float impulse = -(1.0f + restitution) * rel * 0.5f;
                                va.x -= n.x * impulse;
                                va.y -= n.y * impulse;
                                va.z -= n.z * impulse;
                                vb.x += n.x * impulse;
                                vb.y += n.y * impulse;
                                vb.z += n.z * impulse;
                                const float tangentDamp = 0.985f - params_.collision * 0.035f;
                                va.x *= tangentDamp;
                                va.y *= tangentDamp;
                                va.z *= tangentDamp;
                                vb.x *= tangentDamp;
                                vb.y *= tangentDamp;
                                vb.z *= tangentDamp;
                                prevPositions_[a] = { pa.x - va.x, pa.y - va.y, pa.z - va.z };
                                prevPositions_[b] = { pb.x - vb.x, pb.y - vb.y, pb.z - vb.z };
                            }
                            collisionEnergy_[a] = std::max(collisionEnergy_[a], 0.35f + params_.impact * 0.45f);
                            collisionEnergy_[b] = std::max(collisionEnergy_[b], 0.35f + params_.impact * 0.45f);
                        }
                        push += std::min(0.018f, params_.repel * amount * 0.010f / (d2 + 0.08f));
                        if (push > 0.0f) {
                            pa.x -= n.x * push;
                            pa.y -= n.y * push;
                            pa.z -= n.z * push;
                            pb.x += n.x * push;
                            pb.y += n.y * push;
                            pb.z += n.z * push;
                            if (params_.upperHemisphereOnly) {
                                pa.z = std::max(0.0f, pa.z);
                                pb.z = std::max(0.0f, pb.z);
                            }
                            points_[a] = fromVec(pa, points_[a]);
                            points_[b] = fromVec(pb, points_[b]);
                        }
                    }
                }
            }

            applyNearestNeighborSprings(active, spring);
        }

        for (uint32_t i = 0; i < active; ++i) {
            if (params_.upperHemisphereOnly) {
                points_[i].elevationDeg = std::max(0.0f, points_[i].elevationDeg);
            }
            Vec3 p = toVec(points_[i]);
            const float r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            if (r > boundary) {
                const float mul = boundary / std::max(0.0001f, r);
                p.x *= mul;
                p.y *= mul;
                p.z *= mul;
                points_[i] = fromVec(p, points_[i]);
            }
            collisionEnergy_[i] *= 0.86f;
            bondRelease_[i] = std::max(0.0f, bondRelease_[i] - dt);
        }
    }

    void advanceMotion(float dt)
    {
        const uint32_t active = std::min<uint32_t>(params_.activePoints, pointCount_);
        if (params_.motionMode == AmbiPointMotionMode::Off || params_.motionAmount <= 0.0001f || active == 0u) {
            return;
        }
        dt = clamp(dt, 0.0f, 0.05f);
        motionTime_ += dt * params_.rateHz;
        phase_ += dt * params_.rateHz;
        phase_ -= std::floor(phase_);
        const float amount = params_.motionAmount;
        const bool phys = params_.motionMode == AmbiPointMotionMode::Phys;
        const float forceScale = dt * (phys ? 34.0f : 8.0f);
        const float physicsAmount = params_.motionMode == AmbiPointMotionMode::Phys ? (0.25f + amount * 0.75f) : amount;
        const float velocityLimit = phys
            ? (0.035f + amount * 0.055f + params_.impact * 0.070f)
            : (0.012f + amount * 0.018f + params_.impact * 0.020f);

        if (phys) {
            advancePhysicsWorld(dt, active, amount, physicsAmount);
            syncSelectedParamsFromPoint();
            return;
        }

        for (uint32_t i = 0; i < active; ++i) {
            Vec3 p = toVec(points_[i]);
            Vec3 base = toVec(basePoints_[i]);
            Vec3 force {
                -p.x * params_.attract - p.y * params_.swirl,
                -p.y * params_.attract + p.x * params_.swirl,
                -p.z * params_.attract
            };

            if (params_.motionMode == AmbiPointMotionMode::Drift) {
                force.x += std::sin((phase_ + i * 0.071f) * 6.28318530718f) * 0.012f * amount;
                force.y += std::sin((phase_ * 1.31f + i * 0.113f) * 6.28318530718f) * 0.012f * amount;
                force.z += std::sin((phase_ * 1.73f + i * 0.197f) * 6.28318530718f) * 0.008f * amount;
            } else if (params_.motionMode == AmbiPointMotionMode::Orbit) {
                const float wobble = std::sin((phase_ * 0.61f + static_cast<float>(i) * 0.137f) * 6.28318530718f);
                force.x += -p.y * (0.015f + amount * 0.026f);
                force.y += p.x * (0.015f + amount * 0.026f);
                force.x += std::sin(static_cast<float>(i) * 1.19f) * 0.004f * amount;
                force.y += std::cos(static_cast<float>(i) * 1.31f) * 0.004f * amount;
                force.z += wobble * 0.006f * amount;
                force.x += (base.x - p.x) * 0.018f;
                force.y += (base.y - p.y) * 0.018f;
                force.z += (base.z - p.z) * 0.010f;
            } else if (params_.motionMode == AmbiPointMotionMode::Pulse) {
                const float pulse = std::sin(phase_ * 6.28318530718f) * amount;
                force.x += base.x * pulse * 0.060f + (base.x - p.x) * 0.030f;
                force.y += base.y * pulse * 0.060f + (base.y - p.y) * 0.030f;
                force.z += base.z * pulse * 0.050f + (base.z - p.z) * 0.030f;
            } else if (params_.motionMode == AmbiPointMotionMode::Swarm) {
                force.x += randSigned(seed_) * params_.brownian * 0.030f * amount;
                force.y += randSigned(seed_) * params_.brownian * 0.030f * amount;
                force.z += randSigned(seed_) * params_.brownian * 0.024f * amount;
            } else {
                force.x += (base.x - p.x) * 0.006f * (1.0f - amount);
                force.y += (base.y - p.y) * 0.006f * (1.0f - amount);
                force.z += (base.z - p.z) * 0.006f * (1.0f - amount);
                force.x += randSigned(seed_) * (params_.brownian + params_.impact * 0.18f) * 0.018f * physicsAmount;
                force.y += randSigned(seed_) * (params_.brownian + params_.impact * 0.18f) * 0.018f * physicsAmount;
                force.z += randSigned(seed_) * (params_.brownian + params_.impact * 0.18f) * 0.014f * physicsAmount;
                if (params_.motionScene == 3u) {
                    const float beat = std::max(0.0f, std::sin((phase_ * 1.37f + static_cast<float>(i) * 0.071f) * 6.28318530718f));
                    force.x += -p.x * (0.070f + beat * 0.040f) * physicsAmount;
                    force.y += -p.y * (0.070f + beat * 0.040f) * physicsAmount;
                    force.z += (-p.z * 0.035f - 0.055f + beat * 0.040f) * physicsAmount;
                } else if (params_.motionScene == 4u) {
                    const float wave = std::sin((phase_ * 0.73f + static_cast<float>(i) * 0.161f) * 6.28318530718f);
                    force.x += (base.x - p.x) * 0.030f * physicsAmount + wave * 0.026f;
                    force.y += (base.y - p.y) * 0.030f * physicsAmount - wave * 0.022f;
                    force.z += (base.z - p.z) * 0.020f * physicsAmount + std::sin(wave * 2.1f) * 0.016f;
                } else if (params_.motionScene == 5u) {
                    const float kick = std::sin((phase_ * 1.91f + static_cast<float>(i) * 0.379f) * 6.28318530718f);
                    force.x += randSigned(seed_) * 0.075f * physicsAmount + base.x * kick * 0.030f;
                    force.y += randSigned(seed_) * 0.075f * physicsAmount + base.y * kick * 0.030f;
                    force.z += randSigned(seed_) * 0.060f * physicsAmount + base.z * kick * 0.024f;
                }
            }

            velocities_[i].x = (velocities_[i].x + force.x * forceScale) * params_.drag;
            velocities_[i].y = (velocities_[i].y + force.y * forceScale) * params_.drag;
            velocities_[i].z = (velocities_[i].z + force.z * forceScale) * params_.drag;
        }

        if (params_.repel > 0.0f) {
            for (uint32_t a = 0; a < active; ++a) {
                for (uint32_t b = a + 1u; b < active; ++b) {
                    Vec3 pa = toVec(points_[a]);
                    Vec3 pb = toVec(points_[b]);
                    const float dx = pa.x - pb.x;
                    const float dy = pa.y - pb.y;
                    const float dz = pa.z - pb.z;
                    const float d2 = dx * dx + dy * dy + dz * dz + 0.015f;
                    const float d = std::sqrt(d2);
                    const float f = params_.repel * amount * 0.006f * forceScale / d2;
                    const Vec3 push { dx / d * f, dy / d * f, dz / d * f };
                    velocities_[a].x += push.x;
                    velocities_[a].y += push.y;
                    velocities_[a].z += push.z;
                    velocities_[b].x -= push.x;
                    velocities_[b].y -= push.y;
                    velocities_[b].z -= push.z;
                }
            }
        }

        if (params_.motionMode == AmbiPointMotionMode::Phys && (params_.collision > 0.0f || params_.impact > 0.0f)) {
            const float minDist = 0.16f + params_.collision * 0.38f;
            const float minDist2 = minDist * minDist;
            const float sceneBounce = params_.motionScene == 3u ? 0.35f : (params_.motionScene >= 4u ? 0.20f : 0.0f);
            const float bounce = 0.35f + params_.impact * 0.62f + sceneBounce;
            const float pairPushBoost = params_.motionScene == 4u ? 1.45f : (params_.motionScene == 5u ? 1.25f : 1.0f);
            const float pairPush = (0.010f + params_.impact * 0.040f) * physicsAmount * pairPushBoost;
            for (uint32_t a = 0; a < active; ++a) {
                if (!points_[a].enabled) continue;
                for (uint32_t b = a + 1u; b < active; ++b) {
                    if (!points_[b].enabled) continue;
                    const Vec3 pa = toVec(points_[a]);
                    const Vec3 pb = toVec(points_[b]);
                    const float dx = pa.x - pb.x;
                    const float dy = pa.y - pb.y;
                    const float dz = pa.z - pb.z;
                    const float d2 = dx * dx + dy * dy + dz * dz;
                    if (d2 >= minDist2) continue;
                    const float d = std::sqrt(std::max(0.0001f, d2));
                    const Vec3 n { dx / d, dy / d, dz / d };
                    const float overlap = (minDist - d) / minDist;
                    const float impulse = overlap * pairPush * (0.30f + params_.collision * 0.70f);
                    velocities_[a].x += n.x * impulse;
                    velocities_[a].y += n.y * impulse;
                    velocities_[a].z += n.z * impulse;
                    velocities_[b].x -= n.x * impulse;
                    velocities_[b].y -= n.y * impulse;
                    velocities_[b].z -= n.z * impulse;
                    const float rel = (velocities_[a].x - velocities_[b].x) * n.x
                        + (velocities_[a].y - velocities_[b].y) * n.y
                        + (velocities_[a].z - velocities_[b].z) * n.z;
                    if (rel < 0.0f) {
                        const float rebound = -rel * bounce * 0.5f;
                        velocities_[a].x += n.x * rebound;
                        velocities_[a].y += n.y * rebound;
                        velocities_[a].z += n.z * rebound;
                        velocities_[b].x -= n.x * rebound;
                        velocities_[b].y -= n.y * rebound;
                        velocities_[b].z -= n.z * rebound;
                    }
                }
            }
        }

        for (uint32_t i = 0; i < active; ++i) {
            Vec3 p = toVec(points_[i]);
            velocities_[i].x = clamp(velocities_[i].x, -velocityLimit, velocityLimit);
            velocities_[i].y = clamp(velocities_[i].y, -velocityLimit, velocityLimit);
            velocities_[i].z = clamp(velocities_[i].z, -velocityLimit, velocityLimit);
            p.x = clamp(p.x + velocities_[i].x, -1.9f, 1.9f);
            p.y = clamp(p.y + velocities_[i].y, -1.9f, 1.9f);
            p.z = clamp(p.z + velocities_[i].z, -1.9f, 1.9f);
            const float motionBoundary = clamp(params_.physicsScale, 0.25f, 2.0f);
            const float motionR = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            if (motionR > motionBoundary) {
                const float mul = motionBoundary / std::max(0.0001f, motionR);
                p.x *= mul;
                p.y *= mul;
                p.z *= mul;
            }
            if (params_.motionMode == AmbiPointMotionMode::Phys && params_.collision > 0.0f) {
                const float r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
                const float boundary = motionBoundary;
                if (r > boundary) {
                    const float inv = 1.0f / std::max(0.0001f, r);
                    const Vec3 n { p.x * inv, p.y * inv, p.z * inv };
                    p.x = n.x * boundary;
                    p.y = n.y * boundary;
                    p.z = n.z * boundary;
                    const float vn = velocities_[i].x * n.x + velocities_[i].y * n.y + velocities_[i].z * n.z;
                    if (vn > 0.0f) {
                        const float bounce = 1.25f + params_.impact * 1.75f;
                        velocities_[i].x -= n.x * vn * bounce;
                        velocities_[i].y -= n.y * vn * bounce;
                        velocities_[i].z -= n.z * vn * bounce;
                    }
                }
            }
            if (params_.upperHemisphereOnly) {
                p.z = std::max(0.0f, p.z);
                if (params_.motionMode == AmbiPointMotionMode::Phys && params_.collision > 0.0f && velocities_[i].z < 0.0f) {
                    velocities_[i].z = -velocities_[i].z * (0.45f + params_.impact * 0.45f);
                } else {
                    velocities_[i].z = std::max(0.0f, velocities_[i].z);
                }
            }
            points_[i] = fromVec(p, points_[i]);
            if (params_.upperHemisphereOnly) {
                points_[i].elevationDeg = std::max(0.0f, points_[i].elevationDeg);
            }
        }
        syncSelectedParamsFromPoint();
    }

    void smoothScene()
    {
        const uint32_t active = std::min<uint32_t>(params_.activePoints, pointCount_);
        for (uint32_t i = 0; i < pointCount_; ++i) {
            auto& s = smoothPoints_[i];
            const auto p = scaledForMotionView(points_[i]);
            const float daz = wrapSignedDeg(p.azimuthDeg - s.azimuthDeg);
            s.azimuthDeg = wrapSignedDeg(s.azimuthDeg + daz * 0.12f);
            s.elevationDeg += (p.elevationDeg - s.elevationDeg) * 0.12f;
            s.distance += (p.distance - s.distance) * 0.12f;
            s.gain += (p.gain - s.gain) * 0.12f;
            s.enabled = i < active && p.enabled;
            s.solo = i < active && p.solo;
        }
    }

    double sampleRate_ = 48000.0;
    uint32_t pointCount_ = kAmbiPointEncoderPrototypePoints;
    AmbiPointEncoderParams params_ {};
    std::array<AmbiPoint, kAmbiPointEncoderMaxPoints> points_ {};
    std::array<AmbiPoint, kAmbiPointEncoderMaxPoints> basePoints_ {};
    std::array<AmbiPoint, kAmbiPointEncoderMaxPoints> smoothPoints_ {};
    std::array<Vec3, kAmbiPointEncoderMaxPoints> velocities_ {};
    std::array<Vec3, kAmbiPointEncoderMaxPoints> prevPositions_ {};
    std::array<float, kAmbiPointEncoderMaxPoints> collisionEnergy_ {};
    std::array<float, kAmbiPointEncoderMaxPoints> bondRelease_ {};
    Vec3 perturbationSource_ {};
    Vec3 prevPerturbationSource_ {};
    float phase_ = 0.0f;
    float motionTime_ = 0.0f;
    uint32_t seed_ = 0x1234abcdU;
    bool perturbationPrimed_ = false;
};

} // namespace s3g
