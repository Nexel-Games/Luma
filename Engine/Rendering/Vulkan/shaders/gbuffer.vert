#version 450

// G-buffer vertex shader: transforms the engine vertex format (MeshVertex:
// position, normal, tangent + handedness, uv0) to world/clip space.

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 viewProj;
    vec4 camPos;
} u;

// Matches VulkanDeferredRenderer::MeshPush.
layout(push_constant) uniform Push {
    mat4 model;
    vec4 normalMatrix[3];  // inverse-transpose of the model's 3x3 (columns)
    uint materialIndex;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec4 vTangent;
layout(location = 3) out vec2 vUV;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    vWorldPos = worldPos.xyz;
    mat3 normalMatrix = mat3(pc.normalMatrix[0].xyz, pc.normalMatrix[1].xyz,
                             pc.normalMatrix[2].xyz);
    // Normals use the inverse-transpose (correct under non-uniform scale);
    // tangents lie in the surface and transform with the model matrix.
    vNormal = normalMatrix * inNormal;
    vTangent = vec4(mat3(pc.model) * inTangent.xyz, inTangent.w);
    vUV = inUV;
    gl_Position = u.viewProj * worldPos;
}
