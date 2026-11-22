#include "Luma/AssetImport/Pipelines/MaterialPipeline.h"

namespace Luma::Import {

void DefineMaterialPipeline(SettingsBlock& block) {
    SettingField mode;
    mode.key = "materialMode";
    mode.label = "Material Mode";
    mode.category = "Materials";
    mode.type = FieldType::Enum;
    mode.enumValues = {"Create New", "Use Existing", "Do Not Import"};
    block.Define(std::move(mode), "Create New");
}

void AppendMaterialRequests(const ImportScene& scene, const SettingsBlock& common,
                            const SettingsBlock& materials, const std::filesystem::path& dest,
                            std::vector<AssetRequest>& out, std::vector<std::string>& warnings) {
    const std::string mode = materials.GetString("materialMode");
    if (mode == "Do Not Import") {
        return;
    }
    const bool subfolders = common.GetBool("subfolders");
    const std::string prefix = common.GetString("prefixMaterial");

    for (NodeId id = 0; id < scene.nodes.size(); ++id) {
        const ImportNode& node = scene.nodes[id];
        if (node.kind != NodeKind::Material || !node.enabled) {
            continue;
        }
        if (!EnabledMeshUses(scene, id)) {
            continue;
        }
        AssetRequest request;
        request.type = AssetType::Material;
        request.sources = {id};
        const std::string name = PrefixedName(prefix, node.name);
        request.destFile = PlannedFile(dest, subfolders, SubfolderFor(common, AssetType::Material), name, ".lmat");
        if (mode == "Use Existing" && !std::filesystem::exists(request.destFile)) {
            warnings.push_back("Material '" + name + "' was not found; a new material will be created.");
        }
        out.push_back(std::move(request));
    }
}

}  // namespace Luma::Import
