#pragma once

#include "Luma/Asset/Import/Pipelines/ImportPlan.h"

namespace Luma::Import {

void DefineTexturePipeline(SettingsBlock& block);
void AppendTextureRequests(const ImportScene& scene, const SettingsBlock& common,
                           const SettingsBlock& materials, const SettingsBlock& textures,
                           const std::filesystem::path& dest, std::vector<AssetRequest>& out);

}  // namespace Luma::Import
