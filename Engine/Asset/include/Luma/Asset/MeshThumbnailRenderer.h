#pragma once

#include "Luma/Asset/ThumbnailRenderer.h"
#include "Luma/Asset/LumaMesh.h"
#include "Luma/Core/Types.h"
#include "Luma/Math/Math.h"

#include <filesystem>
#include <vector>

namespace Luma {

struct TexturePreviewData {
    std::vector<u8> pixels; // RGBA8
    u32 width = 0;
    u32 height = 0;
    bool IsValid() const { return !pixels.empty() && width > 0 && height > 0; }
};

/**
 * Thumbnail renderer for mesh assets
 * Renders .lmesh files as thumbnails using wireframe or solid preview
 * (CPU rasterized preview).
 */
class MeshThumbnailRenderer : public ThumbnailRenderer {
public:
    MeshThumbnailRenderer();
    ~MeshThumbnailRenderer() override;
    
    // ThumbnailRenderer interface
    AssetType GetAssetType() const override { return AssetType::Mesh; }
    
    bool CanRender(const AssetId& assetId, const std::filesystem::path& nativePath) const override;
    
    bool RenderThumbnail(const AssetId& assetId,
                        const std::filesystem::path& nativePath,
                        const std::filesystem::path& outputPath,
                        u32 width,
                        u32 height) override;
    
    void GetPreferredSize(u32& outWidth, u32& outHeight) const override;
    
    std::string GetRendererName() const override { return "MeshThumbnailRenderer"; }
    
private:
    /**
     * Load mesh and optional texture from file
     */
    bool LoadMesh(const std::filesystem::path& path, LumaMeshData& outMesh, TexturePreviewData& outTexture);
    bool LoadMesh(const std::filesystem::path& path, LumaMeshData& outMesh) {
        TexturePreviewData dummy;
        return LoadMesh(path, outMesh, dummy);
    }

    /**
     * Load an image file into RGBA8 buffer
     */
    bool LoadTextureFromFile(const std::filesystem::path& path, TexturePreviewData& outTexture);

    /**
     * Locate and load the most suitable diffuse/albedo texture for a mesh
     */
    bool FindAndLoadTexture(const std::filesystem::path& meshPath,
                           const void* assimpScene,
                           TexturePreviewData& outTexture);
    
    /**
     * Render mesh as wireframe thumbnail (software fallback)
     */
    bool RenderWireframe(const LumaMeshData& mesh,
                        std::vector<u8>& outPixels,
                        u32 width,
                        u32 height);
    
    /**
     * Render mesh as solid thumbnail with texture and lighting
     */
    bool RenderSolid(const LumaMeshData& mesh,
                    const TexturePreviewData* texture,
                    std::vector<u8>& outPixels,
                    u32 width,
                    u32 height);
    
    /**
     * Transform mesh vertices to camera space
     */
    void TransformMesh(const LumaMeshData& mesh,
                      std::vector<Math::Vec3>& outVertices,
                      const Math::Mat4& viewMatrix,
                      const Math::Mat4& projMatrix);
    
    /**
     * Clear background to a color
     */
    void ClearBackground(std::vector<u8>& pixels, u32 width, u32 height, u8 r, u8 g, u8 b);
    
    /**
     * Draw a line in pixel space
     */
    void DrawLine(std::vector<u8>& pixels, u32 width, u32 height,
                  f32 x0, f32 y0, f32 x1, f32 y1,
                  u8 r, u8 g, u8 b);
    
};

} // namespace Luma