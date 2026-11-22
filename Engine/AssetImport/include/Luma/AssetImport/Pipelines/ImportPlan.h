#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Asset/AssetType.h"
#include "Luma/AssetImport/ImportScene.h"
#include "Luma/AssetImport/ImportSettings.h"
#include "Luma/Core/Types.h"

namespace Luma::Import {

enum class ConflictPolicy : u8 { Overwrite, Skip, KeepBoth };

struct AssetRequest {
    AssetType type = AssetType::Unknown;
    // Registered asset type id (AssetTypes()); empty = the built-in id of `type`.
    std::string typeId;
    std::filesystem::path destFile;
    std::vector<NodeId> sources;
    bool exists = false;
    ConflictPolicy policy = ConflictPolicy::KeepBoth;
};

struct ImportPlan {
    std::vector<AssetRequest> requests;
    std::vector<std::string> warnings;
};

bool EnabledMeshUses(const ImportScene& scene, NodeId material);
bool MaterialUsesTexture(const MaterialDesc& material, NodeId texture);
bool TextureIsStandalone(const ImportScene& scene, const ImportNode& texture);

// Letters, digits, and underscores. Other characters become '_', runs collapse,
// edges are trimmed, and an empty result is "Asset".
std::string SanitizeAssetName(std::string_view name);

// A user-typed sub-folder name made safe as one folder level: trimmed; path
// separators and characters Windows forbids become '_'; "." / ".." and
// leading dots are dropped. Empty when nothing usable is left.
std::string SubfolderName(std::string_view requested);

// The sub-folder for one asset type from the Common settings ("folderMeshes",
// "folderMaterials", "folderTextures", "folderSounds"); the default name
// ("Meshes", …) when the user's name is empty or unusable.
std::string SubfolderFor(const SettingsBlock& common, AssetType type);

// prefix + sanitized stem, unless the stem already begins with the prefix.
std::string PrefixedName(std::string_view prefix, std::string_view stem);

std::filesystem::path PlannedFile(const std::filesystem::path& destFolder, bool subfolders,
                                  std::string_view subfolder, std::string_view name,
                                  std::string_view extension);

class PipelineStack {
public:
    PipelineStack();

    SettingsBlock& Common();
    SettingsBlock& Meshes();
    SettingsBlock& Materials();
    SettingsBlock& Textures();
    SettingsBlock& Audio();
    const SettingsBlock& Common() const;
    const SettingsBlock& Meshes() const;
    const SettingsBlock& Materials() const;
    const SettingsBlock& Textures() const;
    const SettingsBlock& Audio() const;
    std::vector<SettingsBlock*> Blocks();

    ImportPlan BuildPlan(const ImportScene& scene, const std::filesystem::path& destFolder) const;
    SerialValue ToJson() const;
    void FromJson(const SerialValue& json);

private:
    SettingsBlock m_common;
    SettingsBlock m_meshes;
    SettingsBlock m_materials;
    SettingsBlock m_textures;
    SettingsBlock m_audio;
};

}  // namespace Luma::Import
