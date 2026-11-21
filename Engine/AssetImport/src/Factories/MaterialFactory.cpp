#include "Luma/AssetImport/Factories/MaterialFactory.h"

#include "Luma/Material/MaterialLibrary.h"
#include "Luma/Material/MaterialSerializer.h"

namespace Luma::Import {
namespace {

FactoryResult Failed(std::string error) {
    FactoryResult result;
    result.error = std::move(error);
    return result;
}

std::string TextureVirtual(const FactoryContext& context, const AssetRequest& request, NodeId id) {
    if (id == kNone || id >= context.scene.nodes.size()) {
        return {};
    }
    const ImportNode& texture = context.scene.nodes[id];
    if (texture.kind != NodeKind::Texture || !texture.enabled || !texture.error.empty()) {
        return {};
    }
    return VirtualPathFor(context, PlannedAssetFile(context, request, texture));
}

void SetConstant(Material::MaterialInput& input, f32 value) {
    input.constant = {value, value, value, value};
}

}  // namespace

FactoryResult CreateMaterialAsset(const AssetRequest& request, const FactoryContext& context) {
    if (context.materials == nullptr) {
        return Failed("No material library is available");
    }
    if (request.sources.empty() || request.sources.front() >= context.scene.nodes.size()) {
        return Failed("Material request has no source node");
    }
    const ImportNode& node = context.scene.nodes[request.sources.front()];
    const MaterialDesc& desc = node.material;
    const std::string virtualPath = VirtualPathFor(context, request.destFile);
    if (virtualPath.empty()) {
        return Failed("'" + request.destFile.string() + "' is outside the project");
    }

    Material::Material material = Material::Material::MakeDefault();
    material.name = request.destFile.stem().string();
    material.settings.twoSided = desc.twoSided;
    if (desc.opacityMaskClip > 0.0f) {
        material.settings.blendMode = Material::BlendMode::Masked;
        material.settings.opacityMaskClip = desc.opacityMaskClip;
    }

    Material::MaterialInput& base = material.Input(Material::MaterialParameter::BaseColor);
    base.constant = desc.baseColor;
    base.texture = TextureVirtual(context, request, desc.baseColorTex.texture);

    SetConstant(material.Input(Material::MaterialParameter::Metallic), desc.metallic);
    SetConstant(material.Input(Material::MaterialParameter::Roughness), desc.roughness);
    SetConstant(material.Input(Material::MaterialParameter::Specular), desc.specular);

    material.Input(Material::MaterialParameter::Normal).texture =
        TextureVirtual(context, request, desc.normalTex.texture);
    material.Input(Material::MaterialParameter::EmissiveColor).constant = {desc.emissive.x, desc.emissive.y,
                                                                          desc.emissive.z, 1.0f};
    material.Input(Material::MaterialParameter::AmbientOcclusion).texture =
        TextureVirtual(context, request, desc.occlusionTex.texture);
    material.Input(Material::MaterialParameter::OpacityMask).texture =
        TextureVirtual(context, request, desc.opacityTex.texture);

    const std::string packed = TextureVirtual(context, request, desc.metallicRoughnessTex.texture);
    if (!packed.empty()) {
        Material::MaterialInput& metallic = material.Input(Material::MaterialParameter::Metallic);
        Material::MaterialInput& roughness = material.Input(Material::MaterialParameter::Roughness);
        metallic.texture = packed;
        metallic.channel = Material::TextureChannel::B;
        roughness.texture = packed;
        roughness.channel = Material::TextureChannel::G;
    }

    if (ImportCancelled(context)) {
        return Failed("Import was cancelled");
    }

    context.materials->SetMaterial(virtualPath, material);
    std::string error;
    const std::filesystem::path temp = ImportTempPath(request.destFile);
    std::error_code io;
    std::filesystem::create_directories(request.destFile.parent_path(), io);
    if (!Material::MaterialSerializer::SaveToFile(material, temp, &error)) {
        std::filesystem::remove(temp, io);
        return Failed(error.empty() ? "Could not write the material" : error);
    }
    if (ImportCancelled(context)) {
        std::filesystem::remove(temp, io);
        return Failed("Import was cancelled");
    }
    if (!ReplaceFileAtomic(temp, request.destFile, &error)) {
        return Failed(error);
    }
    if (!context.materials->Save(virtualPath, &error)) {
        return Failed(error.empty() ? "Could not save the material" : error);
    }
    return FinishAsset(context, request, request.destFile, &node, "Luma.Import.Material",
                       context.settings.ToJson());
}

}  // namespace Luma::Import
