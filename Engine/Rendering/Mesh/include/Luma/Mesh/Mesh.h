#pragma once

#include "Luma/Core/Types.h"
#include "Luma/Math/Math.h"
#include "Luma/RHI/MeshData.h"
#include "Luma/RHI/Renderer.h"

// Luma::Mesh - renderer-agnostic mesh geometry: built-in primitive shapes and
// conversion of imported meshes into the engine vertex format (MeshVertex /
// MeshSection / MeshData, defined by the RHI as the backend contract).

namespace Luma {

// Builds a unit-sized primitive centered at the origin (cube/sphere/cylinder
// ~1 unit, plane 1x1 on the XZ plane) with UVs, tangents and one section.
MeshData BuildPrimitive(MeshPrimitive primitive);

}  // namespace Luma
