#version 450

// Selection mask: the selected actor's sections, transformed exactly like
// gbuffer.vert (model first, then viewProj) so its depth matches the scene
// depth bit-for-bit where it is visible.

layout(push_constant) uniform Push {
    mat4 viewProj;
    mat4 model;
} pc;

layout(location = 0) in vec3 inPosition;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = pc.viewProj * worldPos;
}
