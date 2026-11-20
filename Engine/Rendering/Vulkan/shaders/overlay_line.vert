#version 450

// Editor overlay lines (transform gizmo). Segments are expanded into
// screen-space quads on the CPU (VulkanOverlayLinePass), so vertices arrive
// already in clip space.
layout(location = 0) in vec4 inClip;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 vColor;

void main() {
    gl_Position = inClip;
    vColor = inColor;
}
