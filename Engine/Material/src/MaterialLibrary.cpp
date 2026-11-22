#include "Luma/Asset/AssetTypeRegistry.h"
#include "Luma/Material/MaterialLibrary.h"

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

}  // namespace Luma::Material
