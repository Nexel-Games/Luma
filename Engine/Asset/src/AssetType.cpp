#include "Luma/Asset/AssetTypeRegistry.h"

namespace Luma {

const char* AssetTypeName(AssetType type) noexcept {
    const auto desc = AssetTypes().FindBuiltin(type);
    return desc ? desc->displayName.c_str() : "?";
}
std::string_view AssetTypeSlug(AssetType type) noexcept {
    const auto desc = AssetTypes().FindBuiltin(type);
    return desc ? std::string_view(desc->id) : "?";
}
AssetType AssetTypeFromExtension(std::string_view extension) noexcept {
    const auto desc = AssetTypes().FindByExtension(extension);
    return desc ? desc->builtin : AssetType::Unknown;
}
std::string_view AssetTypeIconHint(AssetType type) noexcept {
    const auto desc = AssetTypes().FindBuiltin(type);
    return desc ? std::string_view(desc->icon) : "Dot";
}

}  // namespace Luma
