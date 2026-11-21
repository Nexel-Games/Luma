#pragma once

#include "Luma/AssetImport/Pipelines/ImportPlan.h"

namespace Luma::Import {

void DefineMaterialPipeline(SettingsBlock& block);
void AppendMaterialRequests(const ImportScene& scene, const SettingsBlock& common,
                            const SettingsBlock& materials, const std::filesystem::path& dest,
                            std::vector<AssetRequest>& out, std::vector<std::string>& warnings);

}  // namespace Luma::Import
