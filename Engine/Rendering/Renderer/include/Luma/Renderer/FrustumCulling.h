#pragma once

#include <cmath>

#include "Luma/Core/Types.h"
#include "Luma/Math/Math.h"

// View-frustum culling (pure math, backend-agnostic).
//
// Planes are extracted from a view-projection matrix (Gribb/Hartmann) for the
// engine's Vulkan clip space: column-major matrices, x/y in [-w, w], depth z
// in [0, w]. Each plane is normalized and points inward: a point p is inside
// when dot(n, p) + d >= 0 for every plane.
//
// Objects are tested as a world-space AABB obtained from the mesh's local
// bounds and the instance transform (conservative: never rejects anything
// visible). Shadow cascades use their light view-projection with the near
// plane ignored, since casters between the sun and the cascade still cast
// into it.

namespace Luma::Renderer2 {

struct Plane {
    Math::Vec3 normal{0.0f, 1.0f, 0.0f};
    f32 d = 0.0f;
    f32 Distance(const Math::Vec3& p) const { return normal.x * p.x + normal.y * p.y + normal.z * p.z + d; }
};

struct Aabb {
    Math::Vec3 center{0.0f, 0.0f, 0.0f};
    Math::Vec3 extent{0.0f, 0.0f, 0.0f};  // half size (>= 0)
};

enum FrustumPlane : u32 { kLeft = 0, kRight, kBottom, kTop, kNear, kFar, kPlaneCount };

struct Frustum {
    Plane planes[kPlaneCount];
    bool ignoreNear = false;  // shadow casting: anything toward the light counts

    static Frustum FromViewProj(const Math::Mat4& viewProj, bool ignoreNear = false) {
        const f32* m = viewProj.m;
        // Row i of the matrix (column-major storage).
        auto row = [m](int i) { return Math::Vec4{m[i], m[4 + i], m[8 + i], m[12 + i]}; };
        const Math::Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
        auto make = [](f32 a, f32 b, f32 c, f32 d) {
            Plane p;
            const f32 len = std::sqrt(a * a + b * b + c * c);
            const f32 inv = len > 1e-12f ? 1.0f / len : 0.0f;
            p.normal = {a * inv, b * inv, c * inv};
            p.d = d * inv;
            return p;
        };
        Frustum f;
        f.ignoreNear = ignoreNear;
        f.planes[kLeft] = make(r3.x + r0.x, r3.y + r0.y, r3.z + r0.z, r3.w + r0.w);
        f.planes[kRight] = make(r3.x - r0.x, r3.y - r0.y, r3.z - r0.z, r3.w - r0.w);
        f.planes[kBottom] = make(r3.x + r1.x, r3.y + r1.y, r3.z + r1.z, r3.w + r1.w);
        f.planes[kTop] = make(r3.x - r1.x, r3.y - r1.y, r3.z - r1.z, r3.w - r1.w);
        f.planes[kNear] = make(r2.x, r2.y, r2.z, r2.w);  // z >= 0 (Vulkan depth)
        f.planes[kFar] = make(r3.x - r2.x, r3.y - r2.y, r3.z - r2.z, r3.w - r2.w);
        return f;
    }

    bool ContainsPoint(const Math::Vec3& p) const {
        for (u32 i = 0; i < kPlaneCount; ++i) {
            if (ignoreNear && i == kNear) continue;
            if (planes[i].Distance(p) < 0.0f) return false;
        }
        return true;
    }

    bool IntersectsSphere(const Math::Vec3& center, f32 radius) const {
        for (u32 i = 0; i < kPlaneCount; ++i) {
            if (ignoreNear && i == kNear) continue;
            if (planes[i].Distance(center) < -radius) return false;
        }
        return true;
    }

    // Conservative AABB test: false only when the box is fully outside one plane.
    bool IntersectsAabb(const Aabb& box) const {
        for (u32 i = 0; i < kPlaneCount; ++i) {
            if (ignoreNear && i == kNear) continue;
            const Plane& p = planes[i];
            const f32 r = box.extent.x * std::fabs(p.normal.x) + box.extent.y * std::fabs(p.normal.y) +
                          box.extent.z * std::fabs(p.normal.z);
            if (p.Distance(box.center) < -r) return false;
        }
        return true;
    }
};

// World AABB of a local box [boundsMin, boundsMax] under a column-major
// affine transform (exact for the transformed box's enclosing AABB).
inline Aabb TransformAabb(const Math::Mat4& model, const Math::Vec3& boundsMin, const Math::Vec3& boundsMax) {
    const f32* m = model.m;
    const Math::Vec3 c{(boundsMin.x + boundsMax.x) * 0.5f, (boundsMin.y + boundsMax.y) * 0.5f,
                       (boundsMin.z + boundsMax.z) * 0.5f};
    const Math::Vec3 e{(boundsMax.x - boundsMin.x) * 0.5f, (boundsMax.y - boundsMin.y) * 0.5f,
                       (boundsMax.z - boundsMin.z) * 0.5f};
    Aabb out;
    out.center = {m[0] * c.x + m[4] * c.y + m[8] * c.z + m[12], m[1] * c.x + m[5] * c.y + m[9] * c.z + m[13],
                  m[2] * c.x + m[6] * c.y + m[10] * c.z + m[14]};
    out.extent = {std::fabs(m[0]) * e.x + std::fabs(m[4]) * e.y + std::fabs(m[8]) * e.z,
                  std::fabs(m[1]) * e.x + std::fabs(m[5]) * e.y + std::fabs(m[9]) * e.z,
                  std::fabs(m[2]) * e.x + std::fabs(m[6]) * e.y + std::fabs(m[10]) * e.z};
    return out;
}

}  // namespace Luma::Renderer2
