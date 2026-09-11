#pragma once
#include "vstgui/lib/cpoint.h"
// Generated from the ACTIVE Cocoa drawField mesh; see generate-routing-geometry.py.
namespace s3g::portable_gui::routing {
template<class Speakers, class Project, class Edge, class Segment>
void pannerMesh(const s3g::LayoutPannerParams& params, const Speakers& speakers,
                uint32_t n, Project project, Edge edge, Segment segment) {
    auto drawPolyhedronShell = [&](bool dodecaShell) {
        constexpr float phi = 1.61803398875f;
        constexpr float invPhi = 1.0f / phi;
        std::array<s3g::Vec3, 20> verts {};
        const uint32_t count = dodecaShell ? 20u : 12u;
        if (dodecaShell) {
            const float pts[20][3] {
                { 1, 1, 1 }, { 1, 1, -1 }, { 1, -1, 1 }, { 1, -1, -1 },
                { -1, 1, 1 }, { -1, 1, -1 }, { -1, -1, 1 }, { -1, -1, -1 },
                { 0, invPhi, phi }, { 0, invPhi, -phi }, { 0, -invPhi, phi }, { 0, -invPhi, -phi },
                { invPhi, phi, 0 }, { invPhi, -phi, 0 }, { -invPhi, phi, 0 }, { -invPhi, -phi, 0 },
                { phi, 0, invPhi }, { phi, 0, -invPhi }, { -phi, 0, invPhi }, { -phi, 0, -invPhi },
            };
            for (uint32_t i = 0; i < count; ++i) verts[i] = { pts[i][0], pts[i][1], pts[i][2] };
        } else {
            const float pts[12][3] {
                { 0, 1, phi }, { 0, -1, phi }, { 0, 1, -phi }, { 0, -1, -phi },
                { 1, phi, 0 }, { -1, phi, 0 }, { 1, -phi, 0 }, { -1, -phi, 0 },
                { phi, 0, 1 }, { -phi, 0, 1 }, { phi, 0, -1 }, { -phi, 0, -1 },
            };
            for (uint32_t i = 0; i < count; ++i) verts[i] = { pts[i][0], pts[i][1], pts[i][2] };
        }
        std::array<VSTGUI::CPoint, 20> pts {};
        for (uint32_t i = 0; i < count; ++i) {
            const float d = std::sqrt(verts[i].x * verts[i].x + verts[i].y * verts[i].y + verts[i].z * verts[i].z);
            if (d > 0.000001f) {
                verts[i].x /= d;
                verts[i].y /= d;
                verts[i].z /= d;
            }
            pts[i] = project(verts[i]);
        }
        float minD2 = 999999.0f;
        for (uint32_t a = 0; a < count; ++a) {
            for (uint32_t b = a + 1u; b < count; ++b) {
                const float dx = verts[a].x - verts[b].x;
                const float dy = verts[a].y - verts[b].y;
                const float dz = verts[a].z - verts[b].z;
                const float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > 0.0001f) minD2 = std::min(minD2, d2);
            }
        }
        const float maxD2 = minD2 * 1.08f;
        for (uint32_t a = 0; a < count; ++a) {
            for (uint32_t b = a + 1u; b < count; ++b) {
                const float dx = verts[a].x - verts[b].x;
                const float dy = verts[a].y - verts[b].y;
                const float dz = verts[a].z - verts[b].z;
                const float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 <= maxD2) {
                    segment(pts[a], pts[b]);
                }
            }
        }
    };
    auto drawElevationPerimeters = [&]() {
        struct Band {
            float el = 0.0f;
            std::array<uint32_t, s3g::kLayoutPannerMaxSpeakers> ids {};
            uint32_t count = 0;
        };
        std::array<Band, 8> bands {};
        uint32_t bandCount = 0;
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t band = bandCount;
            for (uint32_t b = 0; b < bandCount; ++b) {
                if (std::fabs(bands[b].el - speakers[i].elevationDeg) < 8.0f) {
                    band = b;
                    break;
                }
            }
            if (band == bandCount && bandCount < bands.size()) bands[bandCount++].el = speakers[i].elevationDeg;
            if (band < bandCount) bands[band].ids[bands[band].count++] = i;
        }
        std::sort(bands.begin(), bands.begin() + bandCount, [](const Band& a, const Band& b) { return a.el < b.el; });
        for (uint32_t b = 0; b < bandCount; ++b) {
            std::sort(bands[b].ids.begin(), bands[b].ids.begin() + bands[b].count, [&](uint32_t a, uint32_t bIndex) {
                float aAz = s3g::layoutPannerWrapDeg(speakers[a].azimuthDeg);
                float bAz = s3g::layoutPannerWrapDeg(speakers[bIndex].azimuthDeg);
                if (aAz < 0.0f) aAz += 360.0f;
                if (bAz < 0.0f) bAz += 360.0f;
                return aAz < bAz;
            });
            if (bands[b].count < 2u) continue;
            for (uint32_t i = 0; i < bands[b].count; ++i) {
                edge(bands[b].ids[i], bands[b].ids[(i + 1u) % bands[b].count]);
            }
        }
    };
    if (params.layout == s3g::LayoutPannerPreset::Dodeca12) drawPolyhedronShell(true);
    else if (params.layout == s3g::LayoutPannerPreset::Icosahedron20) drawPolyhedronShell(false);
    else drawElevationPerimeters();
}
} // namespace s3g::portable_gui::routing
