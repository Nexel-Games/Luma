#version 450

// G-buffer fragment shader: evaluates the draw's material (material_common)
// and writes the surface to the G-buffer.
//
// RT0 (R8G8B8A8_SRGB):       rgb = base color (linear in, sRGB stored),
//                            a = shading model / 255
// RT1 (R16G16B16A16_SFLOAT): rgb = world normal * 0.5 + 0.5, a = roughness
// RT2 (R8G8B8A8_UNORM):      r = metallic, g = specular, b = AO
// RT3 (R16G16B16A16_SFLOAT): rgb = emissive radiance (HDR)
//
// Pipeline permutations: kMasked (Masked blend mode: clip below the
// material's Opacity Mask Clip Value) x culling (two-sided = no culling).

#include "material_common.glsl"

layout(constant_id = 0) const bool kMasked = false;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 normalMatrix[3];
    uint materialIndex;
} pc;

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vTangent;
layout(location = 3) in vec2 vUV;

layout(location = 0) out vec4 outBaseColor;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMaterial;
layout(location = 3) out vec4 outEmissive;

void main() {
    MaterialSample s = EvaluateMaterial(pc.materialIndex, vUV, vNormal, vTangent,
                                        gl_FrontFacing);
    if (kMasked && s.opacityMask < materials[pc.materialIndex].opacityMaskClip) {
        discard;
    }
    float shadingModel = float(materials[pc.materialIndex].shadingModel);
    outBaseColor = vec4(s.baseColor, shadingModel / 255.0);
    outNormal = vec4(s.normalWS * 0.5 + 0.5, s.roughness);
    outMaterial = vec4(s.metallic, s.specular, s.ao, 0.0);
    outEmissive = vec4(s.emissive, 1.0);
}
