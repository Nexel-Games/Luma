#include "Luma/Gizmo/TransformGizmo.h"

#include <algorithm>
#include <cmath>

namespace Luma {
namespace {

using Math::Mat4;
using Math::Vec3;

constexpr f32 kPi = 3.14159265358979f;

f32 Get(const Vec3& v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }
void Set(Vec3& v, int i, f32 value) { (i == 0 ? v.x : (i == 1 ? v.y : v.z)) = value; }
f32 Length(const Vec3& v) { return std::sqrt(Math::Dot(v, v)); }
// Element (row r, col c) of a column-major Mat4.
f32 M(const Mat4& m, int r, int c) { return m.m[c * 4 + r]; }
Vec3 Column(const Mat4& m, int c) { return {M(m, 0, c), M(m, 1, c), M(m, 2, c)}; }
Vec3 Rotate(const Mat4& m, const Vec3& v) {
    return {M(m, 0, 0) * v.x + M(m, 0, 1) * v.y + M(m, 0, 2) * v.z,
            M(m, 1, 0) * v.x + M(m, 1, 1) * v.y + M(m, 1, 2) * v.z,
            M(m, 2, 0) * v.x + M(m, 2, 1) * v.y + M(m, 2, 2) * v.z};
}
f32 SnapTo(f32 v, f32 step) { return step > 0.0f ? std::round(v / step) * step : v; }

// Projects a world point to viewport pixels. False when behind the camera.
bool WorldToScreen(const Vec3& p, const GizmoInput& in, f32& outX, f32& outY) {
    const Mat4 vp = in.proj * in.view;
    const f32 cx = vp.m[0] * p.x + vp.m[4] * p.y + vp.m[8] * p.z + vp.m[12];
    const f32 cy = vp.m[1] * p.x + vp.m[5] * p.y + vp.m[9] * p.z + vp.m[13];
    const f32 cw = vp.m[3] * p.x + vp.m[7] * p.y + vp.m[11] * p.z + vp.m[15];
    if (cw <= 0.0001f) return false;
    outX = in.viewportX + (cx / cw * 0.5f + 0.5f) * in.viewportW;
    outY = in.viewportY + (cy / cw * 0.5f + 0.5f) * in.viewportH;
    return true;
}

// World-space ray through the mouse (Vulkan clip depth 0..1).
void MouseRay(const GizmoInput& in, Vec3& origin, Vec3& dir) {
    const Mat4 inv = Math::Inverse(in.proj * in.view);
    const f32 nx = (in.mouseX - in.viewportX) / std::max(in.viewportW, 1.0f) * 2.0f - 1.0f;
    const f32 ny = (in.mouseY - in.viewportY) / std::max(in.viewportH, 1.0f) * 2.0f - 1.0f;
    auto unproject = [&](f32 z) {
        const f32 x = inv.m[0] * nx + inv.m[4] * ny + inv.m[8] * z + inv.m[12];
        const f32 y = inv.m[1] * nx + inv.m[5] * ny + inv.m[9] * z + inv.m[13];
        const f32 zz = inv.m[2] * nx + inv.m[6] * ny + inv.m[10] * z + inv.m[14];
        const f32 w = inv.m[3] * nx + inv.m[7] * ny + inv.m[11] * z + inv.m[15];
        return Vec3(x / w, y / w, zz / w);
    };
    origin = unproject(0.0f);
    dir = Math::Normalize(unproject(1.0f) - origin);
}

bool RayPlane(const Vec3& o, const Vec3& d, const Vec3& point, const Vec3& n, Vec3& hit) {
    const f32 denom = Math::Dot(d, n);
    if (std::fabs(denom) < 1e-5f) return false;
    const f32 t = Math::Dot(point - o, n) / denom;
    if (t < 0.0f) return false;
    hit = o + d * t;
    return true;
}

f32 DistToSegment(f32 px, f32 py, f32 ax, f32 ay, f32 bx, f32 by) {
    const f32 dx = bx - ax, dy = by - ay;
    const f32 len2 = dx * dx + dy * dy;
    f32 t = len2 > 0.0f ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const f32 ex = px - (ax + dx * t), ey = py - (ay + dy * t);
    return std::sqrt(ex * ex + ey * ey);
}

// Point-in-convex-polygon (screen space), either winding. Degenerate
// (edge-on) quads never contain the point.
bool InsideQuad(f32 px, f32 py, const f32 xs[4], const f32 ys[4]) {
    f32 area2 = 0.0f;
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        area2 += xs[i] * ys[j] - xs[j] * ys[i];
    }
    if (std::fabs(area2) < 2.0f * 40.0f) return false;  // < ~40 px^2: edge-on
    int sign = 0;
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        const f32 c = (xs[j] - xs[i]) * (py - ys[i]) - (ys[j] - ys[i]) * (px - xs[i]);
        const int s = c > 0.0f ? 1 : (c < 0.0f ? -1 : 0);
        if (s == 0) continue;
        if (sign == 0) sign = s;
        else if (s != sign) return false;
    }
    return sign != 0;
}

GizmoHandle AxisHandle(int i) {
    return i == 0 ? GizmoHandle::X : (i == 1 ? GizmoHandle::Y : GizmoHandle::Z);
}
int AxisIndex(GizmoHandle h) {
    switch (h) {
        case GizmoHandle::X: return 0;
        case GizmoHandle::Y: return 1;
        case GizmoHandle::Z: return 2;
        default: return -1;
    }
}
// The two axes spanning a plane handle.
bool PlaneAxes(GizmoHandle h, int& a, int& b) {
    switch (h) {
        case GizmoHandle::XY: a = 0; b = 1; return true;
        case GizmoHandle::XZ: a = 0; b = 2; return true;
        case GizmoHandle::YZ: a = 1; b = 2; return true;
        default: return false;
    }
}

constexpr GizmoHandle kPlanes[3] = {GizmoHandle::XY, GizmoHandle::XZ, GizmoHandle::YZ};
constexpr int kRingSegments = 64;
constexpr f32 kPlaneMin = 0.22f, kPlaneMax = 0.42f;  // plane handle extent (x axis length)

}  // namespace

// --- Math helpers -------------------------------------------------------------

Mat4 TransformGizmo::RotationFromEuler(const Vec3& degrees) {
    return Math::RotateY(Math::Radians(degrees.y)) * Math::RotateX(Math::Radians(degrees.x)) *
           Math::RotateZ(Math::Radians(degrees.z));
}

Vec3 TransformGizmo::EulerFromRotation(const Mat4& r) {
    // R = Ry(b) * Rx(a) * Rz(c):  r12 = -sin a, r02 = sin b cos a,
    // r22 = cos b cos a, r10 = cos a sin c, r11 = cos a cos c.
    const f32 sa = std::clamp(-M(r, 1, 2), -1.0f, 1.0f);
    const f32 a = std::asin(sa);
    f32 b, c;
    if (std::fabs(sa) < 0.9999f) {
        b = std::atan2(M(r, 0, 2), M(r, 2, 2));
        c = std::atan2(M(r, 1, 0), M(r, 1, 1));
    } else {  // gimbal lock: fold Z into Y
        c = 0.0f;
        b = std::atan2(-M(r, 2, 0), M(r, 0, 0));
    }
    constexpr f32 kToDeg = 180.0f / kPi;
    return {a * kToDeg, b * kToDeg, c * kToDeg};
}

Mat4 TransformGizmo::AxisAngle(const Vec3& axisIn, f32 radians) {
    const Vec3 a = Math::Normalize(axisIn);
    const f32 c = std::cos(radians), s = std::sin(radians), t = 1.0f - c;
    Mat4 r = Mat4::Identity();
    auto set = [&r](int row, int col, f32 v) { r.m[col * 4 + row] = v; };
    set(0, 0, t * a.x * a.x + c);
    set(0, 1, t * a.x * a.y - s * a.z);
    set(0, 2, t * a.x * a.z + s * a.y);
    set(1, 0, t * a.x * a.y + s * a.z);
    set(1, 1, t * a.y * a.y + c);
    set(1, 2, t * a.y * a.z - s * a.x);
    set(2, 0, t * a.x * a.z - s * a.y);
    set(2, 1, t * a.y * a.z + s * a.x);
    set(2, 2, t * a.z * a.z + c);
    return r;
}

// --- Handles --------------------------------------------------------------------

void TransformGizmo::Axes(const GizmoTransform& t, Vec3 out[3]) const {
    if (m_space == GizmoSpace::Local || m_mode == GizmoMode::Scale) {
        const Mat4 r = RotationFromEuler(t.rotationEuler);
        for (int i = 0; i < 3; ++i) out[i] = Math::Normalize(Column(r, i));
    } else {
        out[0] = {1, 0, 0};
        out[1] = {0, 1, 0};
        out[2] = {0, 0, 1};
    }
}

GizmoHandle TransformGizmo::HitTest(const GizmoTransform& t, const GizmoInput& in) const {
    const f32 L = in.scale > 0.0f ? in.scale : 1.0f;
    const Vec3& o = t.position;
    f32 ox, oy;
    if (!WorldToScreen(o, in, ox, oy)) return GizmoHandle::None;
    Vec3 ax[3];
    Axes(t, ax);
    const f32 mx = in.mouseX, my = in.mouseY;

    if (m_mode == GizmoMode::Rotate) {
        GizmoHandle best = GizmoHandle::None;
        f32 bestD = 8.0f;
        for (int i = 0; i < 3; ++i) {
            const Vec3& u = ax[(i + 1) % 3];
            const Vec3& v = ax[(i + 2) % 3];
            f32 px = 0, py = 0;
            bool havePrev = false;
            for (int s = 0; s <= kRingSegments; ++s) {
                const f32 ang = 2.0f * kPi * static_cast<f32>(s) / kRingSegments;
                const Vec3 p = o + (u * std::cos(ang) + v * std::sin(ang)) * L;
                f32 sx, sy;
                if (!WorldToScreen(p, in, sx, sy)) {
                    havePrev = false;
                    continue;
                }
                if (havePrev) {
                    const f32 d = DistToSegment(mx, my, px, py, sx, sy);
                    if (d < bestD) {
                        bestD = d;
                        best = AxisHandle(i);
                    }
                }
                px = sx;
                py = sy;
                havePrev = true;
            }
        }
        return best;
    }

    if (m_mode == GizmoMode::Scale) {
        if (std::hypot(mx - ox, my - oy) < 11.0f) return GizmoHandle::Uniform;
    } else {
        // Translate plane handles win inside their little squares.
        for (GizmoHandle h : kPlanes) {
            int a, b;
            PlaneAxes(h, a, b);
            const f32 s[4][2] = {{kPlaneMin, kPlaneMin}, {kPlaneMax, kPlaneMin},
                                 {kPlaneMax, kPlaneMax}, {kPlaneMin, kPlaneMax}};
            f32 xs[4], ys[4];
            bool ok = true;
            for (int k = 0; k < 4 && ok; ++k) {
                ok = WorldToScreen(o + ax[a] * (s[k][0] * L) + ax[b] * (s[k][1] * L), in,
                                   xs[k], ys[k]);
            }
            if (ok && InsideQuad(mx, my, xs, ys)) return h;
        }
    }

    GizmoHandle best = GizmoHandle::None;
    f32 bestD = 9.0f;
    const f32 start = m_mode == GizmoMode::Scale ? 0.18f : 0.15f;
    for (int i = 0; i < 3; ++i) {
        f32 ax0, ay0, ax1, ay1;
        if (!WorldToScreen(o + ax[i] * (start * L), in, ax0, ay0)) continue;
        if (!WorldToScreen(o + ax[i] * L, in, ax1, ay1)) continue;
        const f32 d = DistToSegment(mx, my, ax0, ay0, ax1, ay1);
        if (d < bestD) {
            bestD = d;
            best = AxisHandle(i);
        }
    }
    return best;
}

bool TransformGizmo::Update(const GizmoTransform& current, const GizmoInput& in,
                            GizmoTransform& out) {
    const f32 L = in.scale > 0.0f ? in.scale : 1.0f;
    Vec3 rayO, rayD;
    MouseRay(in, rayO, rayD);

    // ---- Begin a drag ----
    if (m_active == GizmoHandle::None) {
        m_hovered = HitTest(current, in);
        if (m_hovered == GizmoHandle::None || !in.leftPressed) return false;

        m_start = current;
        Axes(current, m_startAxes);
        m_startMouseX = in.mouseX;
        m_startMouseY = in.mouseY;
        m_useScreenRotate = false;
        const int axis = AxisIndex(m_hovered);
        int pa, pb;
        if (m_hovered == GizmoHandle::Uniform) {
            m_active = m_hovered;
            return false;
        }
        if (m_mode == GizmoMode::Rotate) {
            m_planeNormal = m_startAxes[axis];
            // Edge-on rings can't be intersected reliably: drag along the
            // ring's screen tangent instead.
            if (std::fabs(Math::Dot(rayD, m_planeNormal)) < 0.15f ||
                !RayPlane(rayO, rayD, current.position, m_planeNormal, m_startHit)) {
                m_useScreenRotate = true;
                f32 ox = 0, oy = 0;
                WorldToScreen(current.position, in, ox, oy);
                f32 tx = -(in.mouseY - oy), ty = in.mouseX - ox;
                const f32 len = std::max(std::hypot(tx, ty), 1e-3f);
                m_screenTangentX = tx / len;
                m_screenTangentY = ty / len;
            }
        } else if (PlaneAxes(m_hovered, pa, pb)) {
            m_planeNormal = m_startAxes[3 - pa - pb];
            if (!RayPlane(rayO, rayD, current.position, m_planeNormal, m_startHit)) return false;
        } else {
            // Axis drag: the plane containing the axis that faces the camera.
            const Vec3& a = m_startAxes[axis];
            Vec3 n = Math::Cross(a, Math::Cross(rayD, a));
            if (Length(n) < 1e-4f) return false;
            m_planeNormal = Math::Normalize(n);
            if (!RayPlane(rayO, rayD, current.position, m_planeNormal, m_startHit)) return false;
        }
        m_active = m_hovered;
        return false;
    }

    // ---- Continue / end a drag ----
    if (!in.leftDown) {
        m_active = GizmoHandle::None;
        return false;
    }
    out = m_start;
    const int axis = AxisIndex(m_active);

    if (m_mode == GizmoMode::Translate) {
        Vec3 hit;
        if (!RayPlane(rayO, rayD, m_start.position, m_planeNormal, hit)) return false;
        const Vec3 delta = hit - m_startHit;
        int pa = axis, pb = -1;
        if (axis < 0) PlaneAxes(m_active, pa, pb);
        const int moved[2] = {pa, pb};
        Vec3 pos = m_start.position;
        for (int k : moved) {
            if (k < 0) continue;
            f32 along = Math::Dot(delta, m_startAxes[k]);
            if (m_snap.translate && m_space == GizmoSpace::Local) {
                along = SnapTo(along, m_snap.translateStep);
            }
            pos = pos + m_startAxes[k] * along;
        }
        if (m_snap.translate && m_space == GizmoSpace::World) {
            // World grid: snap the absolute coordinates that moved.
            for (int k : moved) {
                if (k >= 0) Set(pos, k, SnapTo(Get(pos, k), m_snap.translateStep));
            }
        }
        out.position = pos;
        return true;
    }

    if (m_mode == GizmoMode::Rotate) {
        f32 radians;
        if (m_useScreenRotate) {
            const f32 dx = in.mouseX - m_startMouseX, dy = in.mouseY - m_startMouseY;
            radians = (dx * m_screenTangentX + dy * m_screenTangentY) * 0.012f;
        } else {
            Vec3 hit;
            if (!RayPlane(rayO, rayD, m_start.position, m_planeNormal, hit)) return false;
            const Vec3 v0 = m_startHit - m_start.position;
            const Vec3 v1 = hit - m_start.position;
            radians = std::atan2(Math::Dot(Math::Cross(v0, v1), m_planeNormal),
                                 Math::Dot(v0, v1));
        }
        if (m_snap.rotate) {
            radians = Math::Radians(SnapTo(radians * 180.0f / kPi, m_snap.rotateStepDeg));
        }
        // Rotate about the (world-space) handle axis: pre-multiply.
        const Mat4 r = AxisAngle(m_startAxes[axis], radians) *
                       RotationFromEuler(m_start.rotationEuler);
        out.rotationEuler = EulerFromRotation(r);
        return true;
    }

    // Scale (always local axes).
    auto snapScale = [&](f32 v) {
        if (m_snap.scale) v = SnapTo(v, m_snap.scaleStep);
        if (std::fabs(v) < 0.001f) v = v < 0.0f ? -0.001f : 0.001f;
        return v;
    };
    if (m_active == GizmoHandle::Uniform) {
        const f32 d = (in.mouseX - m_startMouseX) - (in.mouseY - m_startMouseY);
        const f32 factor = std::max(0.01f, 1.0f + d / 100.0f);
        for (int k = 0; k < 3; ++k) Set(out.scale, k, snapScale(Get(m_start.scale, k) * factor));
        return true;
    }
    Vec3 hit;
    if (!RayPlane(rayO, rayD, m_start.position, m_planeNormal, hit)) return false;
    const f32 along = Math::Dot(hit - m_startHit, m_startAxes[axis]);
    const f32 factor = 1.0f + along / L;
    Set(out.scale, axis, snapScale(Get(m_start.scale, axis) * factor));
    return true;
}

// --- Geometry ---------------------------------------------------------------------

const std::vector<LineVertex>& TransformGizmo::BuildLines(const GizmoTransform& t, f32 L,
                                                         const Mat4& view) {
    m_lines.clear();
    // Unreal's axis colors (UnrealWidget: X 0.594/0.0197/0, Y 0.1349/0.3959/0,
    // Z 0.0251/0.207/0.85 linear) converted to display values, since the
    // overlay is drawn after tone mapping.
    const Vec3 kAxisColor[3] = {{0.79f, 0.15f, 0.0f}, {0.40f, 0.66f, 0.0f},
                                {0.17f, 0.49f, 0.93f}};
    const Vec3 kHighlight{1.0f, 0.86f, 0.18f};
    const GizmoHandle shown = m_active != GizmoHandle::None ? m_active : m_hovered;
    auto colorOf = [&](GizmoHandle h, const Vec3& base) { return h == shown ? kHighlight : base; };
    auto line = [&](const Vec3& a, const Vec3& b, const Vec3& c) {
        m_lines.push_back({a, c});
        m_lines.push_back({b, c});
    };
    auto cube = [&](const Vec3& c, const Vec3 ax[3], f32 h, const Vec3& col) {
        Vec3 p[8];
        for (int i = 0; i < 8; ++i) {
            p[i] = c + ax[0] * ((i & 1) ? h : -h) + ax[1] * ((i & 2) ? h : -h) +
                   ax[2] * ((i & 4) ? h : -h);
        }
        const int e[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                              {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& k : e) line(p[k[0]], p[k[1]], col);
    };

    const Vec3& o = t.position;
    Vec3 ax[3];
    Axes(t, ax);

    if (m_mode == GizmoMode::Rotate) {
        const Mat4 invView = Math::Inverse(view);
        const Vec3 camPos{invView.m[12], invView.m[13], invView.m[14]};
        const Vec3 toCam = Math::Normalize(camPos - o);
        for (int i = 0; i < 3; ++i) {
            const Vec3& u = ax[(i + 1) % 3];
            const Vec3& v = ax[(i + 2) % 3];
            const Vec3 col = colorOf(AxisHandle(i), kAxisColor[i]);
            Vec3 prev = o + u * L;
            for (int s = 1; s <= kRingSegments; ++s) {
                const f32 ang = 2.0f * kPi * static_cast<f32>(s) / kRingSegments;
                const Vec3 p = o + (u * std::cos(ang) + v * std::sin(ang)) * L;
                // Dim the half of the ring facing away from the camera.
                const Vec3 mid = Math::Normalize((prev + p) * 0.5f - o);
                const bool back = Math::Dot(mid, toCam) < -0.05f;
                line(prev, p, back ? col * 0.38f : col);
                prev = p;
            }
        }
        return m_lines;
    }

    if (m_mode == GizmoMode::Translate) {
        for (int i = 0; i < 3; ++i) {
            const Vec3 col = colorOf(AxisHandle(i), kAxisColor[i]);
            const Vec3 base = o + ax[i] * (L * 0.80f);
            const Vec3 tip = o + ax[i] * L;
            line(o + ax[i] * (L * 0.12f), base, col);
            // Arrow head: a cone of lines around the axis.
            const Vec3& u = ax[(i + 1) % 3];
            const Vec3& v = ax[(i + 2) % 3];
            const f32 r = L * 0.045f;
            Vec3 prev = base + u * r;
            for (int s = 1; s <= 12; ++s) {
                const f32 ang = 2.0f * kPi * static_cast<f32>(s) / 12.0f;
                const Vec3 p = base + (u * std::cos(ang) + v * std::sin(ang)) * r;
                line(prev, p, col);
                line(p, tip, col);
                prev = p;
            }
        }
        // Plane handles: an L of two edges, each colored by the axis it runs along.
        for (GizmoHandle h : kPlanes) {
            int a, b;
            PlaneAxes(h, a, b);
            const bool hot = h == shown;
            const Vec3 ca = hot ? kHighlight : kAxisColor[a];
            const Vec3 cb = hot ? kHighlight : kAxisColor[b];
            const Vec3 p10 = o + ax[a] * (kPlaneMax * L) + ax[b] * (kPlaneMin * L);
            const Vec3 p11 = o + ax[a] * (kPlaneMax * L) + ax[b] * (kPlaneMax * L);
            const Vec3 p01 = o + ax[a] * (kPlaneMin * L) + ax[b] * (kPlaneMax * L);
            line(p10, p11, cb);  // runs along b
            line(p01, p11, ca);  // runs along a
            if (hot) {
                const Vec3 p00 = o + ax[a] * (kPlaneMin * L) + ax[b] * (kPlaneMin * L);
                line(p00, p10, kHighlight);
                line(p00, p01, kHighlight);
            }
        }
        return m_lines;
    }

    // Scale: box-tipped axes + uniform center box.
    for (int i = 0; i < 3; ++i) {
        const Vec3 col = colorOf(AxisHandle(i), kAxisColor[i]);
        line(o + ax[i] * (L * 0.16f), o + ax[i] * (L * 0.92f), col);
        cube(o + ax[i] * L, ax, L * 0.06f, col);
    }
    cube(o, ax, L * 0.08f, colorOf(GizmoHandle::Uniform, Vec3(0.85f, 0.85f, 0.85f)));
    return m_lines;
}

}  // namespace Luma
