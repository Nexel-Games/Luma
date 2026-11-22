#pragma once

#include <array>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "Luma/Asset/AssetPath.h"
#include "Luma/Material/GraphAssets.h"
#include "Luma/Material/Material.h"
#include "Luma/Material/MaterialInstance.h"

// Loads, caches, edits and resolves material assets by virtual path.
//
// Resolution (Unreal: a Material Instance's parameters fall back through its
// parent chain to the root Material): walk parents (max depth 16), apply each
// instance's overrides child-over-parent, then validate textures. Cycles,
// missing parents and over-deep chains resolve to the Default Material with a
// user-facing warning (once per asset). Missing texture files fall back to the
// input's constant, also warned once.
//
// Edits are in memory (dirty until Save) and bump a global version counter so
// renderers re-upload only what changed; a resolved result carries the
// combined version of its chain, so editing a parent re-resolves children.

namespace Luma::Material {

inline constexpr std::string_view kDefaultMaterialPath = "/Engine/Materials/DefaultMaterial";

struct ResolvedMaterial {
    MaterialSettings settings;
    MaterialInputs inputs = Material::DefaultInputs();
    std::string rootMaterial;  // virtual path of the root Material
    u64 version = 0;           // changes whenever any asset in the chain changes
};

class MaterialLibrary {
public:
    explicit MaterialLibrary(AssetPathResolver& paths);

    // nullptr when the path is not a (loadable) asset of that kind.
    const Material* GetMaterial(std::string_view path);
    const MaterialInstance* GetInstance(std::string_view path);
    bool IsInstance(std::string_view path);
    bool Exists(std::string_view path);

    // "" (or the Default Material path) resolves to the Default Material.
    const ResolvedMaterial& Resolve(std::string_view path);

    // In-memory edits (asset becomes dirty, version bumps).
    void SetMaterial(std::string_view path, Material material);
    void SetInstance(std::string_view path, MaterialInstance instance);

    // Writes the asset to its file. Clears dirty on success.
    bool Save(std::string_view path, std::string* outError = nullptr);
    bool IsDirty(std::string_view path) const;
    std::vector<std::string> DirtyAssets() const;

    // Re-reads the asset from disk (external change; graph assets reload on
    // their next Get*, so a deleted file stops resolving). Skipped, with a
    // warning, when the asset has unsaved edits.
    void Reload(std::string_view path);
    // Discards unsaved edits: re-reads the asset from disk; an asset that was
    // never saved returns to the state CreateInMemoryInstance made it in.
    void Revert(std::string_view path);
    // Re-validates textures (a texture file was added/removed/changed).
    void InvalidateTextures();

    // False when `parent` is missing, is `instance`, or already depends on it.
    bool CanSetParent(std::string_view instance, std::string_view parent);

    // Registers a new, dirty, not-yet-saved instance at `desiredPath` or the
    // first free "<path>_N". Returns the path it got.
    std::string CreateInMemoryInstance(std::string_view desiredPath, MaterialInstance instance);

    u64 Version(std::string_view path) const;
    const AssetPathResolver& Paths() const { return m_paths; }

    // --- Graph assets (v4 materials, v2 instances, functions, collections) --
    // Loaded lazily from .lmat (v4) / .lmatinst (v2) / .lmatfunc / .lmpc;
    // nullptr when missing, of another kind/version, or malformed (warned once).
    const GraphMaterial* GetGraphMaterial(std::string_view path);
    const GraphMaterialInstance* GetGraphInstance(std::string_view path);
    const MaterialFunction* GetFunction(std::string_view path);
    const ParameterCollection* GetCollection(std::string_view path);
    // In-memory edits (dirty until SaveGraphAsset; version bumps).
    void SetGraphMaterial(std::string_view path, GraphMaterial material);
    void SetGraphInstance(std::string_view path, GraphMaterialInstance instance);
    void SetFunction(std::string_view path, MaterialFunction function);
    void SetCollection(std::string_view path, ParameterCollection collection);
    // Runtime collection value change (not a file edit): bumps the
    // collection's revision. `value.x` for scalars. False if unknown.
    bool SetCollectionValue(std::string_view path, std::string_view parameter, Math::Vec4 value);
    bool SaveGraphAsset(std::string_view path, std::string* outError = nullptr);
    bool IsGraphAssetDirty(std::string_view path) const;
    u64 GraphVersion(std::string_view path) const;
    // Loaded graph assets that reference `path` (function calls, collection
    // parameters, textures, instance parents), sorted.
    std::vector<std::string> Dependents(std::string_view path) const;

private:
    struct Entry {
        std::variant<Material, MaterialInstance> asset;
        bool dirty = false;
        u64 version = 0;
    };
    struct CachedResolve {
        ResolvedMaterial resolved;
        u64 chainKey = 0;  // combined versions of the chain at resolve time
        bool valid = false;
    };

    Entry* Load(std::string_view path);
    const Entry* Find(std::string_view path) const;
    u64 ChainKey(std::string_view path);
    void WarnOnce(const std::string& key, const std::string& message);

    struct GraphEntry {
        std::variant<GraphMaterial, GraphMaterialInstance, MaterialFunction, ParameterCollection> asset;
        bool dirty = false;
        u64 version = 0;
    };
    template <typename T>
    T* LoadGraph(std::string_view path, const char* ext);
    template <typename T>
    void SetGraph(std::string_view path, T asset);

    AssetPathResolver& m_paths;
    std::unordered_map<std::string, GraphEntry> m_graph;
    std::unordered_map<std::string, Entry> m_entries;
    std::unordered_map<std::string, CachedResolve> m_resolved;
    std::set<std::string> m_missing;
    std::unordered_map<std::string, MaterialInstance> m_created;  // never-saved originals  // paths known not to exist (until reload)
    std::set<std::string> m_warned;
    u64 m_nextVersion = 1;
    u64 m_textureEpoch = 0;
};

}  // namespace Luma::Material
