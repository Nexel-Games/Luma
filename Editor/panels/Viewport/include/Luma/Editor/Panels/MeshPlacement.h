#pragma once

#include <optional>

#include "Luma/Core/Types.h"
#include "Luma/Math/Math.h"

// Where a static mesh dragged into the viewport lands (Unreal: trace the
// world under the cursor, fall back to a default point, snap to the location
// grid, and offset by the bounds so the mesh rests on the surface).

namespace Luma::Editor::Panels {

struct PlacementHit {
    Math::Vec3 point{0.0f, 0.0f, 0.0f};
    Math::Vec3 normal{0.0f, 1.0f, 0.0f};
};

struct MeshPlacementInput {
    Math::Vec3 rayOrigin{0.0f, 0.0f, 0.0f};  // world ray under the cursor
    Math::Vec3 rayDir{0.0f, 0.0f, -1.0f};
    std::optional<PlacementHit> hit;          // surface under the cursor, if any
    Math::Vec3 boundsMin{0.0f, 0.0f, 0.0f};   // mesh local bounds
    Math::Vec3 boundsMax{0.0f, 0.0f, 0.0f};
    bool snap = false;                        // gizmo location-grid snapping
    f32 snapStep = 0.1f;
};

// Distance along the ray used when nothing is hit and the ray never reaches
// the ground (Unreal's default drop distance, in Luma units).
inline constexpr f32 kDefaultDropDistance = 10.0f;

// World position for the new actor (rotation 0, scale 1): x/z from the hit
// (or the ground plane y = 0, or the default distance), snapped to the grid;
// y so that the bounds' bottom rests on the surface.
Math::Vec3 ComputeMeshPlacement(const MeshPlacementInput& in);

// Local bounds of a mesh as an asset's placement transform rotates (degrees,
// TransformComponent order) and scales it: the axis-aligned box around the
// transformed corners, so a placed model rests on the surface correctly.
void PlacedBounds(const Math::Vec3& boundsMin, const Math::Vec3& boundsMax, const Math::Vec3& rotationDegrees,
                  const Math::Vec3& scale, Math::Vec3& outMin, Math::Vec3& outMax);

// Ray vs axis-aligned box: the entry point and the face's outward normal
// (the exit point when the origin is inside). Nothing when missed or behind.
std::optional<PlacementHit> RayAabb(const Math::Vec3& origin, const Math::Vec3& dir,
                                    const Math::Vec3& lo, const Math::Vec3& hi);

}  // namespace Luma::Editor::Panels
