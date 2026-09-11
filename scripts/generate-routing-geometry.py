#!/usr/bin/env python3
"""Extract the active Cocoa panner mesh equations, emitting an apply_patch patch.

No layout inference: the retained Cocoa implementation remains the reference.
Disabled #if 0 meshes are deliberately not migrated. The only substitutions
are the native point/project/path operations at the drawing boundary.
"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
source = (root / "plugins/clap_layout_panner/s3g_layout_panner_clap.cpp").read_text()
start = source.index("    auto drawPolyhedronShell =")
end = source.index("#if 0", start)
body = source[start:end]
body = body.replace("NSPoint", "VSTGUI::CPoint")
body = re.sub(r"\[self projectWorldPoint:(.*?) rect:rect depth:nil\]", r"project(\1)", body)
body = body.replace("[links moveToPoint:pts[a]];\n                    [links lineToPoint:pts[b]];", "segment(pts[a], pts[b]);")
assert "[links " not in body and "[self " not in body
content = """#pragma once
#include "vstgui/lib/cpoint.h"
// Generated from the ACTIVE Cocoa drawField mesh; see generate-routing-geometry.py.
namespace s3g::portable_gui::routing {
template<class Speakers, class Project, class Edge, class Segment>
void pannerMesh(const s3g::LayoutPannerParams& params, const Speakers& speakers,
                uint32_t n, Project project, Edge edge, Segment segment) {
""" + body + "}\n} // namespace s3g::portable_gui::routing\n"
node_source = (root / "plugins/clap_node_track_mixer/s3g_node_track_mixer_clap.cpp").read_text()
start = node_source.index("        if (n.sourceLayout == s3g::NodeTrackLayout::Cube && count >= 8u) {")
end = node_source.index("        for (uint32_t ch = 0; ch < count; ++ch) {", start)
node_content = """#pragma once
// Literal face/edge indices from the retained Cocoa Node Bus editor.
namespace s3g::portable_gui::routing {
template<class Node,class Edge,class Tri,class Quad>
void nodeMesh(const Node& n,unsigned count,Edge strokeEdge,Tri fillTri,Quad fillQuad) {
""" + node_source[start:end] + "}\n}\n"
outputs = {
    root / "plugins/common/s3g_panner_mesh.h": content,
    root / "plugins/common/s3g_node_mesh.h": node_content,
}
if "--check" in sys.argv:
    for path, expected in outputs.items():
        if path.read_text() != expected:
            sys.exit(f"Cocoa geometry has diverged: {path}")
    print("Panner and Node Bus geometry match the active Cocoa sources.")
else:
    print("*** Begin Patch")
    for path, expected in outputs.items():
        print("*** Add File: " + str(path))
        print("\n".join("+" + line for line in expected.splitlines()))
    print("*** End Patch")
