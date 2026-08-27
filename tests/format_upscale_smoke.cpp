#include "s3g_format_upscale.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <utility>

namespace {

bool near(float actual, float expected, float tolerance = 0.0002f)
{
    return std::abs(actual - expected) <= tolerance;
}

bool check(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

float routePower(const s3g::FormatUpscale& upscale, uint32_t input)
{
    float power = 0.0f;
    for (uint32_t output = 0u; output < upscale.activeOutputs(); ++output) {
        const float anchor = upscale.targetAnchorGain(input, output);
        const float extension = upscale.targetExtensionGain(input, output);
        power += anchor * anchor + extension * extension;
    }
    return power;
}

float columnPower(const s3g::FormatUpscale& upscale, uint32_t output)
{
    float power = 0.0f;
    for (uint32_t input = 0u; input < upscale.activeInputs(); ++input) {
        const float gain = upscale.targetAnchorGain(input, output)
            + upscale.targetExtensionGain(input, output);
        power += gain * gain;
    }
    return power;
}

} // namespace

int main()
{
    bool ok = true;
    auto upscale = std::make_unique<s3g::FormatUpscale>();
    upscale->prepare(48000.0);

    ok &= check(upscale->activeInputs() == 2u,
        "default input is not stereo");
    ok &= check(upscale->activeOutputs() == 4u,
        "default output is not quad");
    ok &= check(near(upscale->inputLayout().speakers[0u].azimuthDeg, -30.0f)
            && upscale->inputLayout().speakers[0u].role
                == s3g::FormatUpscaleRole::Right
            && near(upscale->inputLayout().speakers[1u].azimuthDeg, 30.0f)
            && upscale->inputLayout().speakers[1u].role
                == s3g::FormatUpscaleRole::Left,
        "stereo does not follow the s3g AED right-to-left channel policy");
    ok &= check(near(upscale->outputLayout().speakers[0u].azimuthDeg, 45.0f)
            && near(upscale->outputLayout().speakers[1u].azimuthDeg, -45.0f)
            && near(upscale->outputLayout().speakers[2u].azimuthDeg, -135.0f)
            && near(upscale->outputLayout().speakers[3u].azimuthDeg, 135.0f),
        "quad does not use the canonical panner/decoder channel order");
    constexpr float invSqrt2 = 0.70710678f;
    ok &= check(near(upscale->targetAnchorGain(0u, 1u), invSqrt2),
        "same-side right anchor is not constant-power");
    ok &= check(near(upscale->targetExtensionGain(0u, 2u), invSqrt2),
        "same-side right-rear route is missing");
    ok &= check(near(upscale->targetAnchorGain(1u, 0u), invSqrt2),
        "same-side left anchor is not constant-power");
    ok &= check(near(upscale->targetExtensionGain(1u, 3u), invSqrt2),
        "same-side left-rear route is missing");
    ok &= check(near(routePower(*upscale, 0u), 1.0f)
            && near(routePower(*upscale, 1u), 1.0f),
        "share policy does not conserve per-input electrical power");

    std::array<float, s3g::kFormatUpscaleMaxChannels> input {};
    std::array<float, s3g::kFormatUpscaleMaxChannels> output {};
    input[0] = 1.0f;
    input[1] = 2.0f;
    upscale->processFrame(input.data(), 64u, output.data(), 64u);
    ok &= check(near(output[0], invSqrt2 * 2.0f)
            && near(output[1], invSqrt2)
            && near(output[2], invSqrt2)
            && near(output[3], invSqrt2 * 2.0f),
        "same-side stereo-to-quad signal routing is incorrect");
    for (uint32_t channel = 4u; channel < output.size(); ++channel)
        ok &= check(output[channel] == 0.0f,
            "inactive output channel was not cleared");

    auto params = upscale->params();
    params.placement = s3g::FormatUpscalePlacement::Cross;
    params.smoothingMs = 1.0f;
    upscale->setParams(params);
    upscale->reset();
    ok &= check(near(upscale->targetExtensionGain(0u, 3u), invSqrt2)
            && near(upscale->targetExtensionGain(1u, 2u), invSqrt2),
        "cross placement did not swap rear ownership");

    params.inputLayout = s3g::FormatUpscaleLayout::FiveZero;
    params.outputLayout = s3g::FormatUpscaleLayout::SevenZero;
    params.placement = s3g::FormatUpscalePlacement::SameSide;
    params.basis = s3g::FormatUpscaleBasis::Direct;
    params.origin = s3g::FormatUpscaleOrigin::Share;
    params.copies = 2u;
    upscale->setParams(params);
    upscale->reset();
    ok &= check(upscale->activeInputs() == 5u
            && upscale->activeOutputs() == 7u,
        "5.0-to-7.0 format counts are incorrect");
    ok &= check(near(upscale->targetAnchorGain(1u, 1u), invSqrt2)
            && near(upscale->targetExtensionGain(1u, 2u), invSqrt2),
        "right surround did not split to right rear");
    ok &= check(near(upscale->targetAnchorGain(2u, 4u), invSqrt2)
            && near(upscale->targetExtensionGain(2u, 3u), invSqrt2),
        "left surround did not split to left rear");
    ok &= check(near(upscale->targetAnchorGain(4u, 6u), 1.0f),
        "center anchor should remain unchanged when it has no extension");

    params.outputLayout = s3g::FormatUpscaleLayout::Stereo;
    upscale->setParams(params);
    ok &= check(upscale->activeInputs() == 5u
            && upscale->activeOutputs() == 5u,
        "a narrower output format was not rejected safely");

    params.inputLayout = s3g::FormatUpscaleLayout::Mono;
    params.outputLayout = s3g::FormatUpscaleLayout::Ring64;
    params.placement = s3g::FormatUpscalePlacement::Interleave;
    params.basis = s3g::FormatUpscaleBasis::Direct;
    params.origin = s3g::FormatUpscaleOrigin::Share;
    params.copies = 64u;
    upscale->setParams(params);
    uint32_t monoDestinations = 0u;
    for (uint32_t outputIndex = 0u; outputIndex < 64u; ++outputIndex) {
        const float gain = upscale->targetAnchorGain(0u, outputIndex)
            + upscale->targetExtensionGain(0u, outputIndex);
        if (std::abs(gain) > 0.0001f) ++monoDestinations;
    }
    ok &= check(upscale->activeOutputs() == 64u
            && monoDestinations == 64u
            && near(routePower(*upscale, 0u), 1.0f),
        "mono-to-ring-64 did not fill the full constant-power destination");

    params.inputLayout = s3g::FormatUpscaleLayout::ElevenZeroEight;
    params.outputLayout = s3g::FormatUpscaleLayout::ElevenZeroEight;
    params.placement = s3g::FormatUpscalePlacement::Match;
    upscale->setParams(params);
    ok &= check(upscale->activeInputs() == 19u
            && upscale->activeOutputs() == 19u,
        "missing 11.0.8 layout does not expose 19 ordered channels");
    const std::array<std::pair<s3g::FormatUpscaleLayout, uint32_t>, 17u>
        addedLayouts {{
            { s3g::FormatUpscaleLayout::SixZero, 6u },
            { s3g::FormatUpscaleLayout::QuadOverhead6, 6u },
            { s3g::FormatUpscaleLayout::FiveZeroTwo, 7u },
            { s3g::FormatUpscaleLayout::SevenZeroTwo, 9u },
            { s3g::FormatUpscaleLayout::NineZero, 9u },
            { s3g::FormatUpscaleLayout::NineZeroTwo, 11u },
            { s3g::FormatUpscaleLayout::NineZeroFour, 13u },
            { s3g::FormatUpscaleLayout::NineZeroSix, 15u },
            { s3g::FormatUpscaleLayout::SevenZeroSix, 13u },
            { s3g::FormatUpscaleLayout::ElevenZeroEight, 19u },
            { s3g::FormatUpscaleLayout::Cube17, 17u },
            { s3g::FormatUpscaleLayout::Cube41, 41u },
            { s3g::FormatUpscaleLayout::Lpac41, 41u },
            { s3g::FormatUpscaleLayout::Srst25, 25u },
            { s3g::FormatUpscaleLayout::Dome25, 25u },
            { s3g::FormatUpscaleLayout::Icosahedron20, 20u },
            { s3g::FormatUpscaleLayout::Sphere24, 24u },
        }};
    for (const auto& [layout, count] : addedLayouts)
        ok &= check(s3g::formatUpscaleLayoutChannels(layout) == count,
            "an added shared-policy layout has the wrong channel count");
    const auto cube41 = s3g::formatUpscaleLayoutData(
        s3g::FormatUpscaleLayout::Cube41);
    std::array<float, 8u> cubePlaneHeights {};
    std::array<uint32_t, 8u> cubePlaneCounts {};
    std::array<float, 8u> cubePlaneMinimumElevations {};
    std::array<float, 8u> cubePlaneMaximumElevations {};
    cubePlaneMinimumElevations.fill(90.0f);
    cubePlaneMaximumElevations.fill(-90.0f);
    uint32_t cubePlaneCount = 0u;
    for (uint32_t speaker = 0u; speaker < cube41.count; ++speaker) {
        const float height = s3g::formatUpscaleSpeakerHeight(
            cube41.speakers[speaker]);
        uint32_t plane = 0u;
        while (plane < cubePlaneCount
            && std::abs(cubePlaneHeights[plane] - height)
                >= s3g::kFormatUpscaleTierHeightTolerance)
            ++plane;
        if (plane == cubePlaneCount)
            cubePlaneHeights[cubePlaneCount++] = height;
        ++cubePlaneCounts[plane];
        cubePlaneMinimumElevations[plane] = std::min(
            cubePlaneMinimumElevations[plane],
            cube41.speakers[speaker].elevationDeg);
        cubePlaneMaximumElevations[plane] = std::max(
            cubePlaneMaximumElevations[plane],
            cube41.speakers[speaker].elevationDeg);
    }
    uint32_t cubeTopPlane = 0u;
    for (uint32_t plane = 1u; plane < cubePlaneCount; ++plane)
        if (cubePlaneHeights[plane] > cubePlaneHeights[cubeTopPlane])
            cubeTopPlane = plane;
    ok &= check(cubePlaneCount == 4u
            && cubePlaneCounts[cubeTopPlane] == 5u
            && cubePlaneMinimumElevations[cubeTopPlane] > 56.0f
            && cubePlaneMinimumElevations[cubeTopPlane] < 58.0f
            && near(cubePlaneMaximumElevations[cubeTopPlane], 90.0f, 0.01f),
        "Cube 41 height planes were mislabeled as exact elevation tiers");
    ok &= check(near(upscale->inputLayout().speakers[0u].azimuthDeg, -30.0f)
            && near(upscale->inputLayout().speakers[5u].azimuthDeg, 180.0f)
            && near(upscale->inputLayout().speakers[10u].azimuthDeg, 0.0f)
            && near(upscale->inputLayout().speakers[11u].azimuthDeg, -30.0f)
            && near(upscale->inputLayout().speakers[11u].elevationDeg, 55.0f)
            && near(upscale->inputLayout().speakers[18u].azimuthDeg, 30.0f),
        "11.0.8 channel order diverges from the layout panner policy");

    const auto customInput = s3g::formatUpscaleDefaultCustomInputLayout();
    const auto customOutput =
        s3g::formatUpscaleDefaultThreeTierOutputLayout();
    upscale->setCustomInputLayout(customInput);
    upscale->setCustomOutputLayout(customOutput);
    params.inputLayout = s3g::FormatUpscaleLayout::Custom;
    params.outputLayout = s3g::FormatUpscaleLayout::Custom;
    params.placement = s3g::FormatUpscalePlacement::Interleave;
    params.basis = s3g::FormatUpscaleBasis::Direct;
    params.origin = s3g::FormatUpscaleOrigin::Share;
    params.copies = 3u;
    upscale->setParams(params);
    ok &= check(upscale->activeInputs() == 3u
            && upscale->activeOutputs() == 9u,
        "custom 3-input to 9-output layout counts are incorrect");
    ok &= check(near(upscale->outputLayout().speakers[0u].elevationDeg, -35.0f)
            && near(upscale->outputLayout().speakers[3u].elevationDeg, 0.0f)
            && near(upscale->outputLayout().speakers[6u].elevationDeg, 45.0f)
            && near(upscale->outputLayout().speakers[0u].distance, 1.15f)
            && near(upscale->outputLayout().speakers[6u].distance, 0.9f),
        "custom three-tier AED coordinates were not retained");
    for (uint32_t source = 0u; source < 3u; ++source)
        ok &= check(near(routePower(*upscale, source), 1.0f),
            "custom three-tier routing is not constant-power");

    const std::array<s3g::FormatUpscalePlacement, 8u> tierModes {{
        s3g::FormatUpscalePlacement::Match,
        s3g::FormatUpscalePlacement::SameSide,
        s3g::FormatUpscalePlacement::Cross,
        s3g::FormatUpscalePlacement::Rotate,
        s3g::FormatUpscalePlacement::Interleave,
        s3g::FormatUpscalePlacement::Nearest,
        s3g::FormatUpscalePlacement::Span,
        s3g::FormatUpscalePlacement::TierFill,
    }};
    params.copies = 1u;
    for (const auto mode : tierModes) {
        params.placement = mode;
        upscale->setParams(params);
        upscale->useAutomaticRoutes();
        std::array<uint32_t, 9u> destinationOwners {};
        for (uint32_t source = 0u; source < 3u; ++source) {
            std::array<uint32_t, 3u> tierDestinations {};
            for (uint32_t tier = 0u; tier < 3u; ++tier) {
                for (uint32_t offset = 0u; offset < 3u; ++offset) {
                    const uint32_t outputIndex = tier * 3u + offset;
                    const float gain = upscale->targetAnchorGain(
                            source, outputIndex)
                        + upscale->targetExtensionGain(source, outputIndex);
                    if (std::abs(gain) > 0.0001f) {
                        ++tierDestinations[tier];
                        ++destinationOwners[outputIndex];
                    }
                }
            }
            const bool tierCoverage = tierDestinations[0u] == 1u
                && tierDestinations[1u] == 1u
                && tierDestinations[2u] == 1u
                && near(routePower(*upscale, source), 1.0f);
            if (!tierCoverage)
                std::cerr << "Auto mode "
                    << s3g::formatUpscalePlacementName(mode) << " source "
                    << source << " tier destinations "
                    << tierDestinations[0u] << "/"
                    << tierDestinations[1u] << "/"
                    << tierDestinations[2u] << '\n';
            ok &= check(tierCoverage,
                "an auto-map mode did not distribute each input evenly across every available tier");
        }
        for (uint32_t outputIndex = 0u; outputIndex < 9u; ++outputIndex) {
            if (destinationOwners[outputIndex] != 1u)
                std::cerr << "Auto mode "
                    << s3g::formatUpscalePlacementName(mode) << " output "
                    << outputIndex << " owners "
                    << destinationOwners[outputIndex] << '\n';
            ok &= check(destinationOwners[outputIndex] == 1u,
                "an auto-map mode did not distribute sources evenly within an available tier");
        }
    }
    params.placement = s3g::FormatUpscalePlacement::Interleave;
    params.copies = 3u;
    upscale->setParams(params);

    s3g::FormatUpscaleLayoutData sparseInput {};
    sparseInput.count = 4u;
    constexpr float sparseAzimuths[4] { -45.0f, 45.0f, -135.0f, 135.0f };
    for (uint32_t source = 0u; source < sparseInput.count; ++source)
        s3g::formatUpscaleSetSpeaker(sparseInput, source,
            sparseAzimuths[source], 0.0f, s3g::FormatUpscaleRole::Generic);
    s3g::FormatUpscaleLayoutData sparseOutput {};
    sparseOutput.count = 6u;
    for (uint32_t outputIndex = 0u; outputIndex < 4u; ++outputIndex)
        s3g::formatUpscaleSetSpeaker(sparseOutput, outputIndex,
            sparseAzimuths[outputIndex], 0.0f,
            s3g::FormatUpscaleRole::Generic);
    s3g::formatUpscaleSetSpeaker(sparseOutput, 4u, -45.0f, 45.0f,
        s3g::FormatUpscaleRole::Generic);
    s3g::formatUpscaleSetSpeaker(sparseOutput, 5u, 45.0f, 45.0f,
        s3g::FormatUpscaleRole::Generic);
    upscale->setCustomInputLayout(sparseInput);
    upscale->setCustomOutputLayout(sparseOutput);
    params.placement = s3g::FormatUpscalePlacement::TierFill;
    params.copies = 1u;
    upscale->setParams(params);
    upscale->useAutomaticRoutes();
    std::array<uint32_t, 6u> sparseOwners {};
    for (uint32_t source = 0u; source < 4u; ++source) {
        uint32_t lowerRoutes = 0u;
        uint32_t upperRoutes = 0u;
        for (uint32_t outputIndex = 0u; outputIndex < 6u; ++outputIndex) {
            const float gain = upscale->targetAnchorGain(source, outputIndex)
                + upscale->targetExtensionGain(source, outputIndex);
            if (std::abs(gain) <= 0.0001f) continue;
            ++sparseOwners[outputIndex];
            if (outputIndex < 4u) ++lowerRoutes;
            else ++upperRoutes;
        }
        ok &= check(lowerRoutes >= 1u && upperRoutes >= 1u
                && near(routePower(*upscale, source), 1.0f),
            "tier fill did not route an input through a sparse elevation tier");
    }
    ok &= check(sparseOwners[0u] >= 1u && sparseOwners[1u] >= 1u
            && sparseOwners[2u] >= 1u && sparseOwners[3u] >= 1u
            && sparseOwners[4u] == 2u && sparseOwners[5u] == 2u,
        "tier fill did not balance shared routes across sparse-tier columns");

    params.inputLayout = s3g::FormatUpscaleLayout::OctophonicRing;
    params.outputLayout = s3g::FormatUpscaleLayout::Srst25;
    params.placement = s3g::FormatUpscalePlacement::SameSide;
    params.copies = 2u;
    upscale->setParams(params);
    std::array<uint32_t, 25u> sameSideExtensionOwners {};
    uint32_t sameSideExtensionColumns = 0u;
    for (uint32_t source = 0u; source < 8u; ++source) {
        uint32_t extensionCount = 0u;
        for (uint32_t outputIndex = 0u; outputIndex < 25u; ++outputIndex) {
            if (std::abs(upscale->targetExtensionGain(source, outputIndex))
                <= 0.0001f) continue;
            ++extensionCount;
            ++sameSideExtensionOwners[outputIndex];
        }
        if (extensionCount != 1u)
            std::cerr << "Same-side SRST source " << source
                << " at A"
                << upscale->inputLayout().speakers[source].azimuthDeg
                << " has " << extensionCount << " extensions\n";
        ok &= check(extensionCount == 1u,
            "copies=2 did not produce one anchor plus one extension");
    }
    for (const uint32_t owners : sameSideExtensionOwners) {
        if (owners > 0u) ++sameSideExtensionColumns;
        ok &= check(owners <= 1u,
            "same-side automap reused an output before an eligible column");
    }
    if (sameSideExtensionColumns != 8u) {
        std::cerr << "Same-side SRST extension owners:";
        for (uint32_t outputIndex = 0u; outputIndex < 25u; ++outputIndex)
            if (sameSideExtensionOwners[outputIndex] > 0u)
                std::cerr << " O" << outputIndex + 1u << "="
                    << sameSideExtensionOwners[outputIndex];
        std::cerr << '\n';
        std::cerr << "Same-side SRST anchors:";
        for (uint32_t outputIndex = 0u; outputIndex < 25u; ++outputIndex) {
            for (uint32_t source = 0u; source < 8u; ++source) {
                if (std::abs(upscale->targetAnchorGain(source, outputIndex))
                    > 0.0001f)
                    std::cerr << " O" << outputIndex + 1u << "=I"
                        << source + 1u;
            }
        }
        std::cerr << '\n';
    }
    ok &= check(sameSideExtensionColumns == 8u,
        "same-side automap did not globally load-balance its added copies");

    params.placement = s3g::FormatUpscalePlacement::TierFill;
    upscale->setParams(params);
    std::array<bool, 25u> tierFillColumns {};
    std::array<float, 25u> srstTierElevations {};
    std::array<uint32_t, 25u> srstOutputTiers {};
    uint32_t srstTierCount = 0u;
    for (uint32_t outputIndex = 0u; outputIndex < 25u; ++outputIndex) {
        const float elevation = upscale->outputLayout()
            .speakers[outputIndex].elevationDeg;
        uint32_t tier = 0u;
        while (tier < srstTierCount
            && std::abs(srstTierElevations[tier] - elevation) >= 0.5f)
            ++tier;
        if (tier == srstTierCount) srstTierElevations[srstTierCount++] = elevation;
        srstOutputTiers[outputIndex] = tier;
    }
    for (uint32_t source = 0u; source < 8u; ++source) {
        std::array<bool, 25u> sourceTiers {};
        for (uint32_t outputIndex = 0u; outputIndex < 25u; ++outputIndex) {
            const float gain = upscale->targetAnchorGain(source, outputIndex)
                + upscale->targetExtensionGain(source, outputIndex);
            if (std::abs(gain) <= 0.0001f) continue;
            tierFillColumns[outputIndex] = true;
            sourceTiers[srstOutputTiers[outputIndex]] = true;
        }
        for (uint32_t tier = 0u; tier < srstTierCount; ++tier)
            ok &= check(sourceTiers[tier],
                "tier fill did not reach every SRST elevation tier");
        ok &= check(near(routePower(*upscale, source), 1.0f),
            "tier fill did not retain row-normalized power");
    }
    for (const bool used : tierFillColumns)
        ok &= check(used,
            "tier fill left an SRST output column unassigned");

    params.inputLayout = s3g::FormatUpscaleLayout::Stereo;
    params.outputLayout = s3g::FormatUpscaleLayout::Quad;
    params.placement = s3g::FormatUpscalePlacement::SameSide;
    params.copies = 2u;
    upscale->setParams(params);
    upscale->setAutoRowShape(s3g::FormatUpscaleRowShape::Center);
    ok &= check(upscale->targetAnchorGain(0u, 1u)
                > upscale->targetExtensionGain(0u, 2u)
            && near(routePower(*upscale, 0u), 1.0f),
        "center auto row shape did not favor the spatially centered route");
    upscale->setAutoRowShape(s3g::FormatUpscaleRowShape::Edges);
    ok &= check(upscale->targetAnchorGain(0u, 1u)
                < upscale->targetExtensionGain(0u, 2u)
            && near(routePower(*upscale, 0u), 1.0f),
        "edges auto row shape did not favor the outer route");
    upscale->setAutoRowShape(s3g::FormatUpscaleRowShape::Flat);

    upscale->setCustomInputLayout(customInput);
    upscale->setCustomOutputLayout(customOutput);
    params.inputLayout = s3g::FormatUpscaleLayout::Custom;
    params.outputLayout = s3g::FormatUpscaleLayout::Custom;
    params.placement = s3g::FormatUpscalePlacement::Interleave;
    params.copies = 3u;
    upscale->setParams(params);

    upscale->clearManualRoutes();
    upscale->setManualRoute(0u, 0u, true);
    upscale->setManualRoute(0u, 3u, true);
    upscale->setManualRoute(0u, 6u, true);
    constexpr float invSqrt3 = 0.577350269f;
    ok &= check(upscale->manualRoutesActive()
            && upscale->manualRoute(0u, 0u)
            && upscale->manualRoute(0u, 3u)
            && upscale->manualRoute(0u, 6u),
        "directly drawn connection graph was not retained");
    ok &= check(near(upscale->targetExtensionGain(0u, 0u), invSqrt3)
            && near(upscale->targetExtensionGain(0u, 3u), invSqrt3)
            && near(upscale->targetExtensionGain(0u, 6u), invSqrt3)
            && near(routePower(*upscale, 0u), 1.0f),
        "drawn connections are not equal-power normalized");
    upscale->setManualWeight(0u, 3u, 0.5f);
    constexpr float weightedFull = 0.666666667f;
    constexpr float weightedHalf = 0.333333333f;
    ok &= check(near(upscale->manualWeight(0u, 3u), 0.5f)
            && near(upscale->targetExtensionGain(0u, 0u), weightedFull)
            && near(upscale->targetExtensionGain(0u, 3u), weightedHalf)
            && near(upscale->targetExtensionGain(0u, 6u), weightedFull)
            && near(routePower(*upscale, 0u), 1.0f),
        "weighted matrix connections are not power-normalized");
    upscale->setManualRoute(0u, 3u, false);
    ok &= check(!upscale->manualRoute(0u, 3u)
            && near(upscale->targetExtensionGain(0u, 0u), invSqrt2)
            && near(upscale->targetExtensionGain(0u, 6u), invSqrt2),
        "drawing the same connection again did not remove and renormalize it");

    // Row normalization preserves each distributed input, while column
    // normalization protects an output that receives several inputs. A
    // rectangular matrix cannot make every row and every column unit power
    // simultaneously, so Dual Limit row-normalizes and only attenuates columns
    // whose summed coefficient power would otherwise exceed one.
    upscale->clearManualRoutes();
    upscale->setManualWeight(0u, 0u, 1.0f);
    upscale->setManualWeight(1u, 0u, 0.5f);
    upscale->setManualWeight(2u, 0u, 1.0f);
    ok &= check(near(columnPower(*upscale, 0u), 3.0f),
        "row normalization did not expose the converging-column load");
    upscale->setNormalization(s3g::FormatUpscaleNormalization::Column);
    ok &= check(near(columnPower(*upscale, 0u), 1.0f)
            && near(upscale->targetExtensionGain(0u, 0u), 2.0f / 3.0f)
            && near(upscale->targetExtensionGain(1u, 0u), 1.0f / 3.0f)
            && near(upscale->targetExtensionGain(2u, 0u), 2.0f / 3.0f),
        "column normalization did not preserve relative input weights");
    upscale->setNormalization(s3g::FormatUpscaleNormalization::DualLimit);
    ok &= check(near(columnPower(*upscale, 0u), 1.0f)
            && near(upscale->targetExtensionGain(0u, 0u), invSqrt3)
            && near(upscale->targetExtensionGain(1u, 0u), invSqrt3)
            && near(upscale->targetExtensionGain(2u, 0u), invSqrt3)
            && routePower(*upscale, 0u) < 1.0f,
        "dual-safe normalization did not limit an overloaded column");
    upscale->setNormalization(s3g::FormatUpscaleNormalization::Row);
    upscale->useAutomaticRoutes();

    s3g::FormatUpscaleLayoutData midSideOutput {};
    midSideOutput.count = 5u;
    s3g::formatUpscaleSetSpeaker(midSideOutput, 0u, 0.0f, 0.0f,
        s3g::FormatUpscaleRole::Center);
    s3g::formatUpscaleSetSpeaker(midSideOutput, 1u, 90.0f, 0.0f,
        s3g::FormatUpscaleRole::LeftSurround);
    s3g::formatUpscaleSetSpeaker(midSideOutput, 2u, -90.0f, 0.0f,
        s3g::FormatUpscaleRole::RightSurround);
    s3g::formatUpscaleSetSpeaker(midSideOutput, 3u, 90.0f, 60.0f,
        s3g::FormatUpscaleRole::TopLeftFront);
    s3g::formatUpscaleSetSpeaker(midSideOutput, 4u, -90.0f, 60.0f,
        s3g::FormatUpscaleRole::TopRightFront);
    upscale->setCustomOutputLayout(midSideOutput);
    params.inputLayout = s3g::FormatUpscaleLayout::Stereo;
    params.outputLayout = s3g::FormatUpscaleLayout::Custom;
    params.basis = s3g::FormatUpscaleBasis::Direct;
    params.placement = s3g::FormatUpscalePlacement::MidSideSpread;
    params.smoothingMs = 1.0f;
    params.decorrelationPercent = 0.0f;
    params.delayMs = 0.0f;
    upscale->setParams(params);
    upscale->reset();
    const float msCenterRight = upscale->targetExtensionGain(0u, 0u);
    const float msCenterLeft = upscale->targetExtensionGain(1u, 0u);
    const float msLeftFromRight = upscale->targetExtensionGain(0u, 1u);
    const float msLeftFromLeft = upscale->targetExtensionGain(1u, 1u);
    const float msRightFromRight = upscale->targetExtensionGain(0u, 2u);
    const float msRightFromLeft = upscale->targetExtensionGain(1u, 2u);
    const float msHeightFromRight = upscale->targetExtensionGain(0u, 3u);
    const float msHeightFromLeft = upscale->targetExtensionGain(1u, 3u);
    ok &= check(std::strcmp(s3g::formatUpscalePlacementName(
                    s3g::FormatUpscalePlacement::MidSideSpread),
                    "M/S spread") == 0
            && near(msCenterRight, msCenterLeft)
            && msLeftFromLeft > 0.0f && msLeftFromRight < 0.0f
            && msRightFromRight > 0.0f && msRightFromLeft < 0.0f,
        "M/S spread did not create a centered Mid and opposing lateral Side map");
    ok &= check((msLeftFromLeft - msLeftFromRight)
                > (msHeightFromLeft - msHeightFromRight)
            && near(routePower(*upscale, 0u), 1.0f)
            && near(routePower(*upscale, 1u), 1.0f),
        "M/S spread did not reduce Side with elevation or normalize its rows");
    input.fill(0.0f);
    input[0u] = 1.0f;
    input[1u] = 1.0f;
    upscale->processFrame(input.data(), 2u, output.data(), 5u);
    ok &= check(output[0u] > 0.0f
            && near(output[1u], output[2u])
            && near(output[3u], output[4u]),
        "M/S spread did not collapse a mono-compatible input symmetrically");
    upscale->beginManualRoutesFromCurrent();
    ok &= check(upscale->manualWeight(0u, 1u) < 0.0f
            && upscale->targetExtensionGain(0u, 1u) < 0.0f,
        "capturing the M/S matrix did not preserve negative Side polarity");
    upscale->setManualWeight(0u, 1u, -0.5f);
    ok &= check(upscale->manualRoute(0u, 1u)
            && near(upscale->manualWeight(0u, 1u), -0.5f)
            && upscale->targetExtensionGain(0u, 1u) < 0.0f,
        "editing an M/S matrix weight did not retain its signed polarity");
    upscale->useAutomaticRoutes();
    auto asymmetricMidSideOutput = midSideOutput;
    asymmetricMidSideOutput.count = 6u;
    s3g::formatUpscaleSetSpeaker(asymmetricMidSideOutput, 5u,
        45.0f, 20.0f, s3g::FormatUpscaleRole::Left);
    upscale->setCustomOutputLayout(asymmetricMidSideOutput);
    ok &= check(near(upscale->targetExtensionGain(0u, 0u),
                upscale->targetExtensionGain(1u, 0u))
            && near(routePower(*upscale, 0u), 1.0f)
            && near(routePower(*upscale, 1u), 1.0f),
        "M/S spread did not retain a common pair scale on an asymmetric array");

    params.inputLayout = s3g::FormatUpscaleLayout::Stereo;
    params.outputLayout = s3g::FormatUpscaleLayout::Quad;
    params.basis = s3g::FormatUpscaleBasis::Side;
    params.placement = s3g::FormatUpscalePlacement::SameSide;
    params.origin = s3g::FormatUpscaleOrigin::Keep;
    params.decorrelationPercent = 0.0f;
    params.delayMs = 0.0f;
    upscale->setParams(params);
    upscale->reset();
    ok &= check(upscale->targetExtensionGain(0u, 2u) > 0.0f
            && upscale->targetExtensionGain(1u, 2u) < 0.0f
            && upscale->targetExtensionGain(1u, 3u) > 0.0f
            && upscale->targetExtensionGain(0u, 3u) < 0.0f,
        "side basis did not form opposing difference routes");
    input.fill(0.0f);
    input[0] = 1.0f;
    input[1] = 1.0f;
    upscale->processFrame(input.data(), 2u, output.data(), 64u);
    ok &= check(std::abs(output[2]) < 0.0002f
            && std::abs(output[3]) < 0.0002f,
        "mono-compatible stereo leaked through the side extension");

    params.basis = s3g::FormatUpscaleBasis::Direct;
    params.decorrelationPercent = 35.0f;
    params.delayMs = 8.0f;
    upscale->setParams(params);
    upscale->reset();
    double energy = 0.0;
    for (uint32_t frame = 0u; frame < 2048u; ++frame) {
        input[0] = frame == 0u ? 1.0f : 0.0f;
        input[1] = 0.0f;
        upscale->processFrame(input.data(), 2u, output.data(), 64u);
        for (uint32_t channel = 0u; channel < 4u; ++channel) {
            ok &= check(std::isfinite(output[channel]),
                "delay/decorrelation produced a non-finite sample");
            energy += static_cast<double>(output[channel]) * output[channel];
        }
    }
    ok &= check(energy > 0.1, "delay/decorrelation path lost the signal");

    if (!ok) return 1;
    std::cout << "format upscale smoke passed\n";
    return 0;
}
