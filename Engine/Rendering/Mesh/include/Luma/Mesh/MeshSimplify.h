#pragma once

#include <span>
#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/RHI/MeshData.h"

// Mesh simplification and automatic LOD chains.
//
// SimplifyIndices is a quadric-error-metric (Garland & Heckbert) edge
// collapser that only rewrites the index list: every collapse moves a vertex
// onto one of its existing neighbours, so the vertex buffer (positions, UVs,
// normals, tangents) is shared by every LOD unchanged. Open borders and UV /
// normal seams (vertices sharing a position) are locked, collapses that would
// flip a triangle are rejected, and a collapse whose error exceeds the bound
// stops the simplification.
//
// BuildMeshLods simplifies every section independently (so section/material
// boundaries stay exact) into a chain of progressively coarser LODs and gives
// each one the screen size at which it takes over (Unreal-style: the fraction
// of the screen height the bounding sphere covers).

namespace Luma::Mesh {

struct SimplifyOptions {
    usize targetIndexCount = 0;  // stop at or below this many indices
    // Largest allowed error, relative to the mesh's bounding-box diagonal
    // (0.01 = 1 % of the mesh size). Stops early when reached.
    f32 maxRelativeError = 0.01f;
};

// Simplified copy of `indices` (a triangle list over `vertices`). `outError`
// receives the relative error of the last collapse (0 when nothing changed).
std::vector<u32> SimplifyIndices(std::span<const MeshVertex> vertices, std::span<const u32> indices,
                                 const SimplifyOptions& options, f32* outError = nullptr);

struct LodSettings {
    u32 maxLods = 4;                 // including LOD0 (the source)
    f32 reductionPerLod = 0.5f;      // each LOD keeps this fraction of the previous one's triangles
    f32 minReduction = 0.75f;        // stop when a LOD keeps more than this fraction (no real gain)
    f32 maxRelativeError = 0.02f;    // per LOD, relative to the bounding-box diagonal
    u32 minTrianglesForLods = 512;   // smaller meshes keep only LOD0
};

// LOD1.. (LOD0 is the source mesh itself; not included). Empty when the mesh
// is too small or does not simplify.
std::vector<MeshLodData> BuildMeshLods(const MeshData& mesh, const LodSettings& settings = {});

// Screen size (fraction of the view height covered by a bounding sphere of
// `radius` at `distance`) for a perspective projection with vertical FOV
// `fovY` (radians). Unreal's ComputeBoundsScreenSize.
f32 BoundsScreenSize(f32 radius, f32 distance, f32 fovY);

// LOD index for `screenSize` (0 when no LOD applies): the coarsest LOD whose
// threshold the object has shrunk below, then shifted by `bias` (positive =
// coarser) and clamped to the available LODs.
u32 SelectLod(std::span<const MeshLodData> lods, f32 screenSize, i32 bias = 0);

}  // namespace Luma::Mesh
