#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "Luma/Asset/AssetId.h"
#include "Luma/Asset/AssetType.h"
#include "Luma/Core/Types.h"
#include "Luma/Serialization/SerialValue.h"

namespace Luma::Import {

struct ImportMetadata {
    AssetId guid;
    AssetType type = AssetType::Unknown;
    std::string sourceFile;
    std::string sourceHash;
    i64 sourceMtime = 0;
    i64 sourceSize = -1;  // bytes; -1 = unknown (written before sizes were stored)
    std::string importer;
    u32 importerVersion = 1;
    std::string sourceNodeName;
    SerialValue settings;
};

bool WriteMetadata(const std::filesystem::path& assetFile, const ImportMetadata& meta,
                   std::string* err);
std::optional<ImportMetadata> ReadMetadata(const std::filesystem::path& assetFile);
std::filesystem::path MetadataPathFor(const std::filesystem::path& assetFile);
std::string HashFile(const std::filesystem::path& file);

}  // namespace Luma::Import
