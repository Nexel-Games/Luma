#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "Luma/Asset/AssetId.h"
#include "Luma/Asset/AssetType.h"
#include "Luma/Core/Types.h"
#include "Luma/Math/Math.h"
#include "Luma/Serialization/SerialValue.h"

namespace Luma::Import {

// How a placeable asset (mesh, prefab) lands in a level: applied on top of
// the drop point (position offset, rotation in degrees, scale). Edited in the
// asset Inspector; identity = placed as authored.
struct PlacementTransform {
    Math::Vec3 position{0.0f, 0.0f, 0.0f};
    Math::Vec3 rotation{0.0f, 0.0f, 0.0f};
    Math::Vec3 scale{1.0f, 1.0f, 1.0f};
    bool IsIdentity() const {
        return position.x == 0.0f && position.y == 0.0f && position.z == 0.0f && rotation.x == 0.0f &&
               rotation.y == 0.0f && rotation.z == 0.0f && scale.x == 1.0f && scale.y == 1.0f && scale.z == 1.0f;
    }
};

// "placement" object of a .meta (absent = identity).
SerialValue WritePlacement(const PlacementTransform& placement);
PlacementTransform ReadPlacement(const SerialValue* placement);

struct ImportMetadata {
    AssetId guid;
    AssetType type = AssetType::Unknown;
    std::string typeId;  // stable type id for custom or unavailable plugins
    std::string sourceFile;
    std::string sourceHash;
    i64 sourceMtime = 0;
    i64 sourceSize = -1;  // bytes; -1 = unknown (written before sizes were stored)
    std::string importer;
    u32 importerVersion = 1;
    std::string sourceNodeName;
    SerialValue settings;
    PlacementTransform placement;  // kept across reimports
};

bool WriteMetadata(const std::filesystem::path& assetFile, const ImportMetadata& meta,
                   std::string* err);
std::optional<ImportMetadata> ReadMetadata(const std::filesystem::path& assetFile);
std::filesystem::path MetadataPathFor(const std::filesystem::path& assetFile);
std::string HashFile(const std::filesystem::path& file);

// Renames/moves an asset file and its .meta sidecar (if any). Fails without
// changing anything when the target exists or the file cannot be renamed;
// a sidecar that cannot follow leaves the asset renamed and sets `warning`.
bool RenameAssetFile(const std::filesystem::path& from, const std::filesystem::path& to,
                     std::string* errorOrWarning);

// Moves a file or folder into `folder`, keeping its name and any .meta sidecar.
// False with an empty error means it is already in that folder. False with an
// error means nothing was moved (the destination exists, or a folder would be
// moved into itself).
bool MoveAssetToFolder(const std::filesystem::path& from, const std::filesystem::path& folder,
                       std::filesystem::path* movedTo, std::string* error);

}  // namespace Luma::Import
