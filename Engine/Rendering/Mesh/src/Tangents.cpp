#include "Luma/Mesh/Tangents.h"

#include <cmath>
#include <vector>

namespace Luma {
namespace {

using Math::Vec3;

f32 Length(const Vec3& v) { return std::sqrt(Math::Dot(v, v)); }

// Any unit vector perpendicular to `n`.
Vec3 AnyPerpendicular(const Vec3& n) {
    const Vec3 axis = std::fabs(n.x) < 0.9f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    return Math::Normalize(Math::Cross(Math::Cross(n, axis), n));
}

}  // namespace

void ComputeTangents(MeshData& mesh) {
    const usize count = mesh.vertices.size();
    std::vector<Vec3> tan(count, Vec3(0, 0, 0));
    std::vector<Vec3> bit(count, Vec3(0, 0, 0));  // dP/dv

    for (usize i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const u32 i0 = mesh.indices[i], i1 = mesh.indices[i + 1], i2 = mesh.indices[i + 2];
        if (i0 >= count || i1 >= count || i2 >= count) continue;
        const MeshVertex& v0 = mesh.vertices[i0];
        const MeshVertex& v1 = mesh.vertices[i1];
        const MeshVertex& v2 = mesh.vertices[i2];
        const Vec3 e1 = v1.position - v0.position;
        const Vec3 e2 = v2.position - v0.position;
        const f32 du1 = v1.uv0.x - v0.uv0.x, dv1 = v1.uv0.y - v0.uv0.y;
        const f32 du2 = v2.uv0.x - v0.uv0.x, dv2 = v2.uv0.y - v0.uv0.y;
        const f32 det = du1 * dv2 - du2 * dv1;
        if (std::fabs(det) < 1e-12f) continue;  // degenerate UVs
        const f32 r = 1.0f / det;
        const Vec3 t = (e1 * dv2 - e2 * dv1) * r;
        const Vec3 b = (e2 * du1 - e1 * du2) * r;
        // Area weighting comes from the unnormalized cross product length.
        const f32 w = Length(Math::Cross(e1, e2));
        if (w <= 0.0f) continue;
        for (u32 idx : {i0, i1, i2}) {
            tan[idx] = tan[idx] + t * w;
            bit[idx] = bit[idx] + b * w;
        }
    }

    for (usize i = 0; i < count; ++i) {
        MeshVertex& v = mesh.vertices[i];
        const Vec3 n = Math::Normalize(v.normal);
        Vec3 t = tan[i] - n * Math::Dot(n, tan[i]);
        if (Length(t) < 1e-8f) t = AnyPerpendicular(n);
        t = Math::Normalize(t);
        // Image-up bitangent is -dP/dv (UV origin top-left, GL-style maps).
        const Vec3 up = bit[i] * -1.0f;
        const f32 hand = Math::Dot(Math::Cross(n, t), up) < 0.0f ? -1.0f : 1.0f;
        v.tangent = {t.x, t.y, t.z, hand};
    }
}

}  // namespace Luma
