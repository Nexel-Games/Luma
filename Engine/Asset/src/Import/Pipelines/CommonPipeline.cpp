#include "Luma/Asset/Import/Pipelines/CommonPipeline.h"

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

    add("subfolders", "Create Sub-folders (Meshes/Materials/Textures)", FieldType::Bool, false);
}

}  // namespace Luma::Import
