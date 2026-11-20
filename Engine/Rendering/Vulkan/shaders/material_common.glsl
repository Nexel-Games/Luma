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
//   GpuMaterial          -> Material (16 + 8 * 64 bytes)
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
const uint kInputCount = 8u;

// MaterialInput.flags
const uint kFlagHasTexture = 1u;
const uint kFlagFlipGreen = 2u;

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
    MaterialInput inputs[8];
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
    float ao;
    float opacityMask;
};

bool MaterialHasTexture(MaterialInput mi) {
    return (mi.flags & kFlagHasTexture) != 0u;
}

// The texture sample, channel-selected (scalars broadcast) and tinted.
vec4 SampleMaterialTexture(MaterialInput mi, vec2 uv) {
    vec2 tuv = uv * mi.tilingOffset.xy + mi.tilingOffset.zw;
    vec4 t = texture(materialTextures[nonuniformEXT(mi.texIndex)], tuv);
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

// normalWS / tangentWS: interpolated world-space vertex normal and tangent
// (w = handedness, bitangent = cross(N, T) * w points image-up).
// frontFacing: false on the back of a two-sided surface (the normal flips).
MaterialSample EvaluateMaterial(uint materialIndex, vec2 uv, vec3 normalWS,
                                vec4 tangentWS, bool frontFacing) {
    Material m = materials[materialIndex];
    MaterialSample s;
    s.baseColor = EvaluateInput(m.inputs[kInputBaseColor], uv).rgb;
    s.metallic = clamp(EvaluateInput(m.inputs[kInputMetallic], uv).x, 0.0, 1.0);
    s.specular = clamp(EvaluateInput(m.inputs[kInputSpecular], uv).x, 0.0, 1.0);
    s.roughness = clamp(EvaluateInput(m.inputs[kInputRoughness], uv).x, 0.0, 1.0);
    s.ao = clamp(EvaluateInput(m.inputs[kInputAO], uv).x, 0.0, 1.0);
    s.opacityMask = EvaluateInput(m.inputs[kInputOpacityMask], uv).x;
    MaterialInput emissive = m.inputs[kInputEmissive];
    s.emissive = max(EvaluateInput(emissive, uv).rgb * emissive.scalar, vec3(0.0));

    vec3 N = normalize(normalWS);
    if (!frontFacing) N = -N;
    vec3 T = tangentWS.xyz - N * dot(N, tangentWS.xyz);
    float tLen = length(T);
    if (tLen < 1e-5) {
        s.normalWS = N;
    } else {
        T /= tLen;
        vec3 B = cross(N, T) * (tangentWS.w < 0.0 ? -1.0 : 1.0);
        vec3 n = EvaluateTangentNormal(m.inputs[kInputNormal], uv);
        s.normalWS = normalize(T * n.x + B * n.y + N * n.z);
    }
    return s;
}

#endif  // LUMA_MATERIAL_COMMON_GLSL
