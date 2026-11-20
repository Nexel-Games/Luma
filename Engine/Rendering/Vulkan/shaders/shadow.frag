#version 450

// Depth-only shadow caster. The Opaque permutation writes depth only; the
// Masked permutation clips texels below the material's Opacity Mask Clip
// Value, so masked surfaces cast shadows with the same holes they render with.
// Casters draw both faces (no culling), so two-sided surfaces cast from either
// side.

#define LUMA_TEXTURE_SET 0
#define LUMA_MATERIAL_SET 1
#include "material_common.glsl"

layout(constant_id = 0) const bool kMasked = false;

layout(push_constant) uniform Push {
    mat4 mvp;
    uint materialIndex;
} pc;

layout(location = 0) in vec2 vUV;

void main() {
    if (kMasked && EvaluateOpacityMask(pc.materialIndex, vUV) <
                       materials[pc.materialIndex].opacityMaskClip) {
        discard;
    }
}
