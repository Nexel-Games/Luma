#pragma once

#include <filesystem>
#include <string>

#include "Luma/Asset/ThumbnailRenderer.h"

namespace Luma {

/**
 * Thumbnail renderer for mesh assets (.lmesh): the mesh framed and shaded by
 * the real renderer with its materials. Pixels come from the GPU render
 * callback the editor installs on the ThumbnailManager.
 */
class MeshThumbnailRenderer : public ThumbnailRenderer {
public:
    AssetType GetAssetType() const override { return AssetType::Mesh; }

    bool CanRender(const AssetId& assetId, const std::filesystem::path& nativePath) const override;

    bool RenderThumbnail(const AssetId& assetId,
                         const std::filesystem::path& nativePath,
                         const std::filesystem::path& outputPath,
                         u32 width,
                         u32 height) override;

    std::string GetRendererName() const override { return "MeshThumbnailRenderer"; }
};

}  // namespace Luma
