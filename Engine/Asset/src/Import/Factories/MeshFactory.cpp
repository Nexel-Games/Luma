#include "Luma/Asset/Import/Factories/MeshFactory.h"

#include "Luma/Asset/Import/Translators/ModelTranslator.h"
#include "Luma/Asset/LumaMesh.h"

namespace Luma::Import {
namespace {

const ImportNode* FindMaterial(const ImportScene& scene, const std::filesystem::path& source,
                               const std::string& name) {
    for (const ImportNode& node : scene.nodes) {
        if (node.kind == NodeKind::Material && node.name == name && node.sourceFile == source) {
            return &node;
        }
    }
    return nullptr;
}

ModelPayloadOptions OptionsFrom(const SettingsBlock& meshes) {
    ModelPayloadOptions options;
    options.uniformScale = static_cast<f32>(meshes.GetFloat("uniformScale"));
    options.offsetTranslation = meshes.GetVec3("offsetTranslation");
    options.offsetRotationDeg = meshes.GetVec3("offsetRotation");
    options.importNormals = meshes.GetBool("importNormals");
    options.smoothingAngleDeg = static_cast<f32>(meshes.GetFloat("smoothingAngle"));
    options.computeTangents = meshes.GetBool("computeTangents");
    options.flipUVs = meshes.GetBool("flipUVs");
    options.combineMeshes = meshes.GetBool("combineMeshes");
    const std::string axis = meshes.GetString("axis");
    if (axis == "Y Up") {
        options.axis = ModelPayloadOptions::AxisOverride::YUp;
    } else if (axis == "Z Up") {
        options.axis = ModelPayloadOptions::AxisOverride::ZUp;
    }
    const std::string pivot = meshes.GetString("pivot");
    if (pivot == "Bounds Center") {
        options.pivot = ModelPayloadOptions::Pivot::BoundsCenter;
    } else if (pivot == "Bounds Bottom") {
        options.pivot = ModelPayloadOptions::Pivot::BoundsBottom;
    }
    return options;
}

FactoryResult Failed(std::string error) {
    FactoryResult result;
    result.error = std::move(error);
    return result;
}

}  // namespace

FactoryResult CreateMeshAsset(const AssetRequest& request, const FactoryContext& context) {
    if (request.sources.empty() || request.sources.front() >= context.scene.nodes.size()) {
        return Failed("Mesh request has no source node");
    }
    const ImportNode& primary = context.scene.nodes[request.sources.front()];
    std::vector<std::string> names;
    names.reserve(request.sources.size());
    for (NodeId id : request.sources) {
        if (id >= context.scene.nodes.size()) {
            return Failed("Mesh request points at a missing node");
        }
        names.push_back(context.scene.nodes[id].name);
    }

    std::string error;
    ModelTranslator translator;
    std::vector<Luma::LumaMeshData> built =
        translator.BuildMeshes(primary.sourceFile, names, OptionsFrom(context.settings.Meshes()), &error);
    if (built.empty()) {
        return Failed(error.empty() ? "The mesh could not be built" : error);
    }

    Luma::LumaMeshData& mesh = built.front();
    for (Luma::LumaSubmesh& section : mesh.submeshes) {
        const ImportNode* material = FindMaterial(context.scene, primary.sourceFile, section.materialName);
        if (material != nullptr && material->enabled) {
            section.materialName = VirtualPathFor(context, PlannedAssetFile(context, request, *material));
        }
    }

    if (ImportCancelled(context)) {
        return Failed("Import was cancelled");
    }
    std::error_code io;
    const std::filesystem::path temp = ImportTempPath(request.destFile);
    std::filesystem::create_directories(request.destFile.parent_path(), io);
    if (!LumaMeshIO::WriteMesh(temp, mesh)) {
        std::filesystem::remove(temp, io);
        return Failed("Could not write '" + request.destFile.filename().string() + "'");
    }
    if (ImportCancelled(context)) {
        std::filesystem::remove(temp, io);
        return Failed("Import was cancelled");
    }
    if (!ReplaceFileAtomic(temp, request.destFile, &error)) {
        return Failed(error);
    }
    return FinishAsset(context, request, request.destFile, &primary, "Luma.Import.Model",
                       context.settings.Meshes().ToJson());
}

}  // namespace Luma::Import
