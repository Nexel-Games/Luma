#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Luma/Asset/AssetId.h"
#include "Luma/Asset/AssetRegistry.h"
#include "Luma/Editor/MaterialGpuCompile.h"
#include "Luma/Jobs/JobSystem.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/MaterialCompiler/Preshader.h"
#include "Luma/RHI/SceneRenderer.h"
#include "Luma/Scene/Scene.h"

// Connects scene content to the renderer: resolves every material slot the
// scene uses through the MaterialLibrary, compiles its graph (Translator ->
// shader compiler) into a CompiledMaterialGpu plus parameter block,
// uploads its textures and the scene's mesh assets through the SceneRenderer,
// and builds the per-frame SceneInstance list.
//
// Uploads are cached: a material re-uploads only when its resolved version
// changes, a texture or mesh only when its file is reported changed.

namespace Luma::ShaderCompiler {
class Compiler;
}

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
    MaterialSceneBridge();
    ~MaterialSceneBridge();
    MaterialSceneBridge(const MaterialSceneBridge&) = delete;
    MaterialSceneBridge& operator=(const MaterialSceneBridge&) = delete;

    // Where generated shaders find their #includes and where compiled SPIR-V
    // is cached. Defaults: the engine shader directory, and
    // <project>/Saved/ShaderCache (beside the project's Assets folder).
    void SetShaderPaths(std::filesystem::path includeDir, std::filesystem::path cacheDir);

    // Mesh LODs: every mesh asset loaded from now on gets its LOD chain from
    // a background job on `jobs` (read from `cacheDir` when an up-to-date
    // cache exists, else simplified and cached there) and handed to the
    // renderer on a later Sync. Until then the mesh draws at full detail.
    // Null `jobs` = no LODs.
    void EnableMeshLods(Jobs::JobSystem* jobs, std::filesystem::path cacheDir);
    // LOD builds still running (tests / status).
    usize PendingLodBuilds() const { return m_lodBuilds.size(); }

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

    // Shader compiles run on worker threads; Sync lands finished ones. This
    // blocks until every pending compile has landed (previews, thumbnails).
    void FinishCompiles(SceneRenderer& renderer, Material::MaterialLibrary& library);
    // Materials waiting for their shaders.
    u32 PendingCompiles() const;
    // Distinct shader sets submitted so far (instances sharing a permutation
    // share one).
    u32 ShaderSetsCompiled() const { return m_setsCompiled; }
    // The last compile errors of a slot path ("" when it compiled).
    std::string_view Errors(std::string_view materialPath) const;

    // Releases every uploaded resource (e.g. before the project closes).
    void Clear(SceneRenderer& renderer);

private:
    struct MaterialEntry {
        MaterialHandle handle = kDefaultMaterialHandle;
        u64 version = 0;
        bool uploaded = false;
        std::vector<std::string> textures;  // virtual paths it samples
        std::string reportedErrors;         // last error set logged (once per distinct set)
        // For parameter-only refreshes (collection value changes).
        MaterialCompiler::PreshaderProgram preshader;
        MaterialCompiler::ParameterBlockLayout layout;
        std::vector<Material::ParameterOverride> overrides;
        std::map<std::string, u32> collectionRevisions;  // collection path -> revision used
        std::string fingerprint;                 // shader set drawing now ("" = none yet)
        CompiledMaterialGpu gpu;                 // what was last uploaded
        std::optional<GpuMaterialBuild> pending;  // waiting for its shader set
        std::string label;
    };
    // One compiled shader set, shared by every material with the same GLSL.
    struct ShaderSet {
        std::vector<GpuShaderSource> sources;
        std::vector<std::vector<u32>> spirv;  // per source
        std::vector<u64> keys;                // per source
        u32 remaining = 0;
        bool failed = false;
        std::string errors;
        u32 instructions = 0;
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
    ShaderCompiler::Compiler& Shaders(const Material::MaterialLibrary& library);
    void ProcessCompiles(SceneRenderer& renderer, Material::MaterialLibrary& library);
    void Finalize(const std::string& path, MaterialEntry& entry, const ShaderSet& set, SceneRenderer& renderer,
                  Material::MaterialLibrary& library);
    void ReportErrors(MaterialEntry& entry, const std::string& errors);
    // Uploads `build`'s textures; true when its bindings differ from `entry`'s.
    bool BindTextures(MaterialEntry& entry, GpuMaterialBuild& build, SceneRenderer& renderer,
                      Material::MaterialLibrary& library);

    std::unordered_map<std::string, MaterialEntry> m_materials;  // by slot key
    std::unordered_map<std::string, TextureEntry> m_textures;    // by virtual path
    std::unordered_map<std::string, MeshEntry> m_meshes;  // by virtual path
    struct LodBuild;
    std::vector<std::shared_ptr<LodBuild>> m_lodBuilds;
    Jobs::JobSystem* m_lodJobs = nullptr;
    std::filesystem::path m_lodCacheDir;
    void PollLodBuilds(SceneRenderer& renderer);
    std::vector<std::string> m_staleTextures;
    std::vector<std::string> m_staleMeshes;
    std::optional<std::pair<std::string, Math::Mat4>> m_preview;  // mesh path, model
    std::unique_ptr<ShaderCompiler::Compiler> m_shaders;
    std::filesystem::path m_shaderInclude;
    std::filesystem::path m_shaderCache;
    std::unordered_map<std::string, ShaderSet> m_sets;                // by fingerprint
    std::unordered_map<u64, std::pair<std::string, u32>> m_tickets;  // job -> (set, source index)
    u32 m_setsCompiled = 0;
};

}  // namespace Luma::Editor
