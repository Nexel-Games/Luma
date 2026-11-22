#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Registration.h"

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

// Writes one native asset for a request. Factories are registered per asset
// type id; the built-in mesh / material / texture / sound factories register
// through the same API plugins use.
using AssetFactory = std::function<FactoryResult(const AssetRequest&, const FactoryContext&)>;

class AssetFactoryRegistry {
public:
    // Rejects an empty id or owner, a missing factory, and duplicate ids.
    RegistrationToken Register(std::string typeId, AssetFactory factory, std::string owner,
                               std::string* error = nullptr);
    // The factory for a type id (empty when none is registered).
    AssetFactory Find(std::string_view typeId) const;
    std::vector<std::string> TypeIds() const;

private:
    struct Entry {
        std::string id, owner;
        AssetFactory factory;
    };
    struct Entries {
        mutable std::mutex mutex;
        std::vector<Entry> items;
    };
    std::shared_ptr<Entries> m_entries = std::make_shared<Entries>();
};

RegistrationSet RegisterBuiltinAssetFactories(AssetFactoryRegistry& registry);
AssetFactoryRegistry& AssetFactories();

// Dispatches to the registered factory for the request's type.
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
