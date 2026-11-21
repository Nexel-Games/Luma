#include "Luma/AssetImport/Pipelines/CommonPipeline.h"

namespace Luma::Import {

void DefineCommonPipeline(SettingsBlock& block) {
    auto add = [&](std::string key, std::string label, FieldType type, SerialValue value) {
        SettingField field;
        field.key = std::move(key);
        field.label = std::move(label);
        field.category = "Common";
        field.type = type;
        block.Define(std::move(field), std::move(value));
    };

    add("prefixMesh", "Mesh Prefix", FieldType::String, "SM_");
    add("prefixMaterial", "Material Prefix", FieldType::String, "M_");
    add("prefixTexture", "Texture Prefix", FieldType::String, "T_");
    add("prefixSound", "Sound Prefix", FieldType::String, "S_");

    SettingField conflict;
    conflict.key = "conflict";
    conflict.label = "If Asset Exists";
    conflict.category = "Common";
    conflict.type = FieldType::Enum;
    conflict.enumValues = {"Overwrite", "Skip", "Keep Both"};
    block.Define(std::move(conflict), "Keep Both");

    SettingField subfolders;
    subfolders.key = "subfolders";
    subfolders.label = "Create Sub-folders";
    subfolders.tooltip = "Put imported assets in Meshes/, Materials/, Textures/ and Sounds/ sub-folders.";
    subfolders.category = "Common";
    subfolders.type = FieldType::Bool;
    block.Define(std::move(subfolders), false);

    // Names of those sub-folders, shown while "Create Sub-folders" is on.
    const struct {
        const char* key;
        const char* label;
        const char* value;
    } folders[] = {{"folderMeshes", "Meshes Folder", "Meshes"},
                   {"folderMaterials", "Materials Folder", "Materials"},
                   {"folderTextures", "Textures Folder", "Textures"},
                   {"folderSounds", "Sounds Folder", "Sounds"}};
    for (const auto& folder : folders) {
        SettingField field;
        field.key = folder.key;
        field.label = folder.label;
        field.tooltip = "Name of the sub-folder (inside the destination) for these assets.";
        field.category = "Common";
        field.type = FieldType::String;
        field.visibleIfKey = "subfolders";
        block.Define(std::move(field), std::string(folder.value));
    }
}

}  // namespace Luma::Import
