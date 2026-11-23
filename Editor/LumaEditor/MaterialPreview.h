#pragma once

#include <bit>
#include <filesystem>
#include <unordered_map>
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
    // width x height; `sky` = HDRI backdrop (blurred) + its lighting (else a
    // dark studio); `floor` = a M_GridBox floor cube under the mesh.
    // Returns the UI texture showing the result.
    //
    // `key` (non-zero) identifies the preview (one per editor). Each keyed
    // preview keeps its own snapshot and re-renders only when its inputs
    // change: material / texture / mesh content (the renderer's content
    // revision after syncing), shape, orbit, size, sky, floor. After a change
    // it keeps rendering kSettleFrames frames so auto exposure converges.
    // key 0 renders every call into the shared viewport texture.
    TextureHandle Render(std::string_view materialPath, PreviewShape shape, f32 yaw, f32 pitch,
                         u32 width, u32 height, bool sky = true, bool floor = true, u64 key = 0);
    // Frees a keyed preview's snapshot (its editor closed).
    void Release(u64 key);
    // GPU frames rendered by Render (tests: idle previews render nothing).
    u64 RenderedFrames() const { return m_renderedFrames; }
    static constexpr u32 kSettleFrames = 45;

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
    // Returns false when `key`'s inputs are unchanged and settled (nothing
    // rendered; its snapshot is current).
    bool RenderFrame(f32 yaw, f32 pitch, u32 width, u32 height, f32 deltaSeconds, u64 key = 0,
                     u64 requestHash = 0);
    bool CaptureSquare(u32 size, std::vector<u8>& outRgba);
    // Capture size: the renderer's current size (no G-buffer rebuild when
    // the Material Editor is showing), 256 x 256 before its first frame.
    void CaptureExtent(u32& width, u32& height) const;

    Rendering::VulkanDeferredRenderer& m_renderer;
    Material::MaterialLibrary& m_library;
    Editor::MaterialSceneBridge m_bridge;
    Scene m_scene;
    Entity m_actor = kNullEntity;
    Entity m_floor = kNullEntity;  // M_GridBox cube under the actor (interactive previews only)
    bool m_showFloor = false;
    f32 m_fovY = 0.5f;  // vertical FOV of the next frame (Render: wide; captures: narrow)
    std::vector<SceneInstance> m_instances;
    SceneView m_view;
    struct KeyState {
        u64 hash = 0;    // request + content revision of the last render
        u32 settle = 0;  // frames still to render after the last change
    };
    std::unordered_map<u64, KeyState> m_keys;
    u64 m_renderedFrames = 0;
};

}  // namespace Luma
