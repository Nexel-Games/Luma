// material_frame.glsl - descriptor set 0 of the shared material pipeline
// layout: per-view data every material pass reads.
//
//   binding 0  LumaFrameUBO        camera matrices, time, viewport
//   binding 1  LumaObjects         per-object data (bounds, world-to-local), by pushed objectId
//   binding 2  lumaSceneTextures   scene buffers for post-process / translucent materials
//   binding 3+ lighting resources  (lighting_common.glsl with LUMA_LIGHTING_BINDING 3)
//
// Parameter-collection values reach shaders through each material's
// parameter block (the CPU preshader folds them), so set 0 has no
// collection buffer.

#ifndef LUMA_MATERIAL_FRAME_GLSL
#define LUMA_MATERIAL_FRAME_GLSL

layout(std140, set = 0, binding = 0) uniform LumaFrameUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    mat4 invViewProj;
    mat4 prevViewProj;
    vec4 camPos;    // xyz = camera position
    vec4 time;      // x = seconds, y = delta seconds, z = frame index
    vec4 viewport;  // x = width, y = height, z = 1/width, w = 1/height
} lumaFrame;

struct LumaObject {
    mat4 worldToLocal;
    vec4 boundsCenterRadius;  // xyz = world-space bounds centre, w = bounding-sphere radius
    vec4 boundsExtent;        // xyz = local-space bounds half extent
};

layout(std430, set = 0, binding = 1) readonly buffer LumaObjects {
    LumaObject lumaObjects[];
};

// Scene buffers (indices of lumaSceneTextures):
const int kLumaSceneColor = 0;         // HDR scene colour copy (linear)
const int kLumaSceneDepth = 1;         // device depth
const int kLumaGBufferA = 2;           // RT0: base colour (sRGB view, read linear), a = shading model / 255
const int kLumaGBufferB = 3;           // RT1: normal * 0.5 + 0.5, a = roughness
const int kLumaGBufferC = 4;           // RT2: metallic, specular, AO
const int kLumaGBufferCustom = 5;      // RT4: shading-model custom data
const int kLumaPostProcessInput0 = 6;  // the post chain's current colour
layout(set = 0, binding = 2) uniform sampler2D lumaSceneTextures[7];

#define LUMA_LIGHTING_SET 0
#define LUMA_LIGHTING_BINDING 3

float MaterialTime() { return lumaFrame.time.x; }
float MaterialDeltaTime() { return lumaFrame.time.y; }
vec3 MaterialCameraPosition() { return lumaFrame.camPos.xyz; }
vec2 MaterialViewSize() { return lumaFrame.viewport.xy; }
mat4 MaterialViewMatrix() { return lumaFrame.view; }

// View-space distance in front of the camera of a device depth at screen UV.
float LumaLinearDepth(vec2 uv, float deviceDepth) {
    vec4 view = inverse(lumaFrame.proj) * vec4(uv * 2.0 - 1.0, deviceDepth, 1.0);
    return -view.z / view.w;
}

// World position of a device depth at screen UV.
vec3 LumaWorldFromDepth(vec2 uv, float deviceDepth) {
    vec4 world = lumaFrame.invViewProj * vec4(uv * 2.0 - 1.0, deviceDepth, 1.0);
    return world.xyz / world.w;
}

#endif  // LUMA_MATERIAL_FRAME_GLSL
