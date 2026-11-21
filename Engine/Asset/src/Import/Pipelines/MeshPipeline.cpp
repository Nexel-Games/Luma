#include "Luma/Asset/Import/Pipelines/MeshPipeline.h"

namespace Luma::Import {
namespace {

SerialValue Vec3Zero() {
    SerialValue value = SerialValue::MakeArray();
    value.PushBack(0.0);
    value.PushBack(0.0);
    value.PushBack(0.0);
    return value;
}

}  // namespace

void DefineMeshPipeline(SettingsBlock& block) {
    auto add = [&](SettingField field, SerialValue value) {
        field.category = "Meshes";
        block.Define(std::move(field), std::move(value));
    };

    SettingField importMeshes;
    importMeshes.key = "importMeshes";
    importMeshes.label = "Import Meshes";
    importMeshes.type = FieldType::Bool;
    add(std::move(importMeshes), true);

    SettingField combine;
    combine.key = "combineMeshes";
    combine.label = "Combine Meshes";
    combine.type = FieldType::Bool;
    add(std::move(combine), false);

    SettingField scale;
    scale.key = "uniformScale";
    scale.label = "Uniform Scale";
    scale.type = FieldType::Float;
    scale.min = 0.0001;
    scale.max = 1000.0;
    add(std::move(scale), 1.0);

    SettingField translation;
    translation.key = "offsetTranslation";
    translation.label = "Offset Translation";
    translation.type = FieldType::Vec3;
    add(std::move(translation), Vec3Zero());

    SettingField rotation;
    rotation.key = "offsetRotation";
    rotation.label = "Offset Rotation";
    rotation.type = FieldType::Vec3;
    add(std::move(rotation), Vec3Zero());

    SettingField axis;
    axis.key = "axis";
    axis.label = "Axis";
    axis.type = FieldType::Enum;
    axis.enumValues = {"Auto", "Y Up", "Z Up"};
    add(std::move(axis), "Auto");

    SettingField normals;
    normals.key = "importNormals";
    normals.label = "Import Normals";
    normals.type = FieldType::Bool;
    add(std::move(normals), true);

    SettingField smoothing;
    smoothing.key = "smoothingAngle";
    smoothing.label = "Smoothing Angle";
    smoothing.type = FieldType::Float;
    smoothing.min = 0.0;
    smoothing.max = 180.0;
    smoothing.visibleIfKey = "!importNormals";
    add(std::move(smoothing), 66.0);

    SettingField tangents;
    tangents.key = "computeTangents";
    tangents.label = "Compute Tangents";
    tangents.type = FieldType::Bool;
    add(std::move(tangents), true);

    SettingField flip;
    flip.key = "flipUVs";
    flip.label = "Flip UVs";
    flip.type = FieldType::Bool;
    add(std::move(flip), false);

    SettingField pivot;
    pivot.key = "pivot";
    pivot.label = "Pivot";
    pivot.type = FieldType::Enum;
    pivot.enumValues = {"Source", "Bounds Center", "Bounds Bottom"};
    add(std::move(pivot), "Source");
}

void AppendMeshRequests(const ImportScene& scene, const SettingsBlock& common, const SettingsBlock& meshes,
                        const std::filesystem::path& dest, std::vector<AssetRequest>& out) {
    if (!meshes.GetBool("importMeshes")) {
        return;
    }
    const bool subfolders = common.GetBool("subfolders");
    const std::string prefix = common.GetString("prefixMesh");
    const bool combine = meshes.GetBool("combineMeshes");

    std::vector<std::string> files;
    std::vector<std::vector<NodeId>> groups;
    auto groupFor = [&](const std::filesystem::path& source) -> std::vector<NodeId>& {
        const std::string key = source.generic_string();
        for (size_t i = 0; i < files.size(); ++i) {
            if (files[i] == key) {
                return groups[i];
            }
        }
        files.push_back(key);
        groups.emplace_back();
        return groups.back();
    };

    for (NodeId id = 0; id < scene.nodes.size(); ++id) {
        const ImportNode& node = scene.nodes[id];
        if (node.kind != NodeKind::Mesh || !node.enabled) {
            continue;
        }
        if (!combine) {
            AssetRequest request;
            request.type = AssetType::Mesh;
            request.sources = {id};
            const std::string name = PrefixedName(prefix, node.name);
            request.destFile = PlannedFile(dest, subfolders, "Meshes", name, ".lmesh");
            out.push_back(std::move(request));
            continue;
        }
        groupFor(node.sourceFile).push_back(id);
    }

    if (!combine) {
        return;
    }
    for (size_t i = 0; i < groups.size(); ++i) {
        if (groups[i].empty()) {
            continue;
        }
        std::filesystem::path source(files[i]);
        std::string stem = source.stem().string();
        if (stem.empty()) {
            stem = scene.nodes[groups[i].front()].name;
        }
        AssetRequest request;
        request.type = AssetType::Mesh;
        request.sources = std::move(groups[i]);
        request.destFile = PlannedFile(dest, subfolders, "Meshes", PrefixedName(prefix, stem), ".lmesh");
        out.push_back(std::move(request));
    }
}

}  // namespace Luma::Import
