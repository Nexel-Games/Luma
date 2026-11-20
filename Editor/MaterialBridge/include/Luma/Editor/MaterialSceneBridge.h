#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Luma/Asset/AssetId.h"
#include "Luma/Asset/AssetRegistry.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/RHI/SceneRenderer.h"
#include "Luma/Scene/Scene.h"

// Connects scene content to the renderer: resolves every material slot the
// scene uses through the MaterialLibrary, converts it to a MaterialGpuDesc,
// uploads its textures and the scene's mesh assets through the SceneRenderer,
// and builds the per-frame SceneInstance list.
//
// Uploads are cached: a material re-uploads only when its resolved version
// changes, a texture or mesh only when its file is reported changed.

namespace Luma::Editor {

struct MeshBounds {
    Math::Vec3 min{0.0f, 0.0f, 0.0f};
    Math::Vec3 max{0.0f, 0.0f, 0.0f};
};

class MaterialSceneBridge {
public:
    // Makes everything `scene` references resident in `renderer`. Call once
    // per frame before BuildInstances.
    void Sync(const Scene& scene, SceneRenderer& renderer,
              Material::MaterialLibrary& library, const AssetRegistry& registry);

    // One SceneInstance per entity with a Transform + Mesh Renderer. Section i
    // uses slot i; sections without a (non-empty) slot use the Default
    // Material.
    void BuildInstances(const Scene& scene, Entity selected,
                        std::vector<SceneInstance>& out) const;

    // A texture file changed on disk: re-upload it and refresh the materials
    // that sample it on the next Sync.
    void OnTextureFileChanged(std::string_view virtualPath);
    // A mesh asset changed on disk: re-upload it on the next Sync.
    void OnMeshFileChanged(const AssetId& mesh);

    // Local-space bounds of a loaded mesh asset (picking, framing).
    const MeshBounds* Bounds(const AssetId& mesh) const;

    // Renderer handle for a material slot path ("" = Default Material).
    MaterialHandle HandleFor(std::string_view materialPath) const;

    // Releases every uploaded resource (e.g. before the project closes).
    void Clear(SceneRenderer& renderer);

private:
    struct MaterialEntry {
        MaterialHandle handle = kDefaultMaterialHandle;
        u64 version = 0;
        bool uploaded = false;
        Math::Vec3 baseColor{0.82f, 0.82f, 0.85f};
        f32 metallic = 0.0f;
        f32 roughness = 0.5f;
        std::vector<std::string> textures;  // virtual paths it samples
    };
    struct TextureEntry {
        GpuTextureHandle handle = kInvalidHandle;
        bool srgb = true;
        bool failed = false;
    };
    struct MeshEntry {
        MeshHandle handle = kInvalidHandle;
        u32 sectionCount = 1;
        MeshBounds bounds;
        bool failed = false;
    };

    void SyncMaterial(const std::string& path, SceneRenderer& renderer,
                      Material::MaterialLibrary& library);
    const TextureEntry& SyncTexture(const std::string& path, SceneRenderer& renderer,
                                    const AssetPathResolver& paths);
    void SyncMesh(const AssetId& id, SceneRenderer& renderer, const AssetRegistry& registry);
    static std::string SlotKey(std::string_view path);

    std::unordered_map<std::string, MaterialEntry> m_materials;  // by slot key
    std::unordered_map<std::string, TextureEntry> m_textures;    // by virtual path
    std::unordered_map<AssetId, MeshEntry> m_meshes;
    std::vector<std::string> m_staleTextures;
    std::vector<AssetId> m_staleMeshes;
};

}  // namespace Luma::Editor
