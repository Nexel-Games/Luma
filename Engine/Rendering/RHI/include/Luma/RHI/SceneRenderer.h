#pragma once

#include "Luma/RHI/MaterialGpuDesc.h"
#include "Luma/RHI/MeshData.h"
#include "Luma/RHI/Renderer.h"

namespace Luma {

// GPU residency for scene content: meshes, textures and materials. A backend
// implements this next to its scene pass; the editor uploads assets through
// it and references the returned handles from SceneInstance.
//
// `key` identifies the source asset (e.g. a hash of its virtual path):
// uploading the same key again returns the existing handle (meshes and
// textures) or rewrites the entry in place (materials). Released resources are
// destroyed once the GPU no longer uses them.
class SceneRenderer {
public:
    virtual ~SceneRenderer() = default;

    virtual MeshHandle UploadMesh(u64 key, const MeshData& mesh) = 0;
    virtual void ReleaseMesh(MeshHandle mesh) = 0;

    virtual GpuTextureHandle UploadTexture(u64 key, const TextureUploadDesc& texture) = 0;
    virtual void ReleaseTexture(GpuTextureHandle texture) = 0;

    // Creates or replaces the material for `key` (kDefaultMaterialKey always
    // maps to kDefaultMaterialHandle). Its pipelines are built for every pass
    // in `material`; until all exist, the handle keeps drawing with the
    // previous shader set (or not at all, for a new material).
    virtual MaterialHandle UpdateMaterial(u64 key, const CompiledMaterialGpu& material,
                                          const MaterialParameterBlock& parameters) = 0;
    // New parameter values for an existing material (instance edits,
    // collection changes): no shader or pipeline work.
    virtual void UpdateMaterialParameters(MaterialHandle material, const MaterialParameterBlock& parameters) = 0;
    virtual void ReleaseMaterial(MaterialHandle material) = 0;

    // Built-in shapes, uploaded when the backend starts.
    virtual MeshHandle PrimitiveMesh(MeshPrimitive primitive) const = 0;
};

}  // namespace Luma
