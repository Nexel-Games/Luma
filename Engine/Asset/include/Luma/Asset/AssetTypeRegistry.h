#pragma once

#include <filesystem>
#include <functional>
#include <mutex>
#include "Luma/Asset/AssetType.h"
#include "Luma/Core/Registration.h"

namespace Luma {

class ThumbnailRenderer;
struct AssetOpenContext {
    std::function<void(const std::filesystem::path&)> openMaterial;
};

struct AssetTypeDesc {
    std::string id, displayName, icon, owner;
    std::vector<std::string> extensions;
    AssetType builtin = AssetType::Unknown;
    std::string nativeExtension = ".lasset";
    std::string importSection, importFolder;
    std::string dragType = "asset/file";
    std::function<void(const std::filesystem::path&, const AssetOpenContext&)> open;
    std::function<std::unique_ptr<ThumbnailRenderer>()> createThumbnailRenderer;
    AssetType thumbnailSource = AssetType::Unknown;
    bool useDefaultThumbnailPrimitive = false;
    // Write a thumbnail to the supplied cache path at the requested resolution.
    std::function<bool(const std::filesystem::path&, const std::filesystem::path&, u32, u32)> thumbnail;
};

class AssetTypeRegistry {
public:
    using Description = std::shared_ptr<const AssetTypeDesc>;
    RegistrationToken Register(AssetTypeDesc desc, std::string* error = nullptr);
    Description Find(std::string_view id) const;
    Description FindByExtension(std::string_view extension) const;
    Description FindBuiltin(AssetType type) const;
    std::vector<Description> All() const;
private:
    mutable std::mutex m_mutex;
    std::vector<Description> m_entries;
    std::shared_ptr<int> m_alive = std::make_shared<int>(0);
};

RegistrationSet RegisterBuiltinAssetTypes(AssetTypeRegistry& registry);
AssetTypeRegistry& AssetTypes();
AssetType AssetTypeFromName(std::string_view name);
std::string NormalizeAssetExtension(std::string_view extension);

}  // namespace Luma
