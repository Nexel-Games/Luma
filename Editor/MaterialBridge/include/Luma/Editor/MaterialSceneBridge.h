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

// One material section of a static mesh (Unreal: a material slot).
struct MeshSectionInfo {
    std::string slotName;         // shown in the Inspector ("Paint", "Element 0")
    std::string defaultMaterial;  // the mesh's own material (virtual path) or ""
};

class MaterialSceneBridge {
public:
    // Makes everything `scene` references resident in `renderer`. Call once
    // per frame before BuildInstances.
    void Sync(const Scene& scene, SceneRenderer& renderer, Material::MaterialLibrary& library);

    // One SceneInstance per entity with a Transform + Mesh Renderer. Section i
    // uses override slot i; an empty/missing slot uses the mesh's own material
    // for that section, else the Default Material.
    void BuildInstances(const Scene& scene, Entity selected,
                        std::vector<SceneInstance>& out) const;

    // A texture file changed on disk: re-upload it and refresh the materials
    // that sample it on the next Sync.
    void OnTextureFileChanged(std::string_view virtualPath);
    // A mesh asset (virtual path) changed on disk, or was renamed/deleted:
    // re-read it on the next Sync.
    void OnMeshFileChanged(std::string_view meshPath);

    // Local-space bounds of a loaded mesh asset (picking, framing, placement).
    const MeshBounds* Bounds(std::string_view meshPath) const;
    // Material sections of a loaded mesh; null when it is not loaded (yet) or
    // failed to load.
    const std::vector<MeshSectionInfo>* Sections(std::string_view meshPath) const;
    // Loads `meshPath` now if it is not resident (e.g. for a drag preview).
    void EnsureMesh(std::string_view meshPath, SceneRenderer& renderer,
                    const AssetPathResolver& paths);

    // A transient mesh drawn as if placed (the viewport's drag-and-drop
    // preview): synced with its own materials, appended to BuildInstances as a
    // selected instance, never part of the scene.
    void SetPlacementPreview(std::string meshPath, const Math::Mat4& model);
    void ClearPlacementPreview();

    // Material sections of a Mesh Renderer's mesh (1 for primitives and for
    // mesh assets not loaded yet).
    u32 SectionCount(const MeshRendererComponent& mesh) const;

    // Renderer handle for a material slot path ("" = Default Material).
    MaterialHandle HandleFor(std::string_view materialPath) const;

    // Releases every uploaded resource (e.g. before the project closes).
    void Clear(SceneRenderer& renderer);

private:
    struct MaterialEntry {
        MaterialHandle handle = kDefaultMaterialHandle;
        u64 version = 0;
        bool uploaded = false;
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
        std::vector<MeshSectionInfo> sections;
        bool failed = false;
    };

    void SyncMaterial(const std::string& path, SceneRenderer& renderer,
                      Material::MaterialLibrary& library);
    const TextureEntry& SyncTexture(const std::string& path, SceneRenderer& renderer,
                                    const AssetPathResolver& paths);
    void SyncMesh(const std::string& path, SceneRenderer& renderer, const AssetPathResolver& paths);
    static std::string SlotKey(std::string_view path);

    std::unordered_map<std::string, MaterialEntry> m_materials;  // by slot key
    std::unordered_map<std::string, TextureEntry> m_textures;    // by virtual path
    std::unordered_map<std::string, MeshEntry> m_meshes;  // by virtual path
    std::vector<std::string> m_staleTextures;
    std::vector<std::string> m_staleMeshes;
    std::optional<std::pair<std::string, Math::Mat4>> m_preview;  // mesh path, model
};

}  // namespace Luma::Editor
