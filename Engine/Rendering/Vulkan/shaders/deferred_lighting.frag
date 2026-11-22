#version 450

// Deferred lighting fragment shader — reads G-Buffer targets and computes
// Cook-Torrance PBR lighting, outputting the final lit colour.  This
// shader doubles as the composition pass: it samples the G-Buffer,
// evaluates lighting, applies tone mapping, and writes gamma-encoded
// colour to the output render target.
//
// G-Buffer inputs (set 0):
//   binding 0: RT0 — base color (RGB, sRGB-stored) + shading model / 255 (A)
//   binding 1: RT1 — normal (RGB encoded [0,1]) + roughness (A)
//   binding 2: RT2 — metallic (R) / specular (G) / AO (B)
//   binding 3: depth
//   binding 9: RT3 — emissive radiance (HDR)
//
// Lighting UBO (set 0, binding 4):
//   Sun direction, colour, ambient, shadow params, etc. Background fragments
//   (depth == far plane) render the physically based atmosphere from
//   sky_atmosphere.glsl — the same model the forward sky pass uses.

#include "sky_atmosphere.glsl"

#define LUMA_LIGHTING_SET 0
#define LUMA_LIGHTING_BINDING 4
#include "lighting_common.glsl"

layout(set = 0, binding = 0) uniform sampler2D gAlbedo;
layout(set = 0, binding = 1) uniform sampler2D gNormal;
layout(set = 0, binding = 2) uniform sampler2D gMaterial;
layout(set = 0, binding = 3) uniform sampler2D gDepth;
layout(set = 0, binding = 9) uniform sampler2D gEmissive;
layout(set = 0, binding = 10) uniform sampler2D gCustom;  // RT4: shading-model data

const uint kShadingUnlit = 1u;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

// ---- Main ------------------------------------------------------------------

// Inverse of the composite's sRGB encode (exact piecewise curve), used so
// data channels show their raw value on screen.
vec3 SRGBToLinearApprox(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    vec3 lo = c / 12.92;
    vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(lo, hi, step(vec3(0.04045), c));
}

void main() {
    // Sample G-Buffer
    vec4  baseData  = texture(gAlbedo, vUV);
    vec3  albedo    = baseData.rgb;
    uint  shading   = uint(baseData.a * 255.0 + 0.5);
    vec4  normalR   = texture(gNormal, vUV);
    vec4  matData   = texture(gMaterial, vUV);
    vec3  emissive  = texture(gEmissive, vUV).rgb;
    float depth     = texture(gDepth, vUV).r;

    // Decode normal from [0,1] → [-1,1]
    vec3 N = normalize(normalR.rgb * 2.0 - 1.0);
    float roughness = clamp(normalR.a, 0.04, 1.0);
    float metallic  = clamp(matData.r, 0.0, 1.0);
    float specular  = clamp(matData.g, 0.0, 1.0);
    float ao        = clamp(matData.b, 0.0, 1.0);

    // ---- View modes (editor viewport) ----
    // Visualizations output linear values that the composite pass encodes to
    // sRGB without exposure or tone mapping; data channels (normal,
    // roughness, metallic) are pre-linearized so they display as raw values.
    int viewMode = int(lighting.skyLight.z + 0.5);
    if (viewMode >= 3 || viewMode == 1) {
        if (depth >= 1.0) {
            outColor = vec4(vec3(0.0), 1.0);
            return;
        }
        vec3 v;
        if (viewMode == 1 || viewMode == 3) v = albedo;                       // linear color
        else if (viewMode == 4) v = SRGBToLinearApprox(N * 0.5 + 0.5);
        else if (viewMode == 5) v = SRGBToLinearApprox(vec3(roughness));
        else                    v = SRGBToLinearApprox(vec3(metallic));
        outColor = vec4(v, 1.0);
        return;
    }
    if (viewMode == 2) {
        // Lighting Only: every surface becomes a neutral rough dielectric;
        // unlit and emissive contributions are not lighting.
        albedo = vec3(0.5);
        metallic = 0.0;
        specular = 0.5;
        emissive = vec3(0.0);
        shading = 0u;
    }

    // The material renderer uses the HDR environment. The main editor renderer
    // keeps its procedural atmosphere background.
    // Background = untouched depth (cleared to exactly 1.0). A threshold like
    // 0.9999 misclassifies far geometry (~400+ units with near=0.1) as sky.
    if (depth >= 1.0) {
        if (lighting.params.w > 0.5) {
            vec3 worldPos = ReconstructWorldPos(vUV, depth);
            vec3 viewDirection = normalize(worldPos - lighting.camPos.xyz);
            // Linear HDR radiance; the post chain exposes + tone-maps it.
            outColor = vec4(SampleEnvironment(viewDirection), 1.0);
        } else if (lighting.atmo.ozoneSky.y > 0.0) {
            vec3 worldPos = ReconstructWorldPos(vUV, depth);
            outColor = vec4(RenderSkyRadiance(worldPos, lighting.camPos.xyz, lighting.atmo), 1.0);
        } else {
            // Neutral dark backdrop (linear; ~0.07 after tone map + gamma).
            outColor = vec4(0.006, 0.007, 0.010, 1.0);
        }
        return;
    }

    // Unlit shading model: the base color plus emissive, no lighting.
    if (shading == kShadingUnlit) {
        outColor = vec4(albedo + emissive, 1.0);
        return;
    }

    vec3 worldPos = ReconstructWorldPos(vUV, depth);
    vec3 V = normalize(lighting.camPos.xyz - worldPos);

    LumaShading shadingData;
    shadingData.model = shading;
    shadingData.custom = texture(gCustom, vUV);
    vec3 lit = LumaSurfaceLighting(worldPos, N, V, albedo, metallic, specular, roughness, ao, shadingData);


    // ---- Compose ----
    // Scene-referred linear HDR radiance. Exposure, bloom, tone mapping and
    // the sRGB transfer all happen in the post chain (bloom_composite.frag),
    // so highlights keep their full range for bloom to work with.
    // Material AO darkens only the ambient (sky light) term, as in Unreal;
    // emissive adds on top of the lit result.
    vec3 color = lit + emissive;
    outColor = vec4(max(color, vec3(0.0)), 1.0);
}
