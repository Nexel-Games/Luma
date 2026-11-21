// material_params.glsl - what generated material code (Luma::MaterialCompiler)
// is written against: descriptor sets 1 (textures + samplers) and 2
// (parameter blocks), the per-draw push constants, the MaterialParameters
// `P` the expressions read, and the M* helpers they call.
//
// Parameter block (set 2): one vec4 per slot at lumaPush.paramOffset. Uniform
// expressions are computed on the CPU into their slot (MP(slot), scalars in
// .x); a texture slot holds (image index, sampler index, bound, 0) as raw
// bits. Image indices address lumaTextures (2i = linear view, 2i+1 = sRGB
// view of texture i) or lumaCubes; sampler indices the immutable sampler
// table (filter x address [+ anisotropic]).

#ifndef LUMA_MATERIAL_PARAMS_GLSL
#define LUMA_MATERIAL_PARAMS_GLSL

#include "material_frame.glsl"

layout(set = 1, binding = 0) uniform texture2D lumaTextures[];
layout(set = 1, binding = 1) uniform sampler lumaSamplers[12];
layout(set = 1, binding = 2) uniform textureCube lumaCubes[];

layout(std430, set = 2, binding = 0) readonly buffer LumaParams {
    vec4 lumaParams[];
};

// 128 bytes: the whole push-constant budget.
layout(push_constant) uniform LumaPush {
    mat4 model;
    vec4 normalMatrix[3];   // inverse-transpose of the model's 3x3 (columns)
    uint paramOffset;       // first vec4 of this draw's parameter block
    uint objectId;          // index into lumaObjects
    uint flags;             // reserved for pass-specific per-draw bits
    float opacityMaskClip;  // Masked materials (instances may override it)
} lumaPush;

#define MP(slot) lumaParams[lumaPush.paramOffset + uint(slot)]

// --- Textures --------------------------------------------------------------------

uint LumaImage(uint slot) { return floatBitsToUint(MP(slot).x); }
uint LumaSampler(uint slot) { return min(floatBitsToUint(MP(slot).y), 11u); }

#define LUMA_TEX2D(slot) sampler2D(lumaTextures[nonuniformEXT(LumaImage(slot))], lumaSamplers[LumaSampler(slot)])
#define LUMA_TEXCUBE(slot) samplerCube(lumaCubes[nonuniformEXT(LumaImage(slot))], lumaSamplers[LumaSampler(slot)])

vec4 MSample2D(uint slot, vec2 uv) { return texture(LUMA_TEX2D(slot), uv); }
vec4 MSample2DLevel(uint slot, vec2 uv, float level) { return textureLod(LUMA_TEX2D(slot), uv, level); }
vec4 MSample2DGrad(uint slot, vec2 uv, vec2 dx, vec2 dy) { return textureGrad(LUMA_TEX2D(slot), uv, dx, dy); }
vec4 MSampleCube(uint slot, vec3 dir) { return texture(LUMA_TEXCUBE(slot), dir); }
vec4 MSampleCubeLevel(uint slot, vec3 dir, float level) { return textureLod(LUMA_TEXCUBE(slot), dir, level); }
#ifdef LUMA_STAGE_PIXEL  // implicit-LOD bias exists only in fragment shaders
vec4 MSample2DBias(uint slot, vec2 uv, float bias) { return texture(LUMA_TEX2D(slot), uv, bias); }
vec4 MSampleCubeBias(uint slot, vec3 dir, float bias) { return texture(LUMA_TEXCUBE(slot), dir, bias); }
#endif
vec2 MTexSize(uint slot) { return vec2(textureSize(LUMA_TEX2D(slot), 0)); }

// Tangent-space normal from a normal map texel (xy stored, z rebuilt, so
// two-channel (BC5) and three-channel maps decode alike).
vec4 MUnpackNormal(vec4 texel) {
    vec2 xy = texel.xy * 2.0 - 1.0;
    return vec4(xy, sqrt(clamp(1.0 - dot(xy, xy), 0.0, 1.0)), 1.0);
}

// --- Scene textures (post-process; translucency: colour + depth) -----------

vec4 MSceneTexture(int id, vec2 uv) {
    // Ids: SceneColor, SceneDepth, WorldNormal, BaseColor, Roughness, Metallic,
    // Specular, CustomData, PostProcessInput0 (Scene Texture node order).
    if (id == 0) return vec4(texture(lumaSceneTextures[kLumaSceneColor], uv).rgb, 1.0);
    if (id == 1) return vec4(LumaLinearDepth(uv, texture(lumaSceneTextures[kLumaSceneDepth], uv).r));
    if (id == 2) return vec4(normalize(texture(lumaSceneTextures[kLumaGBufferB], uv).rgb * 2.0 - 1.0), 1.0);
    if (id == 3) return vec4(texture(lumaSceneTextures[kLumaGBufferA], uv).rgb, 1.0);
    if (id == 4) return vec4(texture(lumaSceneTextures[kLumaGBufferB], uv).a);
    if (id == 5) return vec4(texture(lumaSceneTextures[kLumaGBufferC], uv).r);
    if (id == 6) return vec4(texture(lumaSceneTextures[kLumaGBufferC], uv).g);
    if (id == 7) return texture(lumaSceneTextures[kLumaGBufferCustom], uv);
    return texture(lumaSceneTextures[kLumaPostProcessInput0], uv);
}

vec2 MSceneTextureSize(int id) {
    const int map[9] = int[](kLumaSceneColor, kLumaSceneDepth, kLumaGBufferB, kLumaGBufferA, kLumaGBufferB,
                             kLumaGBufferC, kLumaGBufferC, kLumaGBufferCustom, kLumaPostProcessInput0);
    return vec2(textureSize(lumaSceneTextures[map[clamp(id, 0, 8)]], 0));
}

float MSceneDepth(vec2 uv) { return LumaLinearDepth(uv, texture(lumaSceneTextures[kLumaSceneDepth], uv).r); }
vec3 MSceneColor(vec2 uv) { return texture(lumaSceneTextures[kLumaSceneColor], uv).rgb; }

// --- What expressions read ---------------------------------------------------------

struct MaterialParameters {
    vec3 worldPos;          // after World Position Offset (pixel stage)
    vec3 worldPosNoOffset;
    vec3 worldNormal;       // vertex normal (flipped on back faces)
    vec3 worldTangent;
    vec3 worldBitangent;
    vec3 pixelNormal;       // after the material's Normal (vertex normal before)
    vec2 texcoords[8];
    vec4 vertexColor;
    vec3 cameraVector;      // surface -> camera, normalized
    vec2 screenUV;
    vec2 pixelPos;
    float pixelDepth;       // view-space depth
    vec3 objectPos;
    float objectRadius;
    vec3 objectBounds;      // local-space half extent
    mat4 localToWorld;
    mat4 worldToLocal;
};
MaterialParameters P;

void LumaSetPixelNormal(vec3 tangentNormal) {
    P.pixelNormal = normalize(tangentNormal.x * P.worldTangent + tangentNormal.y * P.worldBitangent +
                              tangentNormal.z * P.worldNormal);
}

// --- What materials produce (Material Output pins; defaults = pin defaults) --

struct MaterialVertex {
    vec3 worldPositionOffset;
    vec2 customUV[8];
    uint customUVMask;  // bit i: customUV[i] replaces texture coordinate i
};

MaterialVertex LumaDefaultVertex() {
    MaterialVertex v;
    v.worldPositionOffset = vec3(0.0);
    for (int i = 0; i < 8; ++i) v.customUV[i] = vec2(0.0);
    v.customUVMask = 0u;
    return v;
}

struct MaterialPixel {
    vec3 baseColor;
    float metallic;
    float specular;
    float roughness;
    float anisotropy;
    vec3 emissive;
    float opacity;
    float opacityMask;
    vec3 normal;   // tangent space
    vec3 tangent;  // tangent space
    vec3 subsurfaceColor;
    float customData0;
    float customData1;
    float ao;
    float refraction;
    float pixelDepthOffset;
};

MaterialPixel LumaDefaultPixel() {
    MaterialPixel m;
    m.baseColor = vec3(0.0);
    m.metallic = 0.0;
    m.specular = 0.5;
    m.roughness = 0.5;
    m.anisotropy = 0.0;
    m.emissive = vec3(0.0);
    m.opacity = 1.0;
    m.opacityMask = 1.0;
    m.normal = vec3(0.0, 0.0, 1.0);
    m.tangent = vec3(1.0, 0.0, 0.0);
    m.subsurfaceColor = vec3(1.0);
    m.customData0 = 0.0;
    m.customData1 = 0.0;
    m.ao = 1.0;
    m.refraction = 1.0;
    m.pixelDepthOffset = 0.0;
    return m;
}

// Varyings between material_template.vert and the pixel templates.
#define LUMA_VARYING_WORLD_POS 0
#define LUMA_VARYING_WORLD_POS_NO_OFFSET 1
#define LUMA_VARYING_NORMAL 2
#define LUMA_VARYING_TANGENT 3
#define LUMA_VARYING_UV 4  // 4 x vec4 (two texture coordinates each)
#define LUMA_VARYING_COLOR 8
#define LUMA_INTERP_LOCATION 9  // generated lumaInterp<N> varyings

#endif  // LUMA_MATERIAL_PARAMS_GLSL
