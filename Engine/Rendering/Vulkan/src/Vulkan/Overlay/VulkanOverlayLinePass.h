#pragma once

#include <string>
#include <vector>

#include "Luma/Math/Math.h"
#include "Luma/RHI/Renderer.h"
#include "Vulkan/VulkanCommon.h"
#include "Vulkan/VulkanMemory.h"

namespace Luma {

// Draws editor overlay lines (SceneView::overlayLines — the transform gizmo)
// on top of the final display image, without depth testing. Its own module:
// owns the pipeline and a persistently mapped vertex buffer. Each segment is
// expanded on the CPU into a screen-space quad of constant pixel width (wide
// lines are not portable in Vulkan), clipped against the near plane first. The
// quad carries a 1px feather on each side that the fragment shader fades out
// for anti-aliased edges.
class VulkanOverlayLinePass {
public:
    // `depthFormat` set: lines are depth-tested (not written) against the
    // scene depth bound in the rendering scope (DebugDraw depth-tested lines).
    VulkanOverlayLinePass(VkPhysicalDevice physical, VkDevice device,
                          const std::string& shaderDir, VkFormat colorFormat,
                          VkFormat depthFormat = VK_FORMAT_UNDEFINED);
    ~VulkanOverlayLinePass();

    VulkanOverlayLinePass(const VulkanOverlayLinePass&) = delete;
    VulkanOverlayLinePass& operator=(const VulkanOverlayLinePass&) = delete;

    bool Valid() const { return m_pipeline != VK_NULL_HANDLE; }

    // Records the lines into an already-open (dynamic) rendering scope whose
    // color target is `width` x `height`. `lines` holds vertex pairs.
    void Record(VkCommandBuffer cmd, const LineVertex* lines, u32 vertexCount,
                const Math::Mat4& viewProj, u32 width, u32 height,
                f32 thicknessPx = 1.5f, u32 slot = 0);

private:
    struct Vertex {
        f32 clip[4];
        f32 color[4];
    };
    static constexpr u32 kMaxVertices = 1u << 16;

    VkDevice m_device;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    GpuBuffer m_vertices;
    std::vector<Vertex> m_scratch;
};

}  // namespace Luma
