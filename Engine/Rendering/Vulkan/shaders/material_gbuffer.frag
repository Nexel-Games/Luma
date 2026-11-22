// material_gbuffer.frag - G-buffer pass of Opaque / Masked surface materials.
//
// RT0 (R8G8B8A8_SRGB):       rgb = base colour, a = shading model / 255
// RT1 (R16G16B16A16_SFLOAT): rgb = world normal * 0.5 + 0.5, a = roughness
// RT2 (R8G8B8A8_UNORM):      r = metallic, g = specular, b = AO
// RT3 (R16G16B16A16_SFLOAT): rgb = emissive radiance (HDR)
// RT4 (R8G8B8A8_UNORM):      shading-model data: Subsurface / Two-Sided
//                            Foliage rgb = subsurface colour, a = opacity;
//                            Clear Coat r = coat, g = coat roughness

#include "material_pixel.glsl"

layout(location = 0) out vec4 outBaseColor;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMaterial;
layout(location = 3) out vec4 outEmissive;
layout(location = 4) out vec4 outCustom;

void main() {
    MaterialPixel m = LumaShadePixel();
    outBaseColor = vec4(clamp(m.baseColor, 0.0, 1.0), float(LUMA_SHADING_MODEL) / 255.0);
    outNormal = vec4(P.pixelNormal * 0.5 + 0.5, clamp(m.roughness, 0.0, 1.0));
    outMaterial = vec4(clamp(m.metallic, 0.0, 1.0), clamp(m.specular, 0.0, 1.0), clamp(m.ao, 0.0, 1.0), 0.0);
    outEmissive = vec4(max(m.emissive, vec3(0.0)), 1.0);
#if LUMA_SHADING_MODEL == 3
    outCustom = vec4(clamp(m.customData0, 0.0, 1.0), clamp(m.customData1, 0.0, 1.0), 0.0, 0.0);
#elif LUMA_SHADING_MODEL == 2 || LUMA_SHADING_MODEL == 4
    outCustom = vec4(clamp(m.subsurfaceColor, 0.0, 1.0), clamp(m.opacity, 0.0, 1.0));
#else
    outCustom = vec4(0.0);
#endif
}
