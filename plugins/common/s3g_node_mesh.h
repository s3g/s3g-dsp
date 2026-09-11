#pragma once
// Literal face/edge indices from the retained Cocoa Node Bus editor.
namespace s3g::portable_gui::routing {
template<class Node,class Edge,class Tri,class Quad>
void nodeMesh(const Node& n,unsigned count,Edge strokeEdge,Tri fillTri,Quad fillQuad) {
        if (n.sourceLayout == s3g::NodeTrackLayout::Cube && count >= 8u) {
            fillQuad(0, 1, 3, 2);
            fillQuad(4, 5, 7, 6);
            static constexpr uint32_t edges[][2] {
                {0, 1}, {1, 3}, {3, 2}, {2, 0},
                {4, 5}, {5, 7}, {7, 6}, {6, 4},
                {0, 4}, {1, 5}, {2, 6}, {3, 7},
            };
            for (const auto& edge : edges) strokeEdge(edge[0], edge[1]);
        } else if (n.sourceLayout == s3g::NodeTrackLayout::Stereo && count >= 2u) {
            strokeEdge(0, 1);
        } else if (n.sourceLayout == s3g::NodeTrackLayout::Quad && count >= 4u) {
            fillQuad(0, 1, 2, 3);
            static constexpr uint32_t edges[][2] { {0, 1}, {1, 2}, {2, 3}, {3, 0} };
            for (const auto& edge : edges) strokeEdge(edge[0], edge[1]);
        } else if (n.sourceLayout == s3g::NodeTrackLayout::FiveZero && count >= 5u) {
            fillTri(0, 2, 1);
            fillQuad(0, 3, 4, 1);
            static constexpr uint32_t edges[][2] { {0, 2}, {2, 1}, {1, 4}, {4, 3}, {3, 0}, {0, 1}, {3, 4} };
            for (const auto& edge : edges) strokeEdge(edge[0], edge[1]);
        } else if (n.sourceLayout == s3g::NodeTrackLayout::SixZero && count >= 6u) {
            fillQuad(0, 1, 2, 5);
            fillQuad(5, 2, 3, 4);
            static constexpr uint32_t edges[][2] { {0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 0}, {2, 5} };
            for (const auto& edge : edges) strokeEdge(edge[0], edge[1]);
        } else if (n.sourceLayout == s3g::NodeTrackLayout::SevenZero && count >= 7u) {
            fillTri(0, 2, 1);
            fillQuad(0, 3, 5, 6);
            fillQuad(1, 4, 6, 5);
            static constexpr uint32_t edges[][2] { {0, 2}, {2, 1}, {0, 3}, {3, 5}, {5, 6}, {6, 4}, {4, 1}, {3, 4} };
            for (const auto& edge : edges) strokeEdge(edge[0], edge[1]);
        } else if (n.sourceLayout == s3g::NodeTrackLayout::FiveZeroTwo && count >= 7u) {
            fillTri(0, 2, 1);
            fillQuad(0, 3, 4, 1);
            static constexpr uint32_t edges[][2] { {0, 2}, {2, 1}, {1, 4}, {4, 3}, {3, 0}, {5, 6}, {0, 5}, {1, 6}, {2, 5}, {2, 6} };
            for (const auto& edge : edges) strokeEdge(edge[0], edge[1]);
        } else if (n.sourceLayout == s3g::NodeTrackLayout::SevenZeroTwo && count >= 9u) {
            fillTri(0, 2, 1);
            fillQuad(0, 3, 5, 6);
            fillQuad(1, 4, 6, 5);
            static constexpr uint32_t edges[][2] { {0, 2}, {2, 1}, {0, 3}, {3, 5}, {5, 6}, {6, 4}, {4, 1}, {7, 8}, {0, 7}, {1, 8}, {2, 7}, {2, 8} };
            for (const auto& edge : edges) strokeEdge(edge[0], edge[1]);
        } else if (n.sourceLayout == s3g::NodeTrackLayout::FiveZeroFour && count >= 9u) {
            fillTri(0, 2, 1);
            fillQuad(0, 3, 4, 1);
            fillQuad(5, 6, 7, 8);
            static constexpr uint32_t edges[][2] { {0, 2}, {2, 1}, {1, 4}, {4, 3}, {3, 0}, {5, 6}, {6, 7}, {7, 8}, {8, 5}, {0, 5}, {1, 6}, {4, 7}, {3, 8} };
            for (const auto& edge : edges) strokeEdge(edge[0], edge[1]);
        } else if (n.sourceLayout == s3g::NodeTrackLayout::SevenZeroFour && count >= 11u) {
            fillTri(0, 2, 1);
            fillQuad(0, 3, 5, 6);
            fillQuad(1, 4, 6, 5);
            fillQuad(7, 8, 9, 10);
            static constexpr uint32_t edges[][2] { {0, 2}, {2, 1}, {0, 3}, {3, 5}, {5, 6}, {6, 4}, {4, 1}, {7, 8}, {8, 9}, {9, 10}, {10, 7}, {0, 7}, {1, 8}, {6, 9}, {5, 10} };
            for (const auto& edge : edges) strokeEdge(edge[0], edge[1]);
        } else if ((n.sourceLayout == s3g::NodeTrackLayout::DoubleRing16 || n.sourceLayout == s3g::NodeTrackLayout::DoubleRing24) && count >= 4u) {
            const uint32_t half = std::max<uint32_t>(1u, count / 2u);
            for (uint32_t i = 0; i < half; ++i) strokeEdge(i, (i + 1u) % half);
            for (uint32_t i = half; i < count; ++i) strokeEdge(i, half + ((i + 1u - half) % std::max<uint32_t>(1u, count - half)));
            for (uint32_t i = 0; i < std::min<uint32_t>(half, count - half); ++i) strokeEdge(i, half + i);
        } else if (count > 1u) {
            for (uint32_t i = 0; i < count; ++i) strokeEdge(i, (i + 1u) % count);
        }
}
}
