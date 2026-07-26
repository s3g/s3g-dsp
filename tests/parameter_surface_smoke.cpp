#include "s3g_ambi_stochastic_encoder.h"
#include "s3g_ambi_wrangler_encoder.h"
#include "s3g_parameter_surface.h"

#include <array>
#include <cmath>
#include <cstdio>

namespace {

struct Params {
    float value = 0.0f;
    float angle = 0.0f;
    uint32_t mode = 0u;
};

bool near(float a, float b, float tolerance = 1.0e-4f)
{
    return std::fabs(a - b) <= tolerance;
}

float wrapDegrees(float degrees)
{
    while (degrees > 180.0f) degrees -= 360.0f;
    while (degrees < -180.0f) degrees += 360.0f;
    return degrees;
}

} // namespace

int main()
{
    s3g::ParameterSurfaceState<Params> surface {};
    Params left {};
    left.value = 0.0f;
    left.angle = 170.0f;
    left.mode = 1u;
    Params right {};
    right.value = 1.0f;
    right.angle = -170.0f;
    right.mode = 2u;
    if (!s3g::addParameterSurfaceCell(surface, left, 0, "LEFT")
        || !s3g::addParameterSurfaceCell(surface, right, 1, "RIGHT")) {
        std::fprintf(stderr, "could not add surface cells\n");
        return 1;
    }
    surface.cells[0].x = 0.0f;
    surface.cells[0].y = 0.5f;
    surface.cells[1].x = 1.0f;
    surface.cells[1].y = 0.5f;
    surface.enabled = 1u;
    s3g::sanitizeParameterSurface(surface);

    const auto center = s3g::parameterSurfaceWeights(surface, 0.5f, 0.5f);
    const float value = s3g::parameterSurfaceBlend(surface, center,
        [](const Params& p) { return p.value; }, -1.0f);
    if (!near(value, 0.5f)) {
        std::fprintf(stderr, "center blend mismatch: %.6f\n", value);
        return 1;
    }
    const float angle = s3g::parameterSurfaceBlendAngleDegrees(surface, center,
        [](const Params& p) { return p.angle; }, 0.0f);
    if (std::fabs(std::fabs(angle) - 180.0f) > 0.01f) {
        std::fprintf(stderr, "angle did not take shortest path: %.6f\n", angle);
        return 1;
    }
    if (s3g::parameterSurfaceNearestParams(surface, center, left).mode != 1u) {
        std::fprintf(stderr, "nearest-cell tie was not stable\n");
        return 1;
    }

    const auto curveBlend = [&](s3g::ParameterSurfaceCurve curve) {
        surface.curve = curve;
        const auto weights = s3g::parameterSurfaceWeights(
            surface, 0.25f, 0.5f);
        return s3g::parameterSurfaceBlend(surface, weights,
            [](const Params& p) { return p.value; }, -1.0f);
    };
    const float soft = curveBlend(s3g::ParameterSurfaceCurve::Soft);
    const float linear = curveBlend(s3g::ParameterSurfaceCurve::Linear);
    const float smooth = curveBlend(s3g::ParameterSurfaceCurve::Smooth);
    const float tight = curveBlend(s3g::ParameterSurfaceCurve::Tight);
    if (!(soft > linear && linear > smooth && smooth > tight)) {
        std::fprintf(stderr,
            "surface curve ordering mismatch: %.6f %.6f %.6f %.6f\n",
            soft, linear, smooth, tight);
        return 1;
    }
    surface.curve = s3g::ParameterSurfaceCurve::Linear;

    const float glideStep = s3g::parameterSurfaceGlideCoefficient(
        1000.0f, 0.1f);
    float glidePosition = 0.0f;
    for (uint32_t step = 0u; step < 10u; ++step) {
        glidePosition += (1.0f - glidePosition) * glideStep;
    }
    if (!near(glidePosition, 0.99f, 0.001f)
        || s3g::parameterSurfaceGlideCoefficient(0.0f, 0.1f) != 1.0f
        || s3g::parameterSurfaceGlideCoefficient(160.0f, 0.0f) != 0.0f) {
        std::fprintf(stderr, "surface glide timing mismatch: %.6f\n",
            glidePosition);
        return 1;
    }

    const auto exact = s3g::parameterSurfaceWeights(surface, 1.0f, 0.5f);
    if (!exact.exact
        || !near(s3g::parameterSurfaceBlend(surface, exact,
            [](const Params& p) { return p.value; }, -1.0f), 1.0f)
        || s3g::parameterSurfaceNearestParams(surface, exact, left).mode != 2u) {
        std::fprintf(stderr, "exact cell recall mismatch\n");
        return 1;
    }

    if (!s3g::removeParameterSurfaceCell(surface, 0u)
        || surface.cellCount != 1u || surface.enabled != 0u) {
        std::fprintf(stderr, "surface removal/safety mismatch\n");
        return 1;
    }

    s3g::ParameterSurfaceState<Params> expanded {};
    for (uint32_t index = 0u;
        index < s3g::kParameterSurfaceMaxCells; ++index) {
        Params valueParams {};
        valueParams.value = static_cast<float>(index);
        if (!s3g::addParameterSurfaceCell(
                expanded, valueParams, static_cast<int32_t>(index), "CELL")) {
            std::fprintf(stderr, "surface stopped at cell %u\n", index);
            return 1;
        }
    }
    if (expanded.cellCount != s3g::kParameterSurfaceMaxCells
        || s3g::kParameterSurfaceMaxCells <= 8u
        || s3g::addParameterSurfaceCell(expanded, Params {}, -1, "EXTRA")) {
        std::fprintf(stderr, "expanded surface capacity mismatch\n");
        return 1;
    }

    s3g::ParameterSurfaceStateV1<Params> legacy {};
    legacy.enabled = 1u;
    legacy.cellCount = 2u;
    legacy.focus = 3.0f;
    legacy.cells[0] = expanded.cells[0];
    legacy.cells[1] = expanded.cells[1];
    const auto migrated = s3g::migrateParameterSurfaceV1(legacy);
    if (migrated.version != s3g::kParameterSurfaceStateVersion
        || migrated.cellCount != 2u || migrated.enabled != 1u
        || !near(migrated.focus, 3.0f)
        || migrated.curve != s3g::ParameterSurfaceCurve::Linear
        || !near(migrated.glideMs,
            s3g::kParameterSurfaceDefaultGlideMs)) {
        std::fprintf(stderr, "version-one surface migration mismatch\n");
        return 1;
    }
    s3g::ParameterSurfaceStateV2<Params> versionTwo {};
    versionTwo.enabled = 1u;
    versionTwo.cellCount = 2u;
    versionTwo.focus = 4.0f;
    versionTwo.cells[0] = expanded.cells[0];
    versionTwo.cells[1] = expanded.cells[1];
    const auto migratedV2 = s3g::migrateParameterSurfaceV2(versionTwo);
    if (migratedV2.version != s3g::kParameterSurfaceStateVersion
        || migratedV2.cellCount != 2u || migratedV2.enabled != 1u
        || !near(migratedV2.focus, 4.0f)
        || migratedV2.curve != s3g::ParameterSurfaceCurve::Linear
        || !near(migratedV2.glideMs,
            s3g::kParameterSurfaceDefaultGlideMs)) {
        std::fprintf(stderr, "version-two surface migration mismatch\n");
        return 1;
    }

    std::array<std::array<float, 16u>, 4u> outputStorage {};
    std::array<float*, 4u> outputs {};
    for (uint32_t channel = 0u; channel < outputs.size(); ++channel) {
        outputs[channel] = outputStorage[channel].data();
    }

    s3g::AmbiStochasticEncoder stochastic {};
    stochastic.prepare(48000.0);
    stochastic.process(outputs.data(), outputs.size(), 16u);
    const float stochasticBefore = stochastic.points()[0].azimuthDeg;
    auto stochasticParams = stochastic.params();
    stochasticParams.centerAzimuthDeg = wrapDegrees(
        stochasticParams.centerAzimuthDeg + 120.0f);
    stochastic.setParameterSurfaceGlideMs(1000.0f);
    stochastic.setParams(stochasticParams);
    if (!near(stochastic.points()[0].azimuthDeg, stochasticBefore)) {
        std::fprintf(stderr, "stochastic point jumped at surface update\n");
        return 1;
    }
    stochastic.process(outputs.data(), outputs.size(), 16u);
    const float stochasticMove = std::fabs(wrapDegrees(
        stochastic.points()[0].azimuthDeg - stochasticBefore));
    if (!(stochasticMove > 0.001f && stochasticMove < 20.0f)) {
        std::fprintf(stderr, "stochastic point glide mismatch: %.6f\n",
            stochasticMove);
        return 1;
    }

    s3g::AmbiWranglerEncoder wrangler {};
    wrangler.prepare(48000.0);
    wrangler.process(outputs.data(), outputs.size(), 16u);
    const float wranglerBefore = wrangler.voicePoint(0u).azimuthDeg;
    auto wranglerParams = wrangler.params();
    wranglerParams.centerAzimuthDeg = wrapDegrees(
        wranglerParams.centerAzimuthDeg + 120.0f);
    wrangler.setParameterSurfaceGlideMs(1000.0f);
    wrangler.setParams(wranglerParams);
    wrangler.process(outputs.data(), outputs.size(), 16u);
    const float wranglerMove = std::fabs(wrapDegrees(
        wrangler.voicePoint(0u).azimuthDeg - wranglerBefore));
    if (!(wranglerMove > 0.001f && wranglerMove < 20.0f)) {
        std::fprintf(stderr, "wrangler point glide mismatch: %.6f\n",
            wranglerMove);
        return 1;
    }
    return 0;
}
