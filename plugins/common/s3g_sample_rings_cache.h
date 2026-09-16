#pragma once
#include "s3g_sample_asset.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace s3g::portable_gui {
struct RingsAssetCache {
    std::shared_ptr<const sample::SampleAsset> owner;
    bool valid = false;
};

// Cache device-independent vector geometry, not pixels: the host's normal
// transform and antialiasing still apply at every supported editor size/DPI.
struct RingsPathCache {
    const sample::SampleAsset* asset = nullptr;
    uint32_t channel = 0;
    double radius = 0, pitch = 0, nudge = 0;
    VSTGUI::CPoint center;
    VSTGUI::SharedPointer<VSTGUI::CGraphicsPath> peakPath, rmsPath, contour;

    bool draw(VSTGUI::CDrawContext& context, const sample::SampleAsset& source,
        uint32_t sourceChannel, double nextRadius, double nextPitch,
        double nextNudge, const VSTGUI::CPoint& nextCenter, uint32_t ink)
    {
        using namespace VSTGUI;
        constexpr uint32_t segments = 320u;
        constexpr double twoPi = 6.28318530717958647692;
        if (asset != &source || channel != sourceChannel || radius != nextRadius
            || pitch != nextPitch || nudge != nextNudge || center != nextCenter
            || !peakPath || !rmsPath || !contour) {
            if (sourceChannel >= source.channels.size() || source.channels[sourceChannel].empty()) return false;
            const auto& samples = source.channels[sourceChannel];
            std::array<float, segments + 1u> peaks {}, rms {}, means {};
            float channelPeak = 1.0e-8f;
            const std::size_t window = std::max<std::size_t>(1u, samples.size() / segments);
            for (uint32_t point = 0u; point < segments; ++point) {
                double phase = static_cast<double>(point) / segments + nextNudge;
                phase -= std::floor(phase);
                const std::size_t start = std::min(samples.size() - 1u,
                    static_cast<std::size_t>(phase * samples.size()));
                float peak = 0.0f; double mean = 0.0, squareSum = 0.0;
                for (uint32_t tap = 0u; tap < 12u; ++tap) {
                    const std::size_t offset = window * tap / 12u;
                    const float value = samples[(start + offset) % samples.size()];
                    peak = std::max(peak, std::abs(value)); mean += value;
                    squareSum += static_cast<double>(value) * value;
                }
                rms[point] = static_cast<float>(std::sqrt(squareSum / 12.0));
                peaks[point] = peak; means[point] = static_cast<float>(mean / 12.0);
                channelPeak = std::max(channelPeak, peak);
            }
            peaks[segments] = peaks[0u]; rms[segments] = rms[0u]; means[segments] = means[0u];
            const double amplitude = std::clamp(nextPitch * 0.38, 1.6, 4.6);
            const auto polar = [&](double r, double phase) {
                const double angle = phase * twoPi;
                return CPoint(nextCenter.x + std::sin(angle) * r,
                    nextCenter.y - std::cos(angle) * r);
            };
            auto nextPeak = owned(context.createGraphicsPath());
            auto nextRms = owned(context.createGraphicsPath());
            auto nextContour = owned(context.createGraphicsPath());
            if (!nextPeak || !nextRms || !nextContour) return false;
            for (const bool usePeak : {true, false}) {
                const auto& levels = usePeak ? peaks : rms;
                auto& path = usePeak ? nextPeak : nextRms;
                CPoint point = polar(nextRadius + amplitude * levels[0u] / channelPeak, 0.0);
                path->beginSubpath(point.x, point.y);
                for (uint32_t index = 1u; index <= segments; ++index) {
                    point = polar(nextRadius + amplitude * levels[index] / channelPeak, static_cast<double>(index) / segments);
                    path->addLine(point.x, point.y);
                }
                for (uint32_t index = segments + 1u; index-- > 0u;) {
                    point = polar(nextRadius - amplitude * levels[index] / channelPeak, static_cast<double>(index) / segments);
                    path->addLine(point.x, point.y);
                }
                path->closeSubpath();
            }
            CPoint point = polar(nextRadius + amplitude * 0.70 * means[0u] / channelPeak, 0.0);
            nextContour->beginSubpath(point.x, point.y);
            for (uint32_t index = 1u; index <= segments; ++index) {
                point = polar(nextRadius + amplitude * 0.70 * means[index] / channelPeak, static_cast<double>(index) / segments);
                nextContour->addLine(point.x, point.y);
            }
            peakPath = std::move(nextPeak); rmsPath = std::move(nextRms); contour = std::move(nextContour);
            asset = &source; channel = sourceChannel; radius = nextRadius;
            pitch = nextPitch; nudge = nextNudge; center = nextCenter;
        }
        const auto color = [ink](uint8_t alpha) {
            return CColor(uint8_t(ink >> 16u), uint8_t(ink >> 8u), uint8_t(ink), alpha);
        };
        context.setFillColor(color(26)); context.drawGraphicsPath(peakPath, CDrawContext::kPathFilled);
        context.setFillColor(color(61)); context.drawGraphicsPath(rmsPath, CDrawContext::kPathFilled);
        context.setFrameColor(color(184)); context.setLineWidth(0.72);
        context.drawGraphicsPath(contour, CDrawContext::kPathStroked);
        return true;
    }
};
} // namespace s3g::portable_gui
