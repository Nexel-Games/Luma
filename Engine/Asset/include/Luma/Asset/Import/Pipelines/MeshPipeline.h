#pragma once

#include "Luma/Asset/Import/Pipelines/ImportPlan.h"

namespace Luma::Import {

void DefineMeshPipeline(SettingsBlock& block);
void AppendMeshRequests(const ImportScene& scene, const SettingsBlock& common, const SettingsBlock& meshes,
                        const std::filesystem::path& dest, std::vector<AssetRequest>& out);

}  // namespace Luma::Import
