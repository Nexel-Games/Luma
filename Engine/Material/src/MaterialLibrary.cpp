#include "Luma/Asset/AssetTypeRegistry.h"
#include "Luma/Material/MaterialLibrary.h"

#include <type_traits>

#include "Luma/Material/MaterialMigrationV4.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "Luma/Asset/AssetType.h"
#include "Luma/Core/Log.h"
#include "Luma/Material/MaterialInstanceSerializer.h"
#include "Luma/Material/MaterialSerializer.h"

namespace Luma::Material {
namespace {

namespace fs = std::filesystem;

constexpr int kMaxChainDepth = 16;

std::string Normalize(std::string_view path) {
    return path.empty() ? std::string(kDefaultMaterialPath) : std::string(path);
}

std::string LeafName(std::string_view path) {
    const usize slash = path.find_last_of('/');
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

}  // namespace

MaterialLibrary::MaterialLibrary(AssetPathResolver& paths) : m_paths(paths) {}

void MaterialLibrary::WarnOnce(const std::string& key, const std::string& message) {
    if (m_warned.insert(key).second) LUMA_USER_WARN("Material", "{}", message);
}

const MaterialLibrary::Entry* MaterialLibrary::Find(std::string_view path) const {
    auto it = m_entries.find(Normalize(path));
    return it == m_entries.end() ? nullptr : &it->second;
}

MaterialLibrary::Entry* MaterialLibrary::Load(std::string_view rawPath) {
    const std::string path = Normalize(rawPath);
    if (auto it = m_entries.find(path); it != m_entries.end()) return &it->second;
    if (m_missing.count(path)) return nullptr;

    Entry entry;
    std::string error;
    if (auto file = m_paths.ToFile(path, {"lmat"})) {
        Material m;
        if (!MaterialSerializer::LoadFromFile(m, *file, &error)) {
            WarnOnce("load:" + path, "Could not read material '" + path + "': " + error);
            if (path != kDefaultMaterialPath) {
                m_missing.insert(path);
                return nullptr;
            }
            // Every resolve falls back to the Default Material: it must exist.
            m = Material::MakeDefault();
        }
        entry.asset = std::move(m);
    } else if (auto instFile = m_paths.ToFile(path, {"lmatinst"})) {
        MaterialInstance mi;
        if (!MaterialInstanceSerializer::LoadFromFile(mi, *instFile, &error)) {
            WarnOnce("load:" + path, "Could not read material instance '" + path + "': " + error);
            m_missing.insert(path);
            return nullptr;
        }
        entry.asset = std::move(mi);
    } else if (path == kDefaultMaterialPath) {
        // Engine content not mounted (tests, stripped builds): built-in default.
        entry.asset = Material::MakeDefault();
    } else {
        m_missing.insert(path);
        return nullptr;
    }
    entry.version = m_nextVersion++;
    return &m_entries.emplace(path, std::move(entry)).first->second;
}

const Material* MaterialLibrary::GetMaterial(std::string_view path) {
    Entry* e = Load(path);
    return e ? std::get_if<Material>(&e->asset) : nullptr;
}

const MaterialInstance* MaterialLibrary::GetInstance(std::string_view path) {
    Entry* e = Load(path);
    return e ? std::get_if<MaterialInstance>(&e->asset) : nullptr;
}

bool MaterialLibrary::IsInstance(std::string_view path) { return GetInstance(path) != nullptr; }
bool MaterialLibrary::Exists(std::string_view path) { return Load(path) != nullptr; }

u64 MaterialLibrary::ChainKey(std::string_view start) {
    u64 key = 1469598103934665603ull ^ m_textureEpoch;
    std::string cur = Normalize(start);
    for (int depth = 0; depth <= kMaxChainDepth; ++depth) {
        Entry* e = Load(cur);
        key = (key ^ (e ? e->version : 0xFFFFull)) * 1099511628211ull;
        const auto* mi = e ? std::get_if<MaterialInstance>(&e->asset) : nullptr;
        if (!mi) break;
        cur = Normalize(mi->parent);
    }
    return key;
}

const ResolvedMaterial& MaterialLibrary::Resolve(std::string_view rawPath) {
    const std::string path = Normalize(rawPath);
    const u64 key = ChainKey(path);
    CachedResolve& cache = m_resolved[path];
    if (cache.valid && cache.chainKey == key) return cache.resolved;

    ResolvedMaterial out;
    // Collect the instance chain (child first) down to the root Material.
    std::vector<const MaterialInstance*> chain;
    std::vector<std::string> visited;
    std::string cur = path;
    const Material* root = nullptr;
    bool fallback = false;
    for (int depth = 0;; ++depth) {
        if (depth > kMaxChainDepth) {
            WarnOnce("depth:" + path, "Material '" + path +
                                          "' has a parent chain deeper than 16 — using the "
                                          "Default Material.");
            fallback = true;
            break;
        }
        if (std::find(visited.begin(), visited.end(), cur) != visited.end()) {
            WarnOnce("cycle:" + path, "Material instance '" + path +
                                          "' has a circular parent chain — using the "
                                          "Default Material.");
            fallback = true;
            break;
        }
        visited.push_back(cur);
        Entry* e = Load(cur);
        if (!e) {
            WarnOnce("parent:" + path + ">" + cur,
                     "Material '" + path + "' references missing material '" + cur +
                         "' — using the Default Material.");
            fallback = true;
            break;
        }
        if (const auto* m = std::get_if<Material>(&e->asset)) {
            root = m;
            out.rootMaterial = cur;
            break;
        }
        const auto& mi = std::get<MaterialInstance>(e->asset);
        chain.push_back(&mi);
        cur = Normalize(mi.parent);
    }
    if (fallback || !root) {
        chain.clear();
        root = GetMaterial(kDefaultMaterialPath);
        out.rootMaterial = std::string(kDefaultMaterialPath);
    }
    out.settings = root->settings;
    out.inputs = root->inputs;
    // Apply overrides from the instance nearest the root to the leaf.
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const MaterialInstance& mi = **it;
        for (usize i = 0; i < kMaterialParameterCount; ++i) {
            if (mi.overridden[i]) out.inputs[i] = mi.inputs[i];
        }
        if (mi.blendMode) out.settings.blendMode = *mi.blendMode;
        if (mi.twoSided) out.settings.twoSided = *mi.twoSided;
        if (mi.opacityMaskClip) out.settings.opacityMaskClip = *mi.opacityMaskClip;
    }
    // Textures that no longer exist fall back to the input's constant.
    for (MaterialInput& input : out.inputs) {
        if (input.texture.empty()) continue;
        if (!m_paths.ToFileAny(input.texture, AssetTypes().FindBuiltin(AssetType::Texture)->extensions)) {
            WarnOnce("texture:" + path + ">" + input.texture,
                     "Texture '" + input.texture + "' used by '" + path +
                         "' is missing — using the constant value.");
            input.texture.clear();
        }
    }
    out.version = m_nextVersion++;
    cache.resolved = std::move(out);
    cache.chainKey = key;
    cache.valid = true;
    return cache.resolved;
}

void MaterialLibrary::SetMaterial(std::string_view rawPath, Material material) {
    const std::string path = Normalize(rawPath);
    m_missing.erase(path);
    Entry& e = m_entries[path];
    e.asset = std::move(material);
    e.dirty = true;
    e.version = m_nextVersion++;
}

void MaterialLibrary::SetInstance(std::string_view rawPath, MaterialInstance instance) {
    const std::string path = Normalize(rawPath);
    m_missing.erase(path);
    Entry& e = m_entries[path];
    e.asset = std::move(instance);
    e.dirty = true;
    e.version = m_nextVersion++;
}

bool MaterialLibrary::Save(std::string_view rawPath, std::string* outError) {
    const std::string path = Normalize(rawPath);
    auto it = m_entries.find(path);
    if (it == m_entries.end()) {
        if (outError) *outError = "material '" + path + "' is not loaded";
        return false;
    }
    Entry& e = it->second;
    const bool isInstance = std::holds_alternative<MaterialInstance>(e.asset);
    const char* ext = isInstance ? "lmatinst" : "lmat";
    auto file = m_paths.ToFile(path, {ext});
    if (!file) file = m_paths.MakeFilePath(path, ext);
    if (!file) {
        if (outError) *outError = "'" + path + "' is not under the project or engine content";
        return false;
    }
    std::error_code ec;
    fs::create_directories(file->parent_path(), ec);
    const bool ok = isInstance
                        ? MaterialInstanceSerializer::SaveToFile(std::get<MaterialInstance>(e.asset),
                                                                 *file, outError)
                        : MaterialSerializer::SaveToFile(std::get<Material>(e.asset), *file,
                                                         outError);
    if (ok) e.dirty = false;
    return ok;
}

bool MaterialLibrary::IsDirty(std::string_view path) const {
    const Entry* e = Find(path);
    return e && e->dirty;
}

std::vector<std::string> MaterialLibrary::DirtyAssets() const {
    std::vector<std::string> out;
    for (const auto& [path, e] : m_entries) {
        if (e.dirty) out.push_back(path);
    }
    std::sort(out.begin(), out.end());
    return out;
}

void MaterialLibrary::Reload(std::string_view rawPath) {
    const std::string path = Normalize(rawPath);
    // Graph assets (v4 materials, instances, functions, collections) reload
    // lazily on their next Get*; a deleted file then stops resolving.
    if (const auto it = m_graph.find(path); it != m_graph.end()) {
        if (it->second.dirty) {
            LUMA_USER_WARN("Material", "'{}' changed on disk but has unsaved edits — keeping the edits.", path);
            return;
        }
        m_graph.erase(it);
    }
    if (IsDirty(path)) {
        LUMA_USER_WARN("Material", "'{}' changed on disk but has unsaved edits — keeping the edits.",
                       path);
        return;
    }
    m_entries.erase(path);
    m_missing.erase(path);
    Load(path);  // fresh version for the reloaded asset
}

void MaterialLibrary::Revert(std::string_view rawPath) {
    const std::string path = Normalize(rawPath);
    m_entries.erase(path);
    m_missing.erase(path);
    if (Load(path)) {
        m_created.erase(path);  // on disk now: the file is the reference
        return;
    }
    // Never saved: back to the state it was created in (e.g. a migrated
    // instance), still unsaved.
    if (auto it = m_created.find(path); it != m_created.end()) {
        m_missing.erase(path);
        SetInstance(path, it->second);
    }
}

void MaterialLibrary::InvalidateTextures() { ++m_textureEpoch; }

bool MaterialLibrary::CanSetParent(std::string_view rawInstance, std::string_view rawParent) {
    const std::string instance = Normalize(rawInstance);
    std::string cur = Normalize(rawParent);
    if (cur == instance || !Exists(cur)) return false;
    for (int depth = 0; depth <= kMaxChainDepth; ++depth) {
        if (cur == instance) return false;
        const MaterialInstance* mi = GetInstance(cur);
        if (!mi) return true;  // reached a root Material without meeting `instance`
        cur = Normalize(mi->parent);
        if (!Exists(cur)) return false;
    }
    return false;  // too deep
}

std::string MaterialLibrary::CreateInMemoryInstance(std::string_view desiredPath,
                                                    MaterialInstance instance) {
    const std::string base(desiredPath);
    std::string candidate = base;
    for (int n = 1; Exists(candidate) || m_paths.ToFile(candidate).has_value(); ++n) {
        candidate = base + "_" + std::to_string(n);
    }
    instance.name = LeafName(candidate);
    m_created[candidate] = instance;  // what Revert restores until it is saved
    SetInstance(candidate, std::move(instance));
    return candidate;
}

u64 MaterialLibrary::Version(std::string_view path) const {
    const Entry* e = Find(path);
    return e ? e->version : 0;
}

// --- Graph assets -------------------------------------------------------------

template <typename T>
T* MaterialLibrary::LoadGraph(std::string_view rawPath, const char* ext) {
    const std::string path = Normalize(rawPath);
    if (auto it = m_graph.find(path); it != m_graph.end()) return std::get_if<T>(&it->second.asset);
    auto file = m_paths.ToFile(path, {ext});
    if (!file) return nullptr;
    T asset;
    std::string err;
    // Phase-1 files (version < 4) convert on load; the converted asset is
    // clean (not dirty) and is written in the new format on its next save.
    if constexpr (std::is_same_v<T, GraphMaterial> || std::is_same_v<T, GraphMaterialInstance>) {
        if (GraphAssetSerializer::ProbeVersion(*file) < kGraphMaterialVersion) {
            std::vector<std::string> notes;
            if constexpr (std::is_same_v<T, GraphMaterial>) {
                const Material* legacy = GetMaterial(path);
                if (!legacy) return nullptr;
                asset = MigrateV3(*legacy, &notes);
            } else {
                const MaterialInstance* legacy = GetInstance(path);
                if (!legacy) return nullptr;
                const ResolvedMaterial& parent = Resolve(legacy->parent);
                Material resolved;
                resolved.settings = parent.settings;
                resolved.inputs = parent.inputs;
                asset = MigrateInstanceV1(*legacy, resolved, &notes);
            }
            for (const std::string& n : notes) LUMA_LOG_INFO("Materials", "{}: {}", path, n);
            LUMA_USER_INFO("Materials", "'{}' was converted to the node-graph format (saved on next save)",
                           path);
            GraphEntry& e = m_graph[path];
            e.asset = std::move(asset);
            e.version = m_nextVersion++;
            return std::get_if<T>(&e.asset);
        }
    }
    if (!GraphAssetSerializer::Load(asset, *file, &err)) {
        WarnOnce("graph:" + path, "'" + path + "' could not be loaded: " + err);
        return nullptr;
    }
    GraphEntry& e = m_graph[path];
    e.asset = std::move(asset);
    e.version = m_nextVersion++;
    return std::get_if<T>(&e.asset);
}

template <typename T>
void MaterialLibrary::SetGraph(std::string_view rawPath, T asset) {
    GraphEntry& e = m_graph[Normalize(rawPath)];
    e.asset = std::move(asset);
    e.dirty = true;
    e.version = m_nextVersion++;
}

const GraphMaterial* MaterialLibrary::GetGraphMaterial(std::string_view path) {
    return LoadGraph<GraphMaterial>(path, "lmat");
}
const GraphMaterialInstance* MaterialLibrary::GetGraphInstance(std::string_view path) {
    return LoadGraph<GraphMaterialInstance>(path, "lmatinst");
}
const MaterialFunction* MaterialLibrary::GetFunction(std::string_view path) {
    return LoadGraph<MaterialFunction>(path, "lmatfunc");
}
const ParameterCollection* MaterialLibrary::GetCollection(std::string_view path) {
    return LoadGraph<ParameterCollection>(path, "lmpc");
}
void MaterialLibrary::SetGraphMaterial(std::string_view path, GraphMaterial material) {
    SetGraph(path, std::move(material));
}
void MaterialLibrary::SetGraphInstance(std::string_view path, GraphMaterialInstance instance) {
    SetGraph(path, std::move(instance));
}
void MaterialLibrary::SetFunction(std::string_view path, MaterialFunction function) {
    SetGraph(path, std::move(function));
}
void MaterialLibrary::SetCollection(std::string_view path, ParameterCollection collection) {
    SetGraph(path, std::move(collection));
}

bool MaterialLibrary::SetCollectionValue(std::string_view path, std::string_view parameter,
                                         Math::Vec4 value) {
    if (!LoadGraph<ParameterCollection>(path, "lmpc")) return false;
    auto* pc = std::get_if<ParameterCollection>(&m_graph[Normalize(path)].asset);
    for (auto& s : pc->scalars) {
        if (s.name == parameter) {
            s.value = value.x;
            ++pc->revision;
            return true;
        }
    }
    for (auto& v : pc->vectors) {
        if (v.name == parameter) {
            v.value = value;
            ++pc->revision;
            return true;
        }
    }
    return false;
}

bool MaterialLibrary::SaveGraphAsset(std::string_view rawPath, std::string* outError) {
    const std::string path = Normalize(rawPath);
    auto it = m_graph.find(path);
    if (it == m_graph.end()) {
        if (outError) *outError = "'" + path + "' is not loaded";
        return false;
    }
    GraphEntry& e = it->second;
    const char* ext = std::holds_alternative<GraphMaterial>(e.asset)           ? "lmat"
                      : std::holds_alternative<GraphMaterialInstance>(e.asset) ? "lmatinst"
                      : std::holds_alternative<MaterialFunction>(e.asset)      ? "lmatfunc"
                                                                               : "lmpc";
    auto file = m_paths.ToFile(path, {ext});
    if (!file) file = m_paths.MakeFilePath(path, ext);
    if (!file) {
        if (outError) *outError = "'" + path + "' is not under the project or engine content";
        return false;
    }
    std::error_code ec;
    fs::create_directories(file->parent_path(), ec);
    const bool ok = std::visit(
        [&](const auto& a) { return GraphAssetSerializer::Save(a, *file, outError); }, e.asset);
    if (ok) e.dirty = false;
    return ok;
}

bool MaterialLibrary::IsGraphAssetDirty(std::string_view path) const {
    auto it = m_graph.find(Normalize(path));
    return it != m_graph.end() && it->second.dirty;
}

u64 MaterialLibrary::GraphVersion(std::string_view path) const {
    auto it = m_graph.find(Normalize(path));
    return it == m_graph.end() ? 0 : it->second.version;
}

std::vector<std::string> MaterialLibrary::Dependents(std::string_view rawPath) const {
    const std::string target = Normalize(rawPath);
    auto graphUses = [&](const MaterialGraph::Graph& g) {
        for (const auto& n : g.Nodes()) {
            for (const char* key : {"function", "collection", "texture"}) {
                auto p = n.properties.find(key);
                if (p != n.properties.end() && Normalize(p->second.AsString()) == target) return true;
            }
        }
        return false;
    };
    std::vector<std::string> out;
    for (const auto& [path, e] : m_graph) {
        bool uses = false;
        if (auto* m = std::get_if<GraphMaterial>(&e.asset)) {
            uses = graphUses(m->graph);
        } else if (auto* fn = std::get_if<MaterialFunction>(&e.asset)) {
            uses = graphUses(fn->graph);
        } else if (auto* inst = std::get_if<GraphMaterialInstance>(&e.asset)) {
            uses = Normalize(inst->parent) == target;
            for (const auto& o : inst->overrides) {
                uses = uses || (!o.texture.empty() && Normalize(o.texture) == target);
            }
        }
        if (uses && path != target) out.push_back(path);
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace Luma::Material
