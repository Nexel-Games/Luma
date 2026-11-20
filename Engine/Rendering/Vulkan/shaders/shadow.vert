#version 450

// Depth-only pass from the sun's point of view. `mvp` is lightViewProj * model.
// uv0 feeds the Masked permutation's opacity-mask clip.
layout(location = 0) in vec3 inPos;
layout(location = 3) in vec2 inUV;

// Matches VulkanDeferredRenderer::ShadowPush.
layout(push_constant) uniform Push {
    mat4 mvp;
    uint materialIndex;
} pc;

layout(location = 0) out vec2 vUV;

void main() {
    vUV = inUV;
    gl_Position = pc.mvp * vec4(inPos, 1.0);
}
