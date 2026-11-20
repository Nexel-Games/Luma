// material_common.glsl - evaluates a Luma material at a surface point.
//
// Every pass that shades or clips mesh surfaces (G-buffer, shadow depth,
// material preview, thumbnails) includes this file and calls
// EvaluateMaterial / EvaluateOpacityMask, so a material looks the same
// everywhere it renders.
//
// PHASE 2 (material node graph): EvaluateMaterial is the single insertion
// point for generated code. The graph compiler will emit a function with this
// exact signature per material permutation; the passes do not change.
//
// Data layout (std430) mirrors VulkanMaterialSystem.h exactly:
//   GpuMaterialInputData -> MaterialInput (64 bytes)
//   GpuMaterial          -> Material (16 + 11 * 64 bytes)
// Textures are one bindless sampler2D array (VulkanTextureCache): element
// 2*s is texture s's linear view, 2*s+1 its sRGB view; the material stores
// the element matching each input's color space.
//
// Includers define LUMA_MATERIAL_SET / LUMA_TEXTURE_SET to choose the
// descriptor set numbers (defaults: textures = 1, materials = 2).

#ifndef LUMA_MATERIAL_COMMON_GLSL
#define LUMA_MATERIAL_COMMON_GLSL

#extension GL_EXT_nonuniform_qualifier : require

#ifndef LUMA_TEXTURE_SET
#define LUMA_TEXTURE_SET 1
#endif
#ifndef LUMA_MATERIAL_SET
#define LUMA_MATERIAL_SET 2
#endif

// Input slots (GpuMaterialInput / Luma::Material::MaterialParameter order).
const uint kInputBaseColor = 0u;
const uint kInputMetallic = 1u;
const uint kInputSpecular = 2u;
const uint kInputRoughness = 3u;
const uint kInputNormal = 4u;
const uint kInputEmissive = 5u;
const uint kInputAO = 6u;
const uint kInputOpacityMask = 7u;
const uint kInputCavity = 8u;
const uint kInputHeight = 9u;
const uint kInputBump = 10u;
const uint kInputCount = 11u;

// MaterialInput.flags
const uint kFlagHasTexture = 1u;
const uint kFlagFlipGreen = 2u;
const uint kFlagInvert = 4u;

// MaterialInput.channel (GpuTextureChannel)
const uint kChannelRGB = 0u;
const uint kChannelR = 1u;
const uint kChannelG = 2u;
const uint kChannelB = 3u;
const uint kChannelA = 4u;

// Material.blendMode / shadingModel
const uint kBlendOpaque = 0u;
const uint kBlendMasked = 1u;
const uint kShadingDefaultLit = 0u;
const uint kShadingUnlit = 1u;

struct MaterialInput {
    vec4 constant;
    vec4 tint;
    vec4 tilingOffset;  // xy tiling, zw offset
    uint texIndex;
    uint channel;
    float scalar;       // normal strength (Normal) / intensity (Emissive)
    uint flags;
};

struct Material {
    uint blendMode;
    uint shadingModel;
    uint twoSided;
    float opacityMaskClip;
    MaterialInput inputs[11];
};

layout(std430, set = LUMA_MATERIAL_SET, binding = 0) readonly buffer MaterialTable {
    Material materials[];
};

layout(set = LUMA_TEXTURE_SET, binding = 0) uniform sampler2D materialTextures[];

struct MaterialSample {
    vec3 baseColor;
    float metallic;
    float specular;
    float roughness;
    vec3 normalWS;
    vec3 emissive;
    float ao;           // material AO x cavity
    float opacityMask;
};

bool MaterialHasTexture(MaterialInput mi) {
    return (mi.flags & kFlagHasTexture) != 0u;
}

// The texture sample, channel-selected (scalars broadcast) and tinted.
vec4 SampleMaterialTexture(MaterialInput mi, vec2 uv) {
    vec2 tuv = uv * mi.tilingOffset.xy + mi.tilingOffset.zw;
    vec4 t = texture(materialTextures[nonuniformEXT(mi.texIndex)], tuv);
    if ((mi.flags & kFlagInvert) != 0u) t = vec4(1.0) - t;
    vec4 v;
    if (mi.channel == kChannelR)      v = vec4(t.r);
    else if (mi.channel == kChannelG) v = vec4(t.g);
    else if (mi.channel == kChannelB) v = vec4(t.b);
    else if (mi.channel == kChannelA) v = vec4(t.a);
    else                                 v = t;
    return v * mi.tint;
}

// A color/scalar input: the tinted texture when one is assigned, otherwise
// the constant.
vec4 EvaluateInput(MaterialInput mi, vec2 uv) {
    return MaterialHasTexture(mi) ? SampleMaterialTexture(mi, uv) : mi.constant;
}

float EvaluateOpacityMask(uint materialIndex, vec2 uv) {
    return EvaluateInput(materials[materialIndex].inputs[kInputOpacityMask], uv).x;
}

// Tangent-space normal for the Normal input: an OpenGL-style map
// (+Y = image up; flipGreen converts DirectX maps) scaled by strength, or
// the constant direction.
vec3 EvaluateTangentNormal(MaterialInput mi, vec2 uv) {
    vec3 n;
    if (MaterialHasTexture(mi)) {
        vec2 tuv = uv * mi.tilingOffset.xy + mi.tilingOffset.zw;
        n = texture(materialTextures[nonuniformEXT(mi.texIndex)], tuv).xyz * 2.0 - 1.0;
        if ((mi.flags & kFlagFlipGreen) != 0u) n.y = -n.y;
        n.xy *= mi.scalar;
    } else {
        n = mi.constant.xyz;
    }
    float len = length(n);
    return len > 1e-5 ? n / len : vec3(0.0, 0.0, 1.0);
}

// Height map value (0 = deepest, 1 = surface) at base-UV `uv`, sampled with
// fixed gradients so it can be called inside the parallax loop.
float SampleHeight(MaterialInput mi, vec2 uv, vec2 dx, vec2 dy) {
    vec2 tuv = uv * mi.tilingOffset.xy + mi.tilingOffset.zw;
    vec4 t = textureGrad(materialTextures[nonuniformEXT(mi.texIndex)], tuv,
                         dx * mi.tilingOffset.xy, dy * mi.tilingOffset.xy);
    float h = mi.channel == kChannelG ? t.g : mi.channel == kChannelB ? t.b
            : mi.channel == kChannelA ? t.a : t.r;
    return (mi.flags & kFlagInvert) != 0u ? 1.0 - h : h;
}

// Parallax occlusion mapping: marches the view ray through the height field
// in tangent space and returns the displaced base UV. `viewTS` points from the
// surface to the eye (x = +u, y = image up, z = normal).
vec2 ParallaxOcclusion(MaterialInput mi, vec2 uv, vec3 viewTS) {
    vec2 dx = dFdx(uv), dy = dFdy(uv);
    float steps = mix(32.0, 8.0, clamp(abs(viewTS.z), 0.0, 1.0));
    float layer = 1.0 / steps;
    // UV v grows down the image while tangent-space y is image up.
    vec2 shift = vec2(viewTS.x, -viewTS.y) / max(viewTS.z, 0.15) * mi.scalar;
    vec2 delta = shift / steps;
    vec2 cur = uv;
    float depth = 0.0;
    float mapDepth = 1.0 - SampleHeight(mi, cur, dx, dy);
    for (int i = 0; i < 32 && depth < mapDepth; ++i) {
        cur -= delta;
        depth += layer;
        mapDepth = 1.0 - SampleHeight(mi, cur, dx, dy);
    }
    // Interpolate between the last two layers for a smooth intersection.
    vec2 prev = cur + delta;
    float after = mapDepth - depth;
    float before = (1.0 - SampleHeight(mi, prev, dx, dy)) - (depth - layer);
    float w = after / (after - before);
    return mix(cur, prev, clamp(w, 0.0, 1.0));
}

// Bump mapping: the tangent-space normal of the bump height field. The slope
// is measured per texel (strength = height change per texel, like a
// normal-from-height filter), so it does not depend on world scale or UV
// tiling. Screen derivatives of the height and of the texel coordinate give
// the height gradient over the texture by the chain rule.
vec3 EvaluateBumpNormal(MaterialInput mi, vec2 uv) {
    float h = EvaluateInput(mi, uv).x;
    vec2 tuv = uv * mi.tilingOffset.xy + mi.tilingOffset.zw;
    vec2 texel = tuv * vec2(textureSize(materialTextures[nonuniformEXT(mi.texIndex)], 0));
    vec2 dpx = dFdx(texel), dpy = dFdy(texel);
    float dhx = dFdx(h), dhy = dFdy(h);
    // Solve [dpx dpy]^T * g = [dhx dhy] for g = (dh/du, dh/dv) in texels.
    float det = dpx.x * dpy.y - dpx.y * dpy.x;
    if (abs(det) < 1e-8) return vec3(0.0, 0.0, 1.0);
    vec2 g = vec2(dhx * dpy.y - dhy * dpx.y, dpx.x * dhy - dpy.x * dhx) / det;
    g *= mi.scalar;
    // Tangent-space y is image up while v grows down the image.
    return normalize(vec3(-g.x, g.y, 1.0));
}

// uv: base UV; posWS: world position (reserved for graph-generated
// world-position expressions); normalWS / tangentWS: interpolated
// world-space vertex normal and tangent (w = handedness, bitangent =
// cross(N, T) * w points image-up); viewWS: surface-to-eye direction;
// frontFacing: false on the back of a two-sided surface (the normal flips).
MaterialSample EvaluateMaterial(uint materialIndex, vec2 uv, vec3 posWS, vec3 normalWS,
                                vec4 tangentWS, vec3 viewWS, bool frontFacing) {
    Material m = materials[materialIndex];
    MaterialSample s;

    vec3 N = normalize(normalWS);
    if (!frontFacing) N = -N;
    vec3 T = tangentWS.xyz - N * dot(N, tangentWS.xyz);
    float tLen = length(T);
    bool hasFrame = tLen >= 1e-5;
    T = hasFrame ? T / tLen : vec3(1.0, 0.0, 0.0);
    vec3 B = cross(N, T) * (tangentWS.w < 0.0 ? -1.0 : 1.0);

    // Height: parallax occlusion displaces the UV every other input reads.
    MaterialInput height = m.inputs[kInputHeight];
    if (hasFrame && MaterialHasTexture(height) && height.scalar > 0.0) {
        vec3 V = normalize(viewWS);
        uv = ParallaxOcclusion(height, uv, vec3(dot(V, T), dot(V, B), dot(V, N)));
    }

    s.baseColor = EvaluateInput(m.inputs[kInputBaseColor], uv).rgb;
    s.metallic = clamp(EvaluateInput(m.inputs[kInputMetallic], uv).x, 0.0, 1.0);
    s.specular = clamp(EvaluateInput(m.inputs[kInputSpecular], uv).x, 0.0, 1.0);
    s.roughness = clamp(EvaluateInput(m.inputs[kInputRoughness], uv).x, 0.0, 1.0);
    s.ao = clamp(EvaluateInput(m.inputs[kInputAO], uv).x, 0.0, 1.0) *
           clamp(EvaluateInput(m.inputs[kInputCavity], uv).x, 0.0, 1.0);
    s.opacityMask = EvaluateInput(m.inputs[kInputOpacityMask], uv).x;
    MaterialInput emissive = m.inputs[kInputEmissive];
    s.emissive = max(EvaluateInput(emissive, uv).rgb * emissive.scalar, vec3(0.0));

    if (!hasFrame) {
        s.normalWS = N;
    } else {
        vec3 n = EvaluateTangentNormal(m.inputs[kInputNormal], uv);
        MaterialInput bump = m.inputs[kInputBump];
        if (MaterialHasTexture(bump) && bump.scalar > 0.0) {
            // Whiteout blend: the bump detail rides on the normal map.
            vec3 d = EvaluateBumpNormal(bump, uv);
            n = normalize(vec3(n.xy + d.xy, n.z * d.z));
        }
        s.normalWS = normalize(T * n.x + B * n.y + N * n.z);
    }
    return s;
}

#endif  // LUMA_MATERIAL_COMMON_GLSL
