#pragma once

#include "Luma/Asset/LumaMesh.h"
#include "Luma/Mesh/Mesh.h"

namespace Luma {

// Converts an imported .lmesh into the engine vertex format: positions,
// normals and uv0 copied; tangents always recomputed from the UVs (the
// importer stores a synthesized bitangent, so stored handedness is
// unreliable); one MeshSection per submesh (one section covering every index
// when the mesh has no submeshes).
MeshData ConvertLumaMesh(const LumaMeshData& source);

}  // namespace Luma
