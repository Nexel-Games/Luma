// material_forward.frag - forward pass of Translucent / Additive / Modulate
// surface materials, lit per pixel with the same functions as the deferred
// pass (lighting_common.glsl). Blend states (set by the pipeline):
//   Translucent  ONE, ONE_MINUS_SRC_ALPHA  (premultiplied: rgb = colour * opacity)
//   Additive     ONE, ONE                  (rgb = colour * opacity)
//   Modulate     DST_COLOR, ZERO           (rgb = colour)
// With refraction the pass reads the scene-colour copy itself and writes the
// final composite (alpha 1).

#include "sky_atmosphere.glsl"
#include "lighting_common.glsl"
#include "material_pixel.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    MaterialPixel m = LumaShadePixel();
    float opacity = clamp(m.opacity, 0.0, 1.0);

#if LUMA_SHADING_MODEL == 1 || defined(LUMA_TRANSLUCENCY_UNLIT)
    vec3 color = m.emissive;
#else
    vec3 V = P.cameraVector;
    vec3 lit = LumaSurfaceLighting(P.worldPos, P.pixelNormal, V, clamp(m.baseColor, 0.0, 1.0),
                                   clamp(m.metallic, 0.0, 1.0), clamp(m.specular, 0.0, 1.0),
                                   clamp(m.roughness, 0.04, 1.0), clamp(m.ao, 0.0, 1.0));
    vec3 color = lit + m.emissive;
#endif

#if defined(LUMA_BLEND_MODULATE)
    outColor = vec4(color, 1.0);
#elif defined(LUMA_REFRACTION)
    // 1 = index of refraction: offset by the view-space normal; 2 = pixel
    // offset: Refraction is a distance in pixels along the normal.
    vec3 normalVS = mat3(lumaFrame.view) * P.pixelNormal;
#if LUMA_REFRACTION == 1
    vec2 offset = (m.refraction - 1.0) * normalVS.xy * 0.1;
#else
    vec2 offset = normalVS.xy * m.refraction * lumaFrame.viewport.zw;
#endif
    vec3 behind = MSceneColor(clamp(P.screenUV + offset, vec2(0.0), vec2(1.0)));
#if defined(LUMA_BLEND_ADDITIVE)
    outColor = vec4(behind + color * opacity, 1.0);
#else
    outColor = vec4(mix(behind, color, opacity), 1.0);
#endif
#elif defined(LUMA_BLEND_ADDITIVE)
    outColor = vec4(color * opacity, 0.0);
#else
    outColor = vec4(color * opacity, opacity);
#endif
}
