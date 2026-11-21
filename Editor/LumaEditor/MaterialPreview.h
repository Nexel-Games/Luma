#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

#include "Luma/Asset/AssetId.h"
#include "Luma/Asset/AssetRegistry.h"
#include "Luma/Editor/MaterialSceneBridge.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/RHI/Renderer.h"
#include "Luma/Scene/Scene.h"

namespace Luma {
namespace Rendering {
class VulkanDeferredRenderer;
}

enum class PreviewShape { Sphere, Cube, Plane, Cylinder };

// The editor's preview scene: one actor in a studio setup (sky light plus a
// key sun, no grid) rendered by a dedicated deferred renderer, so material
// and mesh previews use exactly the viewport's shaders. Drives the Material
// Editor viewport (Render) and the Content Browser's GPU thumbnails
// (Capture*).
class MaterialPreview {
public:
    MaterialPreview(Rendering::VulkanDeferredRenderer& renderer,
                    Material::MaterialLibrary& library);

    // Renders `materialPath` on `shape`, orbiting by yaw/pitch (radians), at
    // width x height; `sky` = sky backdrop + sky light (else a dark studio).
    // Returns the UI texture showing the result.
    TextureHandle Render(std::string_view materialPath, PreviewShape shape, f32 yaw, f32 pitch,
                         u32 width, u32 height, bool sky = true);

    // Square RGBA8 captures (top row first) for thumbnails.
    bool CaptureMaterial(std::string_view materialPath, u32 size, std::vector<u8>& outRgba);
    bool CaptureMesh(std::string_view meshPath, u32 size, std::vector<u8>& outRgba);
    // External file changes (the preview has its own GPU copies).
    void OnTextureFileChanged(std::string_view virtualPath) {
        m_bridge.OnTextureFileChanged(virtualPath);
    }
    void OnMeshFileChanged(std::string_view meshPath) { m_bridge.OnMeshFileChanged(meshPath); }

    // CaptureMaterial written to a PNG.
    bool Capture(std::string_view materialPath, const std::filesystem::path& png, u32 size);

private:
    // Points the actor at `materialPath` on a primitive or `mesh`, then
    // renders one frame orbiting the actor's bounds.
    void RenderFrame(f32 yaw, f32 pitch, u32 width, u32 height, f32 deltaSeconds);
    bool CaptureSquare(u32 size, std::vector<u8>& outRgba);
    // Capture size: the renderer's current size (no G-buffer rebuild when
    // the Material Editor is showing), 256 x 256 before its first frame.
    void CaptureExtent(u32& width, u32& height) const;

    Rendering::VulkanDeferredRenderer& m_renderer;
    Material::MaterialLibrary& m_library;
    Editor::MaterialSceneBridge m_bridge;
    Scene m_scene;
    Entity m_actor = kNullEntity;
    std::vector<SceneInstance> m_instances;
    SceneView m_view;
};

}  // namespace Luma
