#pragma once

#include "Luma/Asset/ThumbnailRenderer.h"

namespace Luma {

// 128x128 waveform. Background #262626, centre line #3A3A3A, samples #2D9CDB,
// inset by 4 pixels. Duration is not drawn; the content tile shows it.
class SoundThumbnailRenderer : public ThumbnailRenderer {
public:
    AssetType GetAssetType() const override { return AssetType::Sound; }
    bool CanRender(const AssetId& assetId, const std::filesystem::path& nativePath) const override;
    bool RenderThumbnail(const AssetId& assetId, const std::filesystem::path& nativePath,
                         const std::filesystem::path& outputPath, u32 width, u32 height) override;
    std::string GetRendererName() const override { return "SoundThumbnailRenderer"; }
};

}  // namespace Luma
