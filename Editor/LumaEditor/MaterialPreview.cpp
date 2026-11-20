#include "MaterialPreview.h"

#include <algorithm>
#include <cmath>

#include "Luma/Core/Log.h"
#include "Luma/Rendering/Vulkan/VulkanDeferredRenderer.h"
#include "Luma/Renderer/DeferredSceneView.h"
#include "Luma/Scene/Components.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4505)
#endif
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace Luma {
namespace {

constexpr f32 kFovY = 0.5f;  // narrow lens: little perspective distortion
// Thumbnails view the actor slightly from above and to the side.
constexpr f32 kThumbnailYaw = 0.6f;
constexpr f32 kThumbnailPitch = 0.3f;

MeshPrimitive ToPrimitive(PreviewShape shape) {
    switch (shape) {
        case PreviewShape::Cube: return MeshPrimitive::Cube;
        case PreviewShape::Plane: return MeshPrimitive::Plane;
        case PreviewShape::Cylinder: return MeshPrimitive::Cylinder;
        case PreviewShape::Sphere: break;
    }
    return MeshPrimitive::Sphere;
}

}  // namespace

MaterialPreview::MaterialPreview(Rendering::VulkanDeferredRenderer& renderer,
                                 Material::MaterialLibrary& library,
                                 const AssetRegistry& registry)
    : m_renderer(renderer), m_library(library), m_registry(registry) {
    m_actor = m_scene.CreateEntity("PreviewActor");
    m_scene.Registry().emplace<MeshRendererComponent>(m_actor);

    m_view.sky.enabled = true;
    // A lone actor has nothing to shadow; cascades fitted to such a small
    // frustum only add self-shadowing artifacts.
    m_view.lighting.sunShadows = false;
    m_view.grid.enabled = false;
    // Auto exposure as in the viewport; captures snap it (delta time 0).
    m_view.viewMode = ViewMode::Lit;
    m_view.fovYRadians = kFovY;
    m_view.nearZ = 0.05f;
    m_view.farZ = 200.0f;
}

void MaterialPreview::RenderFrame(f32 yaw, f32 pitch, u32 width, u32 height,
                                  f32 deltaSeconds) {
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    m_bridge.Sync(m_scene, m_renderer, m_library, m_registry);
    m_bridge.BuildInstances(m_scene, kNullEntity, m_instances);

    // Frame the actor's bounding sphere.
    const auto& mr = m_scene.Registry().get<MeshRendererComponent>(m_actor);
    Math::Vec3 center{0.0f, 0.0f, 0.0f};
    f32 radius = 0.75f;  // unit primitives
    if (mr.UsesMeshAsset()) {
        if (const Editor::MeshBounds* b = m_bridge.Bounds(mr.meshAsset)) {
            center = (b->min + b->max) * 0.5f;
            const Math::Vec3 extent = (b->max - b->min) * 0.5f;
            radius = std::max(std::sqrt(Math::Dot(extent, extent)), 1e-3f);
        }
    }
    const f32 aspect = static_cast<f32>(width) / static_cast<f32>(height);
    const f32 halfFov = std::min(kFovY, 2.0f * std::atan(std::tan(kFovY * 0.5f) * aspect)) * 0.5f;
    const f32 distance = radius / std::sin(halfFov) * 1.08f;
    const Math::Vec3 eye = center + Math::Vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                                               std::cos(pitch) * std::cos(yaw)) * distance;

    m_view.view = Math::LookAt(eye, center, Math::Vec3(0.0f, 1.0f, 0.0f));
    // Studio key light that follows the camera: above and to the left of the
    // view, so the visible side is always lit (the captured sky fills in).
    {
        const Math::Vec3 toEye = Math::Normalize(eye - center);
        const Math::Vec3 right =
            Math::Normalize(Math::Cross(Math::Vec3(0.0f, 1.0f, 0.0f), toEye));
        const Math::Vec3 key = Math::Normalize(toEye * 0.8f + Math::Vec3(0.0f, 0.9f, 0.0f) -
                                               right * 0.6f);
        m_view.sky.sunDirection = key;
        m_view.lighting.sunDirection = key;
    }
    m_view.instances = m_instances.data();
    m_view.instanceCount = static_cast<u32>(m_instances.size());
    m_view.nearZ = std::max(distance - radius * 4.0f, 0.01f);
    m_view.farZ = distance + radius * 8.0f + 50.0f;

    Renderer2::DeferredSceneView frame;
    frame.cameraPosition = eye;
    frame.cameraDirection = Math::Normalize(center - eye);
    frame.viewMatrix = m_view.view;
    frame.projectionMatrix = Math::PerspectiveForViewport(kFovY, static_cast<f32>(width),
                                                          static_cast<f32>(height),
                                                          m_view.nearZ, m_view.farZ);
    frame.viewProjectionMatrix = frame.projectionMatrix * frame.viewMatrix;
    frame.fov = kFovY;
    frame.nearPlane = m_view.nearZ;
    frame.farPlane = m_view.farZ;
    frame.width = width;
    frame.height = height;
    frame.deltaTime = deltaSeconds;
    frame.sceneData = &m_view;
    frame.lightingParams = &m_view.lighting;

    m_renderer.SetViewportDimensions(static_cast<i32>(width), static_cast<i32>(height));
    m_renderer.PrepareScene();
    m_renderer.RenderScene(frame);
}

TextureHandle MaterialPreview::Render(std::string_view materialPath, PreviewShape shape,
                                      f32 yaw, f32 pitch, u32 width, u32 height, bool sky) {
    auto& mr = m_scene.Registry().get<MeshRendererComponent>(m_actor);
    mr.meshAsset = AssetId{};
    mr.primitive = ToPrimitive(shape);
    mr.materials = {std::string(materialPath)};
    m_view.sky.enabled = sky;  // off: dark backdrop, hemisphere ambient
    // Interactive: exposure adapts over frames like the viewport.
    RenderFrame(yaw, pitch, width, height, 1.0f / 60.0f);
    m_view.sky.enabled = true;  // thumbnails always use the sky
    return m_renderer.GetViewportTextureHandle();
}

void MaterialPreview::CaptureExtent(u32& width, u32& height) const {
    width = m_renderer.ViewportWidth() > 0 ? static_cast<u32>(m_renderer.ViewportWidth()) : 256u;
    height = m_renderer.ViewportHeight() > 0 ? static_cast<u32>(m_renderer.ViewportHeight()) : 256u;
}

bool MaterialPreview::CaptureSquare(u32 size, std::vector<u8>& outRgba) {
    // Capture at the renderer's current size (no G-buffer rebuild per
    // thumbnail): a centered square crop, box-filtered down to `size`.
    std::vector<u8> frame;
    u32 width = 0, height = 0;
    if (!m_renderer.ReadFinalImage(frame, width, height) || width == 0 || height == 0) {
        return false;
    }
    const u32 side = std::min(width, height);
    const u32 x0 = (width - side) / 2;
    const u32 y0 = (height - side) / 2;
    outRgba.assign(static_cast<usize>(size) * size * 4, 0);
    const f32 scale = static_cast<f32>(side) / static_cast<f32>(size);
    for (u32 y = 0; y < size; ++y) {
        for (u32 x = 0; x < size; ++x) {
            const u32 sx0 = x0 + static_cast<u32>(x * scale);
            const u32 sy0 = y0 + static_cast<u32>(y * scale);
            const u32 sx1 = std::max(sx0 + 1, x0 + static_cast<u32>((x + 1) * scale));
            const u32 sy1 = std::max(sy0 + 1, y0 + static_cast<u32>((y + 1) * scale));
            u32 sum[4] = {0, 0, 0, 0};
            u32 count = 0;
            for (u32 sy = sy0; sy < sy1 && sy < height; ++sy) {
                for (u32 sx = sx0; sx < sx1 && sx < width; ++sx) {
                    const u8* p = &frame[(static_cast<usize>(sy) * width + sx) * 4];
                    for (int c = 0; c < 4; ++c) sum[c] += p[c];
                    ++count;
                }
            }
            u8* out = &outRgba[(static_cast<usize>(y) * size + x) * 4];
            for (int c = 0; c < 4; ++c) out[c] = static_cast<u8>(sum[c] / std::max(count, 1u));
        }
    }
    return true;
}

bool MaterialPreview::CaptureMaterial(std::string_view materialPath, u32 size,
                                      std::vector<u8>& outRgba) {
    auto& mr = m_scene.Registry().get<MeshRendererComponent>(m_actor);
    mr.meshAsset = AssetId{};
    mr.primitive = MeshPrimitive::Sphere;
    mr.materials = {std::string(materialPath)};
    u32 w = 0, h = 0;
    CaptureExtent(w, h);
    // Two frames: the first captures the sky light; exposure snaps (dt 0).
    RenderFrame(kThumbnailYaw, kThumbnailPitch, w, h, 0.0f);
    RenderFrame(kThumbnailYaw, kThumbnailPitch, w, h, 0.0f);
    return CaptureSquare(size, outRgba);
}

bool MaterialPreview::CaptureMesh(const AssetId& mesh, u32 size, std::vector<u8>& outRgba) {
    auto& mr = m_scene.Registry().get<MeshRendererComponent>(m_actor);
    mr.meshAsset = mesh;
    mr.materials.clear();
    u32 w = 0, h = 0;
    CaptureExtent(w, h);
    RenderFrame(kThumbnailYaw, kThumbnailPitch, w, h, 0.0f);
    if (!m_bridge.Bounds(mesh)) return false;  // mesh failed to load
    RenderFrame(kThumbnailYaw, kThumbnailPitch, w, h, 0.0f);
    return CaptureSquare(size, outRgba);
}

bool MaterialPreview::Capture(std::string_view materialPath, const std::filesystem::path& png,
                              u32 size) {
    std::vector<u8> rgba;
    if (!CaptureMaterial(materialPath, size, rgba)) return false;
    return stbi_write_png(png.string().c_str(), static_cast<int>(size), static_cast<int>(size), 4,
                          rgba.data(), static_cast<int>(size * 4)) != 0;
}

}  // namespace Luma
