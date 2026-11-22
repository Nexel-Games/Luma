#include "Luma/GraphEditor/GraphMath.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace Luma::GraphEditor {

namespace {
constexpr int kLevelCount = static_cast<int>(std::size(ViewTransform::kZoomLevels));
}

void ViewTransform::SetLevel(int level) { m_level = std::clamp(level, 0, kLevelCount - 1); }

Vec2 ViewTransform::ToScreen(Vec2 g, Vec2 origin) const {
    const f32 z = Zoom();
    return {origin.x + (g.x - offset.x) * z, origin.y + (g.y - offset.y) * z};
}

Vec2 ViewTransform::ToGraph(Vec2 s, Vec2 origin) const {
    const f32 z = Zoom();
    return {offset.x + (s.x - origin.x) / z, offset.y + (s.y - origin.y) / z};
}

Rect ViewTransform::RectToScreen(const Rect& g, Vec2 origin) const {
    const Vec2 p = ToScreen({g.x, g.y}, origin);
    return {p.x, p.y, g.w * Zoom(), g.h * Zoom()};
}

void ViewTransform::Pan(Vec2 d) {
    offset.x -= d.x / Zoom();
    offset.y -= d.y / Zoom();
}

void ViewTransform::ZoomAt(int steps, Vec2 anchor, Vec2 origin) {
    const Vec2 fixed = ToGraph(anchor, origin);
    SetLevel(m_level + steps);
    // offset such that ToScreen(fixed) == anchor.
    offset.x = fixed.x - (anchor.x - origin.x) / Zoom();
    offset.y = fixed.y - (anchor.y - origin.y) / Zoom();
}

void ViewTransform::Frame(const Rect& g, Vec2 canvas) {
    int level = kDefaultLevel;
    while (level > 0 && (g.w * kZoomLevels[level] > canvas.x * 0.9f || g.h * kZoomLevels[level] > canvas.y * 0.9f))
        --level;
    SetLevel(level);
    const f32 z = Zoom();
    offset.x = g.x + g.w * 0.5f - canvas.x * 0.5f / z;
    offset.y = g.y + g.h * 0.5f - canvas.y * 0.5f / z;
}

NodeGeometry LayoutNode(const NodeView& n, const NodeMetrics& m) {
    NodeGeometry g;
    if (n.kind == NodeKind::Comment) {
        g.body = {n.position.x, n.position.y, std::max(n.size.x, 60.0f), std::max(n.size.y, 40.0f)};
        return g;
    }
    if (n.kind == NodeKind::Reroute) {
        const f32 s = m.rerouteSize;
        g.body = {n.position.x, n.position.y, s, s};
        const f32 cy = n.position.y + s * 0.5f;
        for (usize i = 0; i < n.inputs.size(); ++i) g.inputs.push_back({n.position.x, cy});
        for (usize i = 0; i < n.outputs.size(); ++i) g.outputs.push_back({n.position.x + s, cy});
        return g;
    }
    auto len = [](const std::vector<PinView>& pins) {
        usize l = 0;
        for (const PinView& p : pins) l = std::max(l, p.name.size());
        return static_cast<f32>(l);
    };
    const f32 titleW = static_cast<f32>(std::max(n.title.size(), n.subtitle.size())) * m.charWidth + 24.0f;
    const f32 pinsW = (len(n.inputs) + len(n.outputs)) * m.charWidth + m.pinColumnGap + 24.0f;
    const f32 w = std::max({m.minWidth, titleW, pinsW});
    const usize rows = std::max(n.inputs.size(), n.outputs.size());
    const f32 h = m.headerH + static_cast<f32>(rows) * m.pinRowH + m.padBottom;
    g.body = {n.position.x, n.position.y, w, h};
    for (usize i = 0; i < n.inputs.size(); ++i)
        g.inputs.push_back({n.position.x, n.position.y + m.headerH + (static_cast<f32>(i) + 0.5f) * m.pinRowH});
    for (usize i = 0; i < n.outputs.size(); ++i)
        g.outputs.push_back({n.position.x + w, n.position.y + m.headerH + (static_cast<f32>(i) + 0.5f) * m.pinRowH});
    return g;
}

std::optional<PinKey> HitPin(const NodeView& n, const NodeGeometry& g, Vec2 p, const NodeMetrics& m) {
    std::optional<PinKey> best;
    f32 bestD = m.pinHitRadius * m.pinHitRadius;
    auto test = [&](const std::vector<Vec2>& anchors, PinDir dir) {
        for (usize i = 0; i < anchors.size(); ++i) {
            const f32 dx = anchors[i].x - p.x, dy = anchors[i].y - p.y;
            const f32 d = dx * dx + dy * dy;
            if (d <= bestD) {
                bestD = d;
                best = PinKey{n.id, dir, static_cast<u32>(i)};
            }
        }
    };
    test(g.inputs, PinDir::Input);
    test(g.outputs, PinDir::Output);
    return best;
}

bool HitNode(const NodeView& n, const NodeGeometry& g, Vec2 p, const NodeMetrics& m) {
    if (n.kind == NodeKind::Comment) return Rect{g.body.x, g.body.y, g.body.w, m.headerH}.Contains(p);
    return g.body.Contains(p);
}

bool Overlaps(const Rect& a, const Rect& b) {
    return a.x < b.Right() && b.x < a.Right() && a.y < b.Bottom() && b.y < a.Bottom();
}

bool ContainsRect(const Rect& o, const Rect& i) {
    return i.x >= o.x && i.y >= o.y && i.Right() <= o.Right() && i.Bottom() <= o.Bottom();
}

Rect RectFromCorners(Vec2 a, Vec2 b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::fabs(a.x - b.x), std::fabs(a.y - b.y)};
}

Vec2 Wire::At(f32 t) const {
    const f32 u = 1.0f - t;
    const f32 a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
    return {a * p0.x + b * c0.x + c * c1.x + d * p1.x, a * p0.y + b * c0.y + c * c1.y + d * p1.y};
}

Wire MakeWire(Vec2 from, Vec2 to) {
    // Tangent from the clamped horizontal and vertical distances; backward
    // wires (input left of output) loop out further so they stay readable.
    const f32 dx = to.x - from.x, dy = to.y - from.y;
    const bool forward = dx >= 0.0f;
    const f32 tx = std::min(std::fabs(dx), forward ? 1000.0f : 400.0f);
    const f32 ty = std::min(std::fabs(dy), forward ? 400.0f : 600.0f);
    const f32 t = std::max(forward ? tx * 0.5f + ty * 0.1f : 60.0f + tx * 0.6f + ty * 0.3f, 20.0f);
    return {from, {from.x + t, from.y}, {to.x - t, to.y}, to};
}

f32 DistanceToWire(const Wire& w, Vec2 p, int segments) {
    f32 best = 1e30f;
    Vec2 a = w.At(0.0f);
    for (int i = 1; i <= segments; ++i) {
        const Vec2 b = w.At(static_cast<f32>(i) / static_cast<f32>(segments));
        const f32 vx = b.x - a.x, vy = b.y - a.y;
        const f32 len2 = vx * vx + vy * vy;
        f32 t = len2 > 0.0f ? ((p.x - a.x) * vx + (p.y - a.y) * vy) / len2 : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        const f32 cx = a.x + vx * t - p.x, cy = a.y + vy * t - p.y;
        best = std::min(best, cx * cx + cy * cy);
        a = b;
    }
    return std::sqrt(best);
}

}  // namespace Luma::GraphEditor
