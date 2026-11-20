#pragma once

#include "Luma/Mesh/Mesh.h"

namespace Luma {

// Computes per-vertex tangents from positions, normals and uv0
// (MikkTSpace-style: per-triangle UV derivatives accumulated per vertex,
// Gram-Schmidt orthogonalized against the normal). Handedness (tangent.w) is
// -1 where the UV mapping is mirrored. Vertices without usable UVs get an
// arbitrary unit tangent perpendicular to the normal.
void ComputeTangents(MeshData& mesh);

}  // namespace Luma
