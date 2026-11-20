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
//   binding 2: RT2 — metallic (R) / specular (G) / AO (B) / selection (A)
//   binding 3: depth
//   binding 9: RT3 — emissive radiance (HDR)
//
// Lighting UBO (set 0, binding 4):
//   Sun direction, colour, ambient, shadow params, etc. Background fragments
//   (depth == far plane) render the physically based atmosphere from
//   sky_atmosphere.glsl — the same model the forward sky pass uses.

#include "sky_atmosphere.glsl"

const int MAX_LIGHTS = 16;
const int MAX_CASCADES = 4;
const float PI = 3.14159265359;

struct Light {
    vec4 posType;   // xyz = position, w = type (0 dir, 1 point, 2 spot)
    vec4 dirRange;  // xyz = direction, w = range
    vec4 color;     // rgb = color, w = intensity
    vec4 spot;      // x = cosInner, y = cosOuter
};

layout(set = 0, binding = 0) uniform sampler2D gAlbedo;
layout(set = 0, binding = 1) uniform sampler2D gNormal;
layout(set = 0, binding = 2) uniform sampler2D gMaterial;
layout(set = 0, binding = 3) uniform sampler2D gDepth;
layout(set = 0, binding = 9) uniform sampler2D gEmissive;

const uint kShadingUnlit = 1u;

layout(set = 0, binding = 4) uniform LightingUBO {
    mat4  invViewProj;
    vec4  camPos;
    vec4  camForward;   // xyz = camera forward (for cascade selection)
    vec4  sunDir;       // xyz = direction TO sun
    vec4  sunColor;     // rgb, w = intensity
    vec4  skyZenith;    // rgb IBL up
    vec4  skyHorizon;   // rgb IBL horizon, w = intensity
    vec4  groundColor;  // rgb IBL below horizon
    vec4  params;       // x = lightCount, y = sunShadows, z = 1/shadowSize,
                         // w = material environment enabled
    vec4  shadowParams; // x = softness, y = cascadeCount, z = shadowBias,
                        // w = normalBias (world units)
    vec4  cascadeSplits;
    vec4  cascadeTexel; // world units per shadow texel, per cascade
    vec4  skyLight;     // x = sky light enabled, y = max mip of skyLightCube,
                        // z = view mode (0 Lit, 1 Unlit, 2 Lighting Only,
                        //     3 Base Color, 4 World Normal, 5 Roughness, 6 Metallic)
    mat4  cascadeViewProj[MAX_CASCADES];
    AtmosphereParams atmo;
    Light lights[MAX_LIGHTS];
} u;

layout(set = 0, binding = 5) uniform sampler2DArray shadowMap;
layout(set = 0, binding = 6) uniform sampler2D environmentMap;

// Sky light (real-time capture of the atmosphere; see VulkanSkyLight):
// GGX-prefiltered cube (one roughness per mip) + L2 SH diffuse irradiance
// with the cosine lobe already convolved in.
layout(set = 0, binding = 7) uniform samplerCube skyLightCube;
layout(std430, set = 0, binding = 8) readonly buffer SkyLightSH {
    vec4 coeffs[9];
} skySH;

// Irradiance E(N) from the convolved SH (same basis as skylight_sh.comp).
vec3 SkyLightIrradiance(vec3 n) {
    vec3 e = skySH.coeffs[0].rgb * 0.282095
           + skySH.coeffs[1].rgb * (0.488603 * n.y)
           + skySH.coeffs[2].rgb * (0.488603 * n.z)
           + skySH.coeffs[3].rgb * (0.488603 * n.x)
           + skySH.coeffs[4].rgb * (1.092548 * n.x * n.y)
           + skySH.coeffs[5].rgb * (1.092548 * n.y * n.z)
           + skySH.coeffs[6].rgb * (0.315392 * (3.0 * n.z * n.z - 1.0))
           + skySH.coeffs[7].rgb * (1.092548 * n.x * n.z)
           + skySH.coeffs[8].rgb * (0.546274 * (n.x * n.x - n.y * n.y));
    return max(e, vec3(0.0));
}

// Unreal's ComputeReflectionCaptureMipFromRoughness (ROUGHEST_MIP = 1,
// ROUGHNESS_MIP_SCALE = 1.2). Inverse of the mapping the filter pass used.
float SkyLightMipFromRoughness(float roughness, float maxMip) {
    float levelFrom1x1 = 1.0 - 1.2 * log2(max(roughness, 0.001));
    return clamp(maxMip - levelFrom1x1, 0.0, maxMip);
}

// Poisson disk for the PCSS blocker search + filtering.
const vec2 POISSON[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870), vec2(0.34495938, 0.29387760),
    vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379),
    vec2(0.44323325, -0.97511554), vec2(0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590),
    vec2(0.19984126, 0.78641367), vec2(0.14383161, -0.14100790));

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

// ---- PBR helpers -----------------------------------------------------------

float DistributionGGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom + 0.0001);
}

float GeometrySmith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float g1 = NdotV / (NdotV * (1.0 - k) + k);
    float g2 = NdotL / (NdotL * (1.0 - k) + k);
    return g1 * g2;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Reconstruct world position from depth + UV using inverse VP.
vec3 ReconstructWorldPos(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = u.invViewProj * clip;
    return world.xyz / world.w;
}

vec3 SampleEnvironment(vec3 direction) {
    const float PI = 3.14159265359;
    const float ENVIRONMENT_EXPOSURE = 1.6;
    direction = normalize(direction);
    float uCoord = 0.5 + atan(direction.z, direction.x) / (2.0 * PI);
    float vCoord = 0.5 - asin(clamp(direction.y, -1.0, 1.0)) / PI;
    vec3 radiance = texture(
        environmentMap, vec2(fract(uCoord), clamp(vCoord, 0.0, 1.0))).rgb;
    return max(radiance * ENVIRONMENT_EXPOSURE,
               vec3(0.018, 0.022, 0.028));
}

vec3 SkyEnv(vec3 dir) {
    float y = dir.y;
    vec3 upper = mix(u.skyHorizon.rgb, u.skyZenith.rgb, clamp(y, 0.0, 1.0));
    vec3 lower = mix(u.skyHorizon.rgb, u.groundColor.rgb, clamp(-y, 0.0, 1.0));
    return y >= 0.0 ? upper : lower;
}

vec3 SkyIrradiance(vec3 N) {
    vec3 sky = mix(u.skyHorizon.rgb, u.skyZenith.rgb,
                   clamp(N.y * 0.5 + 0.5, 0.0, 1.0));
    return mix(u.groundColor.rgb, sky, clamp(N.y * 0.5 + 0.75, 0.0, 1.0));
}

vec3 FresnelSchlickRough(float cosTheta, vec3 F0, float roughness) {
    vec3 Fr = max(vec3(1.0 - roughness), F0);
    return F0 + (Fr - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Percentage-closer soft shadows in one cascade layer. `radiusScale` converts
// the cascade-0-tuned kernel to this cascade so penumbras keep the same world
// width across cascades (farther cascades cover more world per texel).
float pcss(int cascade, vec3 proj, float bias, float radiusScale) {
    vec2 uv = proj.xy * 0.5 + 0.5;
    float receiver = proj.z;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 ||
        receiver > 1.0 || receiver < 0.0) {
        return 1.0;
    }
    float texel = u.params.z;
    // Blocker search: average depth of occluders in a light-sized region.
    float searchR = u.shadowParams.x * texel * 6.0 * radiusScale;
    float blockerSum = 0.0;
    int blockers = 0;
    for (int i = 0; i < 16; ++i) {
        float d = texture(shadowMap, vec3(uv + POISSON[i] * searchR, cascade)).r;
        if (d < receiver - bias) {
            blockerSum += d;
            ++blockers;
        }
    }
    if (blockers == 0) return 1.0;
    float avgBlocker = blockerSum / float(blockers);
    // Penumbra grows with receiver-blocker separation (PCSS estimate).
    float penumbra = (receiver - avgBlocker) / max(avgBlocker, 1e-4);
    float filterR = clamp(penumbra * u.shadowParams.x * texel * 30.0 * radiusScale,
                          texel, texel * 12.0);
    float sum = 0.0;
    for (int i = 0; i < 16; ++i) {
        float d = texture(shadowMap, vec3(uv + POISSON[i] * filterR, cascade)).r;
        sum += (receiver - bias > d) ? 0.0 : 1.0;
    }
    return sum / 16.0;
}

// Shadow factor from one cascade. The lookup position is pushed off the
// surface along the normal by ~1.5 shadow texels (more at grazing angles):
// normal-offset biasing removes acne without the large depth bias that makes
// shadows detach from their casters ("peter-panning").
float cascadeShadow(int cascade, vec3 worldPos, vec3 N, float NdotL) {
    float texelW = max(u.cascadeTexel[cascade], 1e-5);
    vec3 offsetPos = worldPos +
        N * (texelW * 1.5 * (1.0 - 0.5 * NdotL) + u.shadowParams.w);
    vec4 sc = u.cascadeViewProj[cascade] * vec4(offsetPos, 1.0);
    vec3 proj = sc.xyz / sc.w;
    // Small compare bias (raster slope bias + normal offset do most work);
    // coarser cascades get a little more.
    float bias = u.shadowParams.z * 0.5 * (1.0 + float(cascade) * 0.5);
    float radiusScale = max(u.cascadeTexel[0], 1e-5) / texelW;
    return pcss(cascade, proj, bias, radiusScale);
}

// Cascaded sun shadow: pick a cascade by view depth, blend across cascade
// boundaries, and fade out toward the shadow distance.
float sunShadow(vec3 worldPos, vec3 N, vec3 L) {
    if (u.params.y < 0.5) return 1.0;
    int count = int(u.shadowParams.y);
    if (count <= 0) return 1.0;
    float viewDepth = dot(worldPos - u.camPos.xyz, u.camForward.xyz);
    float lastSplit = u.cascadeSplits[count - 1];
    if (viewDepth >= lastSplit) return 1.0;  // beyond the shadow distance

    int cascade = count - 1;
    for (int i = 0; i < count; ++i) {
        if (viewDepth < u.cascadeSplits[i]) {
            cascade = i;
            break;
        }
    }
    float NdotL = max(dot(N, L), 0.0);
    float shadow = cascadeShadow(cascade, worldPos, N, NdotL);

    // Position within this cascade's slice: 0 at its far edge, 1 at its near.
    float sliceNear = cascade == 0 ? 0.0 : u.cascadeSplits[cascade - 1];
    float sliceFar = u.cascadeSplits[cascade];
    float t = (sliceFar - viewDepth) / max(sliceFar - sliceNear, 1e-4);
    const float BLEND_BAND = 0.12;
    if (t < BLEND_BAND) {
        float w = t / BLEND_BAND;
        if (cascade < count - 1) {
            // Crossfade into the next cascade: no visible seam.
            shadow = mix(cascadeShadow(cascade + 1, worldPos, N, NdotL), shadow, w);
        } else {
            // Last cascade: fade to unshadowed instead of a hard cutoff line.
            shadow = mix(1.0, shadow, w);
        }
    }
    return shadow;
}

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
    int viewMode = int(u.skyLight.z + 0.5);
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
        if (u.params.w > 0.5) {
            vec3 worldPos = ReconstructWorldPos(vUV, depth);
            vec3 viewDirection = normalize(worldPos - u.camPos.xyz);
            // Linear HDR radiance; the post chain exposes + tone-maps it.
            outColor = vec4(SampleEnvironment(viewDirection), 1.0);
        } else if (u.atmo.ozoneSky.y > 0.0) {
            vec3 worldPos = ReconstructWorldPos(vUV, depth);
            outColor = vec4(RenderSkyRadiance(worldPos, u.camPos.xyz, u.atmo), 1.0);
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
    vec3 V = normalize(u.camPos.xyz - worldPos);

    // Fresnel F0 (Unreal convention): dielectrics reflect 0.08 * Specular
    // (Specular 0.5 -> 4%), metals tint the reflection with the base color.
    vec3 F0 = mix(vec3(0.08 * specular), albedo, metallic);

    // ---- Directional light (sun) ----
    vec3 L = normalize(u.sunDir.xyz);
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySmith(NdotV, NdotL, roughness);
    vec3  F = FresnelSchlick(HdotV, F0);

    vec3 sunSpecular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    vec3 sunContrib = (diffuse + sunSpecular) * u.sunColor.rgb * u.sunColor.w *
                      NdotL * sunShadow(worldPos, N, L);

    // ---- Punctual lights ----
    vec3 punctualContrib = vec3(0.0);
    int lightCount = int(u.params.x);
    for (int i = 0; i < lightCount && i < MAX_LIGHTS; ++i) {
        Light light = u.lights[i];
        vec3 lDir;
        float attenuation = 1.0;

        if (light.posType.w < 0.5) {
            // Directional
            lDir = normalize(light.dirRange.xyz);
        } else {
            // Point / spot
            vec3 toLight = light.posType.xyz - worldPos;
            float dist = max(length(toLight), 1e-4);
            lDir = toLight / dist;
            // Physical distance falloff 1/(d^p + 1) (p = 2: inverse square; +1
            // keeps it finite at the source), windowed smoothly to exactly
            // zero at the attenuation radius (Unreal / Karis 2013).
            float range = max(light.dirRange.w, 1e-3);
            float ratio = dist / range;
            float window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
            attenuation = (window * window) / (pow(dist, light.spot.z) + 1.0);

            if (light.posType.w > 1.5 && light.posType.w < 2.5) {
                // Spot cone: squared ramp between the outer and inner angles.
                float theta = dot(lDir, normalize(light.dirRange.xyz));
                float cone = clamp((theta - light.spot.y) /
                                   max(light.spot.x - light.spot.y, 1e-4), 0.0, 1.0);
                attenuation *= cone * cone;
            }
            // Tube (type 3) behaves as a point light for now.
        }

        vec3 lH = normalize(V + lDir);
        float lNdotL = max(dot(N, lDir), 0.0);
        float lNdotH = max(dot(N, lH), 0.0);
        float lHdotV = max(dot(lH, V), 0.0);

        float lD = DistributionGGX(lNdotH, roughness);
        float lG = GeometrySmith(NdotV, lNdotL, roughness);
        vec3  lF = FresnelSchlick(lHdotV, F0);

        vec3 lSpec = (lD * lG * lF) / (4.0 * NdotV * lNdotL + 0.0001);
        vec3 lkD = (1.0 - lF) * (1.0 - metallic);
        vec3 lDiff = lkD * albedo / PI;

        punctualContrib += (lDiff + lSpec) * light.color.rgb * light.color.w *
                           lNdotL * attenuation;
    }

    // ---- Ambient / material environment ------------------------------------
    // Split-sum IBL matching the forward scene pass: diffuse irradiance from
    // the hemisphere (or HDR) plus a roughness-aware specular lobe. This is
    // what keeps shaded faces from going black and gives the Unreal-like
    // "skylight" wrap on the default cube/plane.
    vec3 ambient;
    // Skylight strength straight from the IBL slider. (The old LDR path boosted
    // this x1.35 so shaded faces didn't crush to black under a clamped tone
    // curve; with HDR + filmic tone mapping that inflated the sky:sun ratio and
    // washed the scene out with flat blue shadows.)
    float iblScale = max(u.skyHorizon.w, 0.0);
    vec3 Fenv = FresnelSchlickRough(NdotV, F0, roughness);
    vec3 kdIBL = (1.0 - Fenv) * (1.0 - metallic);
    if (u.params.w > 0.5) {
        vec3 diffuseEnv = SampleEnvironment(N) * albedo * kdIBL;
        vec3 reflectedEnv = SampleEnvironment(reflect(-V, N));
        float environmentRoughness = max(roughness, 0.08);
        vec3 specularEnv = mix(reflectedEnv, SampleEnvironment(N),
                                environmentRoughness) * Fenv;
        ambient = (diffuseEnv + specularEnv) * iblScale * ao;
    } else {
        // Split-sum environment BRDF (Karis' analytic fit).
        const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
        const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
        vec4 rp = roughness * c0 + c1;
        float a004 = min(rp.x * rp.x, exp2(-9.28 * NdotV)) * rp.x + rp.y;
        vec2 envBRDF = vec2(-1.04, 1.04) * a004 + rp.zw;
        vec3 R = reflect(-V, N);

        vec3 diffuseIBL;
        vec3 prefiltered;
        if (u.skyLight.x > 0.5) {
            // Sky light from the captured atmosphere (Unreal SkyLight model):
            // Lambert of the SH irradiance, and the prefiltered cube at the
            // mip matching this roughness.
            diffuseIBL = SkyLightIrradiance(N) * (1.0 / PI) * albedo * kdIBL;
            prefiltered = textureLod(skyLightCube, R,
                SkyLightMipFromRoughness(roughness, u.skyLight.y)).rgb;
        } else {
            // No sky: fall back to the analytic hemisphere ambient.
            diffuseIBL = SkyIrradiance(N) * albedo * kdIBL;
            prefiltered = mix(SkyEnv(R), SkyIrradiance(N), roughness);
        }
        vec3 specularIBL = prefiltered * (F0 * envBRDF.x + envBRDF.y);
        ambient = (diffuseIBL + specularIBL) * iblScale * ao;
    }

    // ---- Compose ----
    // Scene-referred linear HDR radiance. Exposure, bloom, tone mapping and
    // the sRGB transfer all happen in the post chain (bloom_composite.frag),
    // so highlights keep their full range for bloom to work with.
    // Material AO darkens only the ambient (sky light) term, as in Unreal;
    // emissive adds on top of the lit result.
    vec3 color = sunContrib + punctualContrib + ambient + emissive;
    outColor = vec4(max(color, vec3(0.0)), 1.0);
}
