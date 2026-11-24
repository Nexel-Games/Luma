#pragma once

#include <string>

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

}  // namespace Luma::Editor::Panels
