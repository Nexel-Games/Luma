#include "Luma/Asset/AssetTypeRegistry.h"
#include "Luma/Core/Log.h"
#include "Luma/Asset/MaterialThumbnailRenderer.h"
#include "Luma/Asset/MeshThumbnailRenderer.h"
#include "Luma/Asset/SoundThumbnailRenderer.h"
#include "Luma/Asset/TextureThumbnailRenderer.h"
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
    auto add = [&](AssetTypeDesc desc) { tokens.Add(registry.Register(std::move(desc))); };
    auto materialEditor = [](const std::filesystem::path& path, const AssetOpenContext& ctx) {
        if (ctx.openMaterial) ctx.openMaterial(path);
    };
    add({"unknown", "Unknown", "File", "Engine", {}, AssetType::Unknown});
    AssetTypeDesc mesh{"mesh", "Mesh", "StaticMesh", "Engine", {"lmesh"}, AssetType::Mesh, ".lmesh", "Meshes", "Meshes"};
    mesh.dragType = "asset/mesh";
    mesh.createThumbnailRenderer = [] { return std::make_unique<MeshThumbnailRenderer>(); };
    add(std::move(mesh));
    AssetTypeDesc texture{"texture", "Texture", "Image", "Engine",
                          {"png", "jpg", "jpeg", "tga", "bmp", "webp", "ktx", "ktx2", "ltex", "psd", "hdr", "exr"},
                          AssetType::Texture, ".ltex", "Textures", "Textures"};
    texture.dragType = "asset/texture";
    texture.createThumbnailRenderer = [] { return std::make_unique<TextureThumbnailRenderer>(); };
    add(std::move(texture));
    AssetTypeDesc material{"material", "Material", "Sphere", "Engine", {"mat", "lumat", "lmat"},
                           AssetType::Material, ".lmat", "Materials", "Materials"};
    material.dragType = "asset/material";
    material.open = materialEditor;
    material.useDefaultThumbnailPrimitive = true;
    material.createThumbnailRenderer = [] { return std::make_unique<MaterialThumbnailRenderer>(); };
    add(std::move(material));
    AssetTypeDesc instance{"material-instance", "MaterialInstance", "MaterialInstance", "Engine", {"lmatinst"}, AssetType::MaterialInstance};
    instance.dragType = "asset/material";
    instance.open = materialEditor;
    instance.thumbnailSource = AssetType::Material;
    add(std::move(instance));
    add({"shader", "Shader", "Shader", "Engine", {"hlsl", "glsl", "frag", "vert", "lshader"}, AssetType::Shader, ".lshader"});
    add({"script", "Script", "Script", "Engine", {"lua", "py", "js", "cs"}, AssetType::Script});
    add({"prefab", "Prefab", "Cube", "Engine", {"prefab", "lumapfb", "lasset"}, AssetType::Prefab});
    add({"scene", "Scene", "Scene", "Engine", {"luma", "scene", "lumascene", "lscene"}, AssetType::Scene});
    AssetTypeDesc sound{"sound", "Sound", "Sound", "Engine", {"wav", "ogg", "mp3", "flac", "laudio"},
                        AssetType::Sound, ".laudio", "Audio", "Sounds"};
    sound.createThumbnailRenderer = [] { return std::make_unique<SoundThumbnailRenderer>(); };
    add(std::move(sound));
    add({"font", "Font", "Font", "Engine", {"ttf", "otf"}, AssetType::Font});
    AssetTypeDesc folder{"folder", "Folder", "Folder", "Engine", {}, AssetType::Folder};
    folder.dragType = "asset/folder";
    add(std::move(folder));
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
