#include "Luma/RHI/DebugDraw.h"

#include <algorithm>
#include <cmath>

namespace Luma {

namespace {
Math::Vec3 TransformPoint(const Math::Mat4& m, const Math::Vec3& p) {
    return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
            m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
            m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]};
}
Math::Vec3 Add(const Math::Vec3& a, const Math::Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Math::Vec3 Scale(const Math::Vec3& a, f32 s) { return {a.x * s, a.y * s, a.z * s}; }
// Two unit vectors perpendicular to `axis` (and each other).
void Basis(const Math::Vec3& axis, Math::Vec3& u, Math::Vec3& v) {
    const Math::Vec3 ref = std::fabs(axis.y) < 0.9f ? Math::Vec3{0, 1, 0} : Math::Vec3{1, 0, 0};
    u = Math::Normalize(Math::Cross(axis, ref));
    v = Math::Cross(axis, u);
}
}  // namespace

void DebugDraw::Line(const Math::Vec3& a, const Math::Vec3& b, const Math::Vec3& color, Mode mode) {
    auto& out = Target(mode);
    out.push_back({a, color});
    out.push_back({b, color});
}

void DebugDraw::Box(const Math::Vec3& center, const Math::Vec3& h, const Math::Vec3& color, Mode mode) {
    OrientedBox(Math::Translate(center), h, color, mode);
}

void DebugDraw::OrientedBox(const Math::Mat4& transform, const Math::Vec3& h, const Math::Vec3& color, Mode mode) {
    Math::Vec3 c[8];
    for (int i = 0; i < 8; ++i) {
        const Math::Vec3 local{(i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z};
        c[i] = TransformPoint(transform, local);
    }
    // Corners differing in exactly one bit share an edge.
    for (int i = 0; i < 8; ++i)
        for (int bit = 1; bit < 8; bit <<= 1)
            if (!(i & bit)) Line(c[i], c[i | bit], color, mode);
}

void DebugDraw::Sphere(const Math::Vec3& center, f32 radius, const Math::Vec3& color, Mode mode, u32 segments) {
    segments = std::max(segments, 4u);
    const Math::Vec3 axes[3][2] = {{{1, 0, 0}, {0, 1, 0}}, {{1, 0, 0}, {0, 0, 1}}, {{0, 1, 0}, {0, 0, 1}}};
    for (const auto& ax : axes) {
        for (u32 i = 0; i < segments; ++i) {
            const f32 a0 = Math::kPi * 2.0f * static_cast<f32>(i) / static_cast<f32>(segments);
            const f32 a1 = Math::kPi * 2.0f * static_cast<f32>(i + 1) / static_cast<f32>(segments);
            const Math::Vec3 p0 = Add(center, Add(Scale(ax[0], std::cos(a0) * radius), Scale(ax[1], std::sin(a0) * radius)));
            const Math::Vec3 p1 = Add(center, Add(Scale(ax[0], std::cos(a1) * radius), Scale(ax[1], std::sin(a1) * radius)));
            Line(p0, p1, color, mode);
        }
    }
}

void DebugDraw::Capsule(const Math::Vec3& a, const Math::Vec3& b, f32 radius, const Math::Vec3& color, Mode mode,
                        u32 segments) {
    segments = std::max(segments, 4u);
    Math::Vec3 axis = b - a;
    const f32 len = std::sqrt(Math::Dot(axis, axis));
    axis = len > 1e-6f ? Scale(axis, 1.0f / len) : Math::Vec3{0, 1, 0};
    Math::Vec3 u, v;
    Basis(axis, u, v);
    auto ring = [&](const Math::Vec3& c) {
        for (u32 i = 0; i < segments; ++i) {
            const f32 a0 = Math::kPi * 2.0f * static_cast<f32>(i) / static_cast<f32>(segments);
            const f32 a1 = Math::kPi * 2.0f * static_cast<f32>(i + 1) / static_cast<f32>(segments);
            Line(Add(c, Add(Scale(u, std::cos(a0) * radius), Scale(v, std::sin(a0) * radius))),
                 Add(c, Add(Scale(u, std::cos(a1) * radius), Scale(v, std::sin(a1) * radius))), color, mode);
        }
    };
    ring(a);
    ring(b);
    // Side lines.
    for (const Math::Vec3& dir : {u, v, Scale(u, -1.0f), Scale(v, -1.0f)})
        Line(Add(a, Scale(dir, radius)), Add(b, Scale(dir, radius)), color, mode);
    // Hemisphere caps: half circles in the two planes through the axis.
    const u32 half = std::max(segments / 2, 2u);
    auto cap = [&](const Math::Vec3& c, f32 sign) {
        for (const Math::Vec3& side : {u, v}) {
            for (u32 i = 0; i < half; ++i) {
                const f32 a0 = Math::kPi * static_cast<f32>(i) / static_cast<f32>(half);
                const f32 a1 = Math::kPi * static_cast<f32>(i + 1) / static_cast<f32>(half);
                Line(Add(c, Add(Scale(side, std::cos(a0) * radius), Scale(axis, sign * std::sin(a0) * radius))),
                     Add(c, Add(Scale(side, std::cos(a1) * radius), Scale(axis, sign * std::sin(a1) * radius))),
                     color, mode);
            }
        }
    };
    cap(b, 1.0f);
    cap(a, -1.0f);
}

DebugDraw& DebugDrawer() {
    static DebugDraw drawer;
    return drawer;
}

}  // namespace Luma
