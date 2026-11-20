#pragma once

#include <string>
#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/Math/Math.h"
#include "Luma/RHI/Renderer.h"

// Luma::Mesh - renderer-agnostic mesh geometry: the engine vertex format,
// built-in primitive shapes, and sections (one material slot each).
//
// UV convention: origin at the top-left of the image (v grows downward), the
// convention textures are sampled with. Tangent-space normal maps are
// OpenGL-style (+Y = image up); tangent.w is the handedness of
// (tangent, cross(normal, tangent) * w) — -1 for mirrored UVs.

namespace Luma {

struct MeshVertex {
    Math::Vec3 position;
    Math::Vec3 normal;
    Math::Vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};  // xyz direction, w handedness
    Math::Vec2 uv0{0.0f, 0.0f};
};

// A contiguous index range drawn with one material slot.
struct MeshSection {
    u32 indexOffset = 0;
    u32 indexCount = 0;
    std::string materialName;  // name from the source asset (display only)
};

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<u32> indices;
    std::vector<MeshSection> sections;  // primitives: exactly one
};

// Builds a unit-sized primitive centered at the origin (cube/sphere/cylinder
// ~1 unit, plane 1x1 on the XZ plane) with UVs, tangents and one section.
MeshData BuildPrimitive(MeshPrimitive primitive);

}  // namespace Luma
