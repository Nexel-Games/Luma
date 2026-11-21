#pragma once

#include "Luma/AssetImport/Pipelines/ImportPlan.h"

namespace Luma::Import {

void DefineAudioPipeline(SettingsBlock& block);
void AppendSoundRequests(const ImportScene& scene, const SettingsBlock& common,
                         const std::filesystem::path& dest, std::vector<AssetRequest>& out);

}  // namespace Luma::Import
