#pragma once

#include <filesystem>
#include <string>

#include "Luma/Asset/AssetId.h"
#include "Luma/Asset/AssetType.h"
#include "Luma/AssetImport/ImportMetadata.h"
#include "Luma/Core/Types.h"

// The Inspector's asset view: shown when the most recent click was on an
// asset in the Content Browser (Unity-style). A common header (thumbnail,
// type, path, size, date, id; Open / Browse / Reimport) and a section per
// asset type (material settings and parameters, instance parent and
// overrides, ...). Drawn by InspectorPanel::DrawAsset.

namespace Luma::Editor::Panels {

// "900 bytes", "1.5 KB", "5.0 MB", "3.0 GB".
std::string FormatAssetSize(u64 bytes);
// Local "YYYY-MM-DD HH:MM" of a Windows FILETIME (AssetData::mtime); "Unknown" for 0.
std::string FormatAssetTime(i64 filetime);

// Rewrites `section`'s default material in a .lmesh (geometry untouched; the
// file watcher reloads it). False + `error` for a bad file or section.
bool SetMeshSectionMaterial(const std::filesystem::path& meshFile, u32 section, const std::string& material,
                            std::string* error);
// Stores a placeable asset's placement transform in its .meta, creating a
// minimal .meta (id + type) when the asset has none; other fields are kept.
bool SetAssetPlacement(const std::filesystem::path& assetFile, const AssetId& id, AssetType type,
                       const Import::PlacementTransform& placement, std::string* error);

}  // namespace Luma::Editor::Panels
