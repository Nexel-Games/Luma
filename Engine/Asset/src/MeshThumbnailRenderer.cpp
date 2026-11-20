#include "Luma/Asset/MeshThumbnailRenderer.h"

#include <algorithm>
#include <cctype>

namespace Luma {

bool MeshThumbnailRenderer::CanRender(const AssetId& /*assetId*/,
                                      const std::filesystem::path& nativePath) const {
    std::string ext = nativePath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".lmesh";
}

bool MeshThumbnailRenderer::RenderThumbnail(const AssetId& assetId,
                                            const std::filesystem::path& nativePath,
                                            const std::filesystem::path& outputPath,
                                            u32 width, u32 height) {
    if (!CanRender(assetId, nativePath)) return false;
    return ThumbnailManager::Get().RenderViaCallback(AssetType::Mesh, nativePath, outputPath,
                                                     width, height);
}

}  // namespace Luma
