#include "Luma/Editor/Panels/MeshPlacement.h"

#include <algorithm>
#include <cmath>

namespace Luma::Editor::Panels {
namespace {

f32 SnapTo(f32 value, f32 step) { return step > 0.0f ? std::round(value / step) * step : value; }

}  // namespace

Math::Vec3 ComputeMeshPlacement(const MeshPlacementInput& in) {
    Math::Vec3 surface;
    if (in.hit) {
        surface = in.hit->point;
    } else if (in.rayDir.y < -1e-4f) {
        // The ground plane y = 0.
        const f32 t = -in.rayOrigin.y / in.rayDir.y;
        surface = in.rayOrigin + in.rayDir * t;
    } else {
        surface = in.rayOrigin + in.rayDir * kDefaultDropDistance;
    }
    Math::Vec3 position = surface;
    if (in.snap) {
        position.x = SnapTo(position.x, in.snapStep);
        position.z = SnapTo(position.z, in.snapStep);
    }
    // Rest the bounds' bottom on the surface, whatever the pivot.
    position.y = surface.y - in.boundsMin.y;
    return position;
}

std::optional<PlacementHit> RayAabb(const Math::Vec3& origin, const Math::Vec3& dir,
                                    const Math::Vec3& lo, const Math::Vec3& hi) {
    const f32 o[3] = {origin.x, origin.y, origin.z};
    const f32 d[3] = {dir.x, dir.y, dir.z};
    const f32 l[3] = {lo.x, lo.y, lo.z};
    const f32 h[3] = {hi.x, hi.y, hi.z};
    f32 tmin = -1e30f, tmax = 1e30f;
    int enterAxis = -1, exitAxis = -1;
    f32 enterSign = 0.0f, exitSign = 0.0f;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(d[i]) < 1e-9f) {
            if (o[i] < l[i] || o[i] > h[i]) return std::nullopt;
            continue;
        }
        f32 t0 = (l[i] - o[i]) / d[i];
        f32 t1 = (h[i] - o[i]) / d[i];
        // t0 enters through the face the ray faces: the low face when moving +.
        f32 sign0 = -1.0f, sign1 = 1.0f;
        if (t0 > t1) {
            std::swap(t0, t1);
            std::swap(sign0, sign1);
        }
        if (t0 > tmin) {
            tmin = t0;
            enterAxis = i;
            enterSign = sign0;
        }
        if (t1 < tmax) {
            tmax = t1;
            exitAxis = i;
            exitSign = sign1;
        }
        if (tmin > tmax) return std::nullopt;
    }
    if (tmax < 0.0f) return std::nullopt;
    const bool inside = tmin < 0.0f;
    const f32 t = inside ? tmax : tmin;
    const int axis = inside ? exitAxis : enterAxis;
    const f32 sign = inside ? exitSign : enterSign;
    PlacementHit hit;
    hit.point = origin + dir * t;
    hit.normal = {axis == 0 ? sign : 0.0f, axis == 1 ? sign : 0.0f, axis == 2 ? sign : 0.0f};
    if (axis < 0) hit.normal = {0.0f, 1.0f, 0.0f};
    return hit;
}

}  // namespace Luma::Editor::Panels
