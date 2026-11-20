#pragma once

#include <filesystem>
#include <string>

#include "Luma/Asset/ThumbnailRenderer.h"

namespace Luma {

/**
 * Thumbnail renderer for Material and Material Instance assets (.lmat,
 * .lmatinst): a preview sphere shaded by the real renderer. The pixels come
 * from the GPU render callback the editor installs on the ThumbnailManager
 * (ThumbnailManager::SetRenderCallback), so thumbnails match the viewport.
 */
class MaterialThumbnailRenderer : public ThumbnailRenderer {
public:
    AssetType GetAssetType() const override { return AssetType::Material; }

    bool CanRender(const AssetId& assetId, const std::filesystem::path& nativePath) const override;

    bool RenderThumbnail(const AssetId& assetId,
                         const std::filesystem::path& nativePath,
                         const std::filesystem::path& outputPath,
                         u32 width,
                         u32 height) override;

    std::string GetRendererName() const override { return "MaterialThumbnailRenderer"; }
};

}  // namespace Luma
