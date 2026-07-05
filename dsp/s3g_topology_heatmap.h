#pragma once

#include "s3g_topology.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace s3g {

inline double fillTopologyHeatmap(const TopologyState& state, uint32_t activeCount, uint32_t cols, uint32_t rows, double* heatOut)
{
    if (!heatOut || cols == 0 || rows == 0) {
        return 0.001;
    }

    activeCount = std::max<uint32_t>(1, activeCount);
    double heatMax = 0.001;
    for (uint32_t row = 0; row < rows; ++row) {
        for (uint32_t col = 0; col < cols; ++col) {
            const double u = (static_cast<double>(col) + 0.5) / static_cast<double>(cols) * 2.0 - 1.0;
            const double v = (static_cast<double>(row) + 0.5) / static_cast<double>(rows) * 2.0 - 1.0;
            double energy = 0.0;
            for (uint32_t lane = 0; lane < activeCount; ++lane) {
                const auto topo = topologyPointForLane(lane, activeCount, state);
                const double r = std::max(0.001, std::sqrt(topo.x * topo.x + topo.y * topo.y + topo.z * topo.z));
                const double pointU = std::atan2(topo.x, topo.z) / M_PI;
                const double pointV = -std::clamp(topo.y / r, -1.0, 1.0);
                double du = std::fabs(u - pointU);
                du = std::min(du, 2.0 - du);
                const double dv = v - pointV;
                const double radiusDelta = std::clamp(r - 1.0, -0.85, 1.35);
                const double radiusSize = 1.0 + radiusDelta * 0.38 + std::fabs(state.flare) * 0.18;
                const double radiusWeight = 0.84 + std::max(0.0, radiusDelta) * 0.86 + std::fabs(radiusDelta) * 0.20;
                const double seedSize = 1.0 + state.jitter * std::fabs(laneNoise(lane + 191u)) * 0.46;
                const double sx = (0.096 + state.amount * 0.035) * radiusSize * seedSize;
                const double sy = (0.145 + state.amount * 0.045) * radiusSize * seedSize;
                energy += radiusWeight * std::exp(-((du * du) / (2.0 * sx * sx) + (dv * dv) / (2.0 * sy * sy)));
            }
            const double cellNoise = laneNoise(row * 67u + col * 19u + 503u) * state.jitter * state.amount;
            energy *= std::clamp(1.0 + cellNoise * 0.34, 0.62, 1.38);
            const size_t index = static_cast<size_t>(row) * cols + col;
            heatOut[index] = energy;
            heatMax = std::max(heatMax, energy);
        }
    }
    return heatMax;
}

} // namespace s3g
