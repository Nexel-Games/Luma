#pragma once

#include <string>
#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/Math/Math.h"

// The engine vertex format: the contract between geometry producers
// (Luma::Mesh primitives, imported meshes) and the rendering backends.
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

// A coarser level of detail of a mesh: a new index list over the same
// vertices, with the same sections (a section may become empty) and the
// screen size below which it replaces the previous level.
struct MeshLodData {
    std::vector<u32> indices;
    std::vector<MeshSection> sections;  // same count/order as the source's
    f32 screenSize = 0.5f;              // fraction of the view height
};

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<u32> indices;
    std::vector<MeshSection> sections;  // primitives: exactly one
};

}  // namespace Luma
