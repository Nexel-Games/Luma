#pragma once

#include <optional>
#include <vector>

#include "Luma/GraphEditor/GraphModel.h"

// Pure maths of the graph editor: the canvas transform (pan + discrete zoom
// levels, as SNodePanel), node geometry and pin anchors, bezier wires
// (tangent grows with the horizontal/vertical distance, clamped, as UE's
// connection drawing policy) and hit tests. All positions without a
// "screen" suffix are in graph space.

namespace Luma::GraphEditor {

class ViewTransform {
public:
    static constexpr f32 kZoomLevels[] = {0.1f, 0.125f, 0.15f, 0.2f,  0.25f, 0.375f, 0.5f, 0.675f,
                                          0.75f, 0.875f, 1.0f,  1.25f, 1.5f,  1.75f,  2.0f};
    static constexpr int kDefaultLevel = 10;  // 1:1

    Vec2 offset{0, 0};  // graph point shown at the canvas origin

    f32 Zoom() const { return kZoomLevels[m_level]; }
    int Level() const { return m_level; }
    void SetLevel(int level);

    Vec2 ToScreen(Vec2 graph, Vec2 origin) const;
    Vec2 ToGraph(Vec2 screen, Vec2 origin) const;
    Rect RectToScreen(const Rect& graph, Vec2 origin) const;
    // Pans by a screen-space mouse delta.
    void Pan(Vec2 screenDelta);
    // Steps zoom levels keeping the graph point under `anchorScreen` fixed.
    void ZoomAt(int steps, Vec2 anchorScreen, Vec2 origin);
    // Centres `graph` in a canvas of `canvasSize`, at the largest level that fits (<= 1:1).
    void Frame(const Rect& graph, Vec2 canvasSize);

private:
    int m_level = kDefaultLevel;
};

struct NodeMetrics {
    f32 headerH = 26.0f;
    f32 pinRowH = 22.0f;
    f32 minWidth = 110.0f;
    f32 charWidth = 7.0f;   // layout estimate per character (font-independent)
    f32 pinColumnGap = 24.0f;
    f32 padBottom = 6.0f;
    f32 rerouteSize = 16.0f;
    f32 pinHitRadius = 9.0f;
};

struct NodeGeometry {
    Rect body;                     // graph space
    std::vector<Vec2> inputs;      // pin anchors (graph space)
    std::vector<Vec2> outputs;
};

NodeGeometry LayoutNode(const NodeView& node, const NodeMetrics& m = {});
// The pin of `node` within the hit radius of `point`, nearest first.
std::optional<PinKey> HitPin(const NodeView& node, const NodeGeometry& g, Vec2 point, const NodeMetrics& m = {});
// Comments are hit on their title bar only (their body lets clicks through).
bool HitNode(const NodeView& node, const NodeGeometry& g, Vec2 point, const NodeMetrics& m = {});
bool Overlaps(const Rect& a, const Rect& b);
bool ContainsRect(const Rect& outer, const Rect& inner);
// Normalised rect between two corners.
Rect RectFromCorners(Vec2 a, Vec2 b);

struct Wire {
    Vec2 p0, c0, c1, p1;
    Vec2 At(f32 t) const;
};
Wire MakeWire(Vec2 fromOutput, Vec2 toInput);
f32 DistanceToWire(const Wire& w, Vec2 p, int segments = 24);

}  // namespace Luma::GraphEditor
