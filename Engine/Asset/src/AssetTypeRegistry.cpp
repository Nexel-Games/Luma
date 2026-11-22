#include "Luma/Asset/AssetTypeRegistry.h"
#include "Luma/Core/Log.h"
#include <algorithm>
#include <cctype>
#include <set>

namespace Luma {

std::string NormalizeAssetExtension(std::string_view extension) {
    if (extension.starts_with('.')) extension.remove_prefix(1);
    std::string result(extension);
    for (char& c : result) {
        if (!std::isalnum(static_cast<unsigned char>(c))) return {};
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

RegistrationToken AssetTypeRegistry::Register(AssetTypeDesc desc, std::string* error) {
    auto reject = [&](std::string why) {
        why = "Asset type '" + desc.id + "': " + why;
        if (error) *error = why;
        LUMA_LOG_ERROR("Asset", "{}", why);
        return RegistrationToken{};
    };
    if (desc.id.empty() || desc.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") != std::string::npos)
        return reject("invalid id");
    if (desc.owner.empty() || desc.displayName.empty()) return reject("missing owner or display name");
    if (desc.builtin >= AssetType::Count) return reject("invalid built-in type");
    std::set<std::string> extensions;
    for (auto& ext : desc.extensions) {
        ext = NormalizeAssetExtension(ext);
        if (ext.empty() || !extensions.insert(ext).second) return reject("invalid or duplicate extension");
    }
    if (!desc.nativeExtension.starts_with('.') || NormalizeAssetExtension(desc.nativeExtension).empty())
        return reject("invalid native extension");
    std::lock_guard lock(m_mutex);
    for (const auto& entry : m_entries) {
        if (entry->id == desc.id || (desc.builtin != AssetType::Unknown && entry->builtin == desc.builtin))
            return reject("type already registered by " + entry->owner);
        for (const auto& ext : entry->extensions)
            if (extensions.contains(ext)) return reject("extension '" + ext + "' belongs to " + entry->id);
    }
    auto entry = std::make_shared<const AssetTypeDesc>(std::move(desc));
    m_entries.push_back(entry);
    return RegistrationToken(m_alive, [this, id = entry->id] {
        std::lock_guard guard(m_mutex);
        std::erase_if(m_entries, [&](const auto& item) { return item->id == id; });
    });
}

AssetTypeRegistry::Description AssetTypeRegistry::Find(std::string_view id) const {
    std::lock_guard lock(m_mutex);
    for (const auto& entry : m_entries) if (entry->id == id) return entry;
    return {};
}

AssetTypeRegistry::Description AssetTypeRegistry::FindBuiltin(AssetType type) const {
    if (type == AssetType::Unknown) return Find("unknown");
    std::lock_guard lock(m_mutex);
    for (const auto& entry : m_entries) if (entry->builtin == type) return entry;
    return {};
}

AssetTypeRegistry::Description AssetTypeRegistry::FindByExtension(std::string_view extension) const {
    const auto key = NormalizeAssetExtension(extension);
    std::lock_guard lock(m_mutex);
    for (const auto& entry : m_entries)
        if (std::find(entry->extensions.begin(), entry->extensions.end(), key) != entry->extensions.end()) return entry;
    return {};
}

std::vector<AssetTypeRegistry::Description> AssetTypeRegistry::All() const {
    std::lock_guard lock(m_mutex);
    return m_entries;
}

RegistrationSet RegisterBuiltinAssetTypes(AssetTypeRegistry& registry) {
    RegistrationSet tokens;
    auto add = [&](AssetType type, std::string id, std::string label, std::string icon,
                   std::vector<std::string> extensions, std::string native = ".lasset",
                   std::string section = {}, std::string folder = {}) {
        AssetTypeDesc desc{std::move(id), std::move(label), std::move(icon), "Engine", std::move(extensions)};
        desc.builtin = type;
        desc.nativeExtension = std::move(native);
        desc.importSection = std::move(section);
        desc.importFolder = std::move(folder);
        tokens.Add(registry.Register(std::move(desc)));
    };
    add(AssetType::Unknown, "unknown", "Unknown", "Dot", {});
    add(AssetType::Mesh, "mesh", "Mesh", "StaticMesh", {"lmesh"}, ".lmesh", "Meshes", "Meshes");
    add(AssetType::Texture, "texture", "Texture", "Image", {"png","jpg","jpeg","tga","bmp","webp","ktx","ktx2","ltex","psd","hdr","exr"}, ".ltex", "Textures", "Textures");
    add(AssetType::Material, "material", "Material", "Sphere", {"mat","lumat","lmat"}, ".lmat", "Materials", "Materials");
    add(AssetType::MaterialInstance, "material-instance", "MaterialInstance", "Sphere", {"lmatinst"});
    add(AssetType::Shader, "shader", "Shader", "Plane", {"hlsl","glsl","frag","vert","lshader"}, ".lshader");
    add(AssetType::Script, "script", "Script", "Cylinder", {"lua","py","js","cs"});
    add(AssetType::Prefab, "prefab", "Prefab", "Cube", {"prefab","lumapfb","lasset"});
    add(AssetType::Scene, "scene", "Scene", "Plane", {"luma","scene","lumascene","lscene"});
    add(AssetType::Sound, "sound", "Sound", "Play", {"wav","ogg","mp3","flac","laudio"}, ".laudio", "Audio", "Sounds");
    add(AssetType::Font, "font", "Font", "Refresh", {"ttf","otf"});
    add(AssetType::Folder, "folder", "Folder", "Folder", {});
    return tokens;
}

AssetTypeRegistry& AssetTypes() {
    static AssetTypeRegistry registry;
    static RegistrationSet builtins = RegisterBuiltinAssetTypes(registry);
    return registry;
}

AssetType AssetTypeFromName(std::string_view name) {
    if (name == "Audio") name = "Sound";  // legacy sidecars
    for (const auto& entry : AssetTypes().All())
        if (entry->displayName == name || entry->id == name) return entry->builtin;
    return AssetType::Unknown;
}

}  // namespace Luma
