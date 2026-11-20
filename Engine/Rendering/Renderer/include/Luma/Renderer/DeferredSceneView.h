#pragma once

#include "Luma/Core/Types.h"
#include "Luma/Math/Math.h"
#include "Luma/RHI/Renderer.h"

// Per-frame camera + scene description handed to the deferred renderer
// (VulkanDeferredRenderer::RenderScene). The engine has a single scene
// render path: this deferred renderer.

namespace Luma::Renderer2 {

struct DeferredSceneView {
    Math::Vec3 cameraPosition{0.0f, 0.0f, 0.0f};
    Math::Vec3 cameraDirection{0.0f, 0.0f, -1.0f};
    Math::Mat4 viewMatrix = Math::Mat4::Identity();
    Math::Mat4 projectionMatrix = Math::Mat4::Identity();
    Math::Mat4 viewProjectionMatrix = Math::Mat4::Identity();
    f32 fov = 0.9f;
    f32 nearPlane = 0.1f;
    f32 farPlane = 500.0f;
    u32 width = 1920;
    u32 height = 1080;
    f32 time = 0.0f;
    f32 deltaTime = 0.0f;

    // The RHI scene data (instances, lights, sky, post-process), filled by
    // the scene builder before calling RenderScene.
    const Luma::SceneView* sceneData = nullptr;
    const Luma::LightingParams* lightingParams = nullptr;
};

}  // namespace Luma::Renderer2
