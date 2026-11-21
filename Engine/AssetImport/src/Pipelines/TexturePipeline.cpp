#include "Luma/AssetImport/Pipelines/TexturePipeline.h"

namespace Luma::Import {
namespace {

std::string TextureExtension(const ImportNode& node) {
    std::string ext = node.textureEmbedded.empty() ? node.textureFile.extension().string() : node.textureEmbeddedExt;
    if (!ext.empty() && ext.front() != '.') {
        ext.insert(ext.begin(), '.');
    }
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    if (ext.empty() || ext == ".psd") {
        return ".png";
    }
    return ext;
}

bool ImportedMaterialUses(const ImportScene& scene, NodeId texture, std::string_view mode) {
    if (mode == "Do Not Import") {
        return false;
    }
    for (NodeId id = 0; id < scene.nodes.size(); ++id) {
        const ImportNode& material = scene.nodes[id];
        if (material.kind != NodeKind::Material || !material.enabled) {
            continue;
        }
        if (!EnabledMeshUses(scene, id) || !MaterialUsesTexture(material.material, texture)) {
            continue;
        }
        return true;
    }
    return false;
}

}  // namespace

void DefineTexturePipeline(SettingsBlock& block) {
    auto add = [&](SettingField field, SerialValue value) {
        field.category = "Textures";
        block.Define(std::move(field), std::move(value));
    };

    SettingField importTextures;
    importTextures.key = "importTextures";
    importTextures.label = "Import Textures";
    importTextures.type = FieldType::Bool;
    add(std::move(importTextures), true);

    SettingField colorSpace;
    colorSpace.key = "colorSpace";
    colorSpace.label = "Color Space";
    colorSpace.type = FieldType::Enum;
    colorSpace.enumValues = {"Auto", "sRGB", "Linear"};
    add(std::move(colorSpace), "Auto");

    SettingField mips;
    mips.key = "generateMips";
    mips.label = "Generate Mips";
    mips.type = FieldType::Bool;
    add(std::move(mips), true);

    SettingField maxSize;
    maxSize.key = "maxSize";
    maxSize.label = "Max Size";
    maxSize.type = FieldType::Enum;
    maxSize.enumValues = {"None", "256", "512", "1024", "2048", "4096", "8192"};
    add(std::move(maxSize), "None");

    SettingField filter;
    filter.key = "filter";
    filter.label = "Filter";
    filter.type = FieldType::Enum;
    filter.enumValues = {"Linear", "Nearest"};
    add(std::move(filter), "Linear");

    SettingField address;
    address.key = "address";
    address.label = "Address";
    address.type = FieldType::Enum;
    address.enumValues = {"Wrap", "Clamp", "Mirror"};
    add(std::move(address), "Wrap");
}

void AppendTextureRequests(const ImportScene& scene, const SettingsBlock& common,
                           const SettingsBlock& materials, const SettingsBlock& textures,
                           const std::filesystem::path& dest, std::vector<AssetRequest>& out,
                           std::vector<std::string>& warnings) {
    if (!textures.GetBool("importTextures")) {
        return;
    }
    const std::string mode = materials.GetString("materialMode");
    const bool subfolders = common.GetBool("subfolders");
    const std::string prefix = common.GetString("prefixTexture");

    for (NodeId id = 0; id < scene.nodes.size(); ++id) {
        const ImportNode& node = scene.nodes[id];
        if (node.kind != NodeKind::Texture || !node.enabled) {
            continue;
        }
        const bool standalone = TextureIsStandalone(scene, node);
        if (!standalone && !ImportedMaterialUses(scene, id, mode)) {
            continue;
        }
        // A texture the translator could not find (or read) produces no asset;
        // materials that use it are created without it.
        if (!node.error.empty()) {
            warnings.push_back(node.error);
            continue;
        }
        AssetRequest request;
        request.type = AssetType::Texture;
        request.sources = {id};
        request.destFile =
            PlannedFile(dest, subfolders, "Textures", PrefixedName(prefix, node.name), TextureExtension(node));
        out.push_back(std::move(request));
    }
}

}  // namespace Luma::Import
