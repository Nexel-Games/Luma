#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "Luma/Asset/AssetPath.h"
#include "Luma/AssetImport/Pipelines/ImportPlan.h"
#include "Luma/Serialization/SerialValue.h"

namespace Luma::Material {
class MaterialLibrary;
}

namespace Luma::Import {

class ModelPayloadCache;

struct FactoryContext {
    const ImportScene& scene;
    const PipelineStack& settings;
    Material::MaterialLibrary* materials;
    const AssetPathResolver& paths;
    std::function<bool()> cancelled;
    // Parsed models shared by the mesh requests of one run (optional).
    ModelPayloadCache* modelCache = nullptr;
};

struct FactoryResult {
    bool ok = false;
    bool skipped = false;
    std::filesystem::path file;
    std::string error;
};

FactoryResult CreateAsset(const AssetRequest& request, const FactoryContext& context);

bool ImportCancelled(const FactoryContext& context);
std::filesystem::path ImportTempPath(const std::filesystem::path& dest);
bool WriteFileAtomic(const std::filesystem::path& dest, const void* data, std::size_t size, std::string* err);
bool ReplaceFileAtomic(const std::filesystem::path& from, const std::filesystem::path& to, std::string* err);

// The asset file is already at `file`. Writes `<file>.meta` last, reusing an existing GUID.
FactoryResult FinishAsset(const FactoryContext& context, const AssetRequest& request,
                          const std::filesystem::path& file, const ImportNode* node, std::string importer,
                          SerialValue settings);

std::string VirtualPathFor(const FactoryContext& context, const std::filesystem::path& file);
std::filesystem::path PlannedAssetFile(const FactoryContext& context, const AssetRequest& request,
                                       const ImportNode& node);

}  // namespace Luma::Import
