#pragma once

#include <array>
#include <string>
#include <string_view>

#include "Luma/Core/Types.h"

// Coarse asset type taxonomy. The mapping from file extension to type lives
// in AssetType.cpp so a new extension doesn't ripple through includes.

namespace Luma {

enum class AssetType : u8 {
    Unknown = 0,
    Texture,
    Mesh,
    Material,
    MaterialInstance,  // .lmatinst (parent material + overrides)
    Shader,
    Script,
    Prefab,
    Scene,
    Sound,
    Font,
    Folder,  // not a file, but the Content Browser shows directory rows
    Count,
};

const char* AssetTypeName(AssetType t) noexcept;
std::string_view AssetTypeSlug(AssetType t) noexcept;
AssetType AssetTypeFromExtension(std::string_view ext) noexcept;
std::string_view AssetTypeIconHint(AssetType t) noexcept;  // enum name from IconKind

}  // namespace Luma
