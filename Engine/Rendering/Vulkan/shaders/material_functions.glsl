// Helper functions called by generated material code (Luma::MaterialCompiler).
//
// Included by the material pass templates after material_params.glsl and
// material_frame.glsl, which provide:
//   MaterialParameters P   (global) worldPos, worldPosNoOffset, worldNormal,
//                          worldTangent, worldBitangent, pixelNormal,
//                          texcoords[8], vertexColor, cameraVector, screenUV,
//                          pixelPos, pixelDepth, objectPos, objectRadius,
//                          objectBounds, localToWorld, worldToLocal
//   MaterialViewMatrix(), MaterialTime(), ...
//   MSample2DGrad(slot, uv, ddx, ddy), MTexSize(slot)

#ifndef LUMA_MATERIAL_FUNCTIONS_GLSL
#define LUMA_MATERIAL_FUNCTIONS_GLSL

// --- Math -------------------------------------------------------------------

// HLSL/Unreal fmod: the result has the sign of a (GLSL mod uses floor).
float LumaFmod(float a, float b) { return a - b * trunc(a / b); }
vec2 LumaFmod(vec2 a, vec2 b) { return a - b * trunc(a / b); }
vec3 LumaFmod(vec3 a, vec3 b) { return a - b * trunc(a / b); }
vec4 LumaFmod(vec4 a, vec4 b) { return a - b * trunc(a / b); }

// --- Coordinates ----------------------------------------------------------------

// Un-mirrors UVs of mirrored geometry halves (triangle wave over [0, 2]).
vec2 LumaUnMirror(vec2 uv, bvec2 axes) {
    vec2 folded = 1.0 - abs(1.0 - 2.0 * fract(uv * 0.5));
    return vec2(axes.x ? folded.x : uv.x, axes.y ? folded.y : uv.y);
}

// Rotates `uv` around `center` by time * speed radians.
vec2 LumaRotator(vec2 uv, float time, vec2 center, float speed) {
    float a = time * speed;
    float s = sin(a), c = cos(a);
    vec2 d = uv - center;
    return center + vec2(d.x * c - d.y * s, d.x * s + d.y * c);
}

// --- Shading helpers ----------------------------------------------------------

// Schlick Fresnel on dot(N, V): 1 at grazing angles, `baseReflect` facing.
float LumaFresnel(float exponent, float baseReflect, vec3 normal, vec3 toCamera) {
    float ndv = clamp(dot(normalize(normal), normalize(toCamera)), 0.0, 1.0);
    float k = pow(1.0 - ndv, max(exponent, 0.0));
    return k + baseReflect * (1.0 - k);
}

// Reoriented normal mapping: `detail` layered onto `base` (both tangent space).
vec3 LumaBlendAngleCorrectedNormals(vec3 base, vec3 detail) {
    vec3 t = base + vec3(0.0, 0.0, 1.0);
    vec3 u = detail * vec3(-1.0, -1.0, 1.0);
    return normalize(t * dot(t, u) / max(t.z, 1e-5) - u);
}

// The surface -> camera direction in tangent space.
vec3 LumaViewTangent() {
    vec3 v = normalize(P.cameraVector);
    return vec3(dot(v, P.worldTangent), dot(v, P.worldBitangent), dot(v, P.worldNormal));
}

// Cheap parallax: shifts UVs along the view by (height - plane) * ratio.
vec2 LumaBumpOffset(vec2 uv, float height, float ratio, float referencePlane) {
    vec3 v = LumaViewTangent();
    // UV v grows down the image while tangent-space y is image up.
    return uv + vec2(v.x, -v.y) * ((height - referencePlane) * ratio);
}

float LumaHeightSample(uint slot, vec2 uv, vec2 dx, vec2 dy, int channel) {
    vec4 t = MSample2DGrad(slot, uv, dx, dy);
    return channel == 1 ? t.g : channel == 2 ? t.b : channel == 3 ? t.a : t.r;
}

// Parallax occlusion mapping: ray-marches the height field (1 = surface,
// 0 = deepest) in tangent space. xy = displaced UVs, z = depth below the
// surface in world units (for Pixel Depth Offset).
vec3 LumaParallaxOcclusion(uint slot, vec2 uv, float ratio, float minSteps, float maxSteps, int channel) {
    vec3 viewTS = LumaViewTangent();
    vec2 dx = dFdx(uv), dy = dFdy(uv);
    float steps = clamp(mix(maxSteps, minSteps, clamp(abs(viewTS.z), 0.0, 1.0)), 1.0, 256.0);
    int count = int(steps);
    float layer = 1.0 / steps;
    vec2 shift = vec2(viewTS.x, -viewTS.y) / max(viewTS.z, 0.15) * ratio;
    vec2 delta = shift / steps;
    vec2 cur = uv;
    float depth = 0.0;
    float mapDepth = 1.0 - LumaHeightSample(slot, cur, dx, dy, channel);
    for (int i = 0; i < count && depth < mapDepth; ++i) {
        cur -= delta;
        depth += layer;
        mapDepth = 1.0 - LumaHeightSample(slot, cur, dx, dy, channel);
    }
    // Interpolate between the last two layers for a smooth intersection.
    vec2 prev = cur + delta;
    float after = mapDepth - depth;
    float before = (1.0 - LumaHeightSample(slot, prev, dx, dy, channel)) - (depth - layer);
    float w = clamp(after / (after - before), 0.0, 1.0);
    vec2 hit = mix(cur, prev, w);
    float hitDepth = mix(depth, depth - layer, w);
    return vec3(hit, hitDepth * ratio / max(abs(viewTS.z), 0.15));
}

// Tangent-space normal from a height texture's per-texel slope, independent
// of world scale and UV tiling (screen derivatives + chain rule).
vec3 LumaNormalFromHeight(uint slot, vec2 uv, float intensity) {
    vec2 dx = dFdx(uv), dy = dFdy(uv);
    float h = LumaHeightSample(slot, uv, dx, dy, 0);
    vec2 texel = uv * MTexSize(slot);
    vec2 dpx = dFdx(texel), dpy = dFdy(texel);
    float dhx = dFdx(h), dhy = dFdy(h);
    float det = dpx.x * dpy.y - dpx.y * dpy.x;
    if (abs(det) < 1e-8) return vec3(0.0, 0.0, 1.0);
    vec2 g = vec2(dhx * dpy.y - dhy * dpx.y, dpx.x * dhy - dpy.x * dhx) / det * intensity;
    return normalize(vec3(-g.x, g.y, 1.0));
}

// --- Space transforms ------------------------------------------------------------

// Directions. Spaces: 0 Tangent, 1 Local, 2 World, 3 View.
vec3 LumaToWorldVector(vec3 v, int space) {
    if (space == 0) return v.x * P.worldTangent + v.y * P.worldBitangent + v.z * P.worldNormal;
    if (space == 1) return mat3(P.localToWorld) * v;
    if (space == 3) return transpose(mat3(MaterialViewMatrix())) * v;
    return v;
}

vec3 LumaFromWorldVector(vec3 v, int space) {
    if (space == 0) return vec3(dot(v, P.worldTangent), dot(v, P.worldBitangent), dot(v, P.worldNormal));
    if (space == 1) return mat3(P.worldToLocal) * v;
    if (space == 3) return mat3(MaterialViewMatrix()) * v;
    return v;
}

vec3 LumaTransformVector(vec3 v, int source, int destination) {
    return LumaFromWorldVector(LumaToWorldVector(v, source), destination);
}

// Positions. Spaces: 0 Local, 1 World, 2 View.
vec3 LumaTransformPosition(vec3 p, int source, int destination) {
    vec3 world = p;
    if (source == 0) world = (P.localToWorld * vec4(p, 1.0)).xyz;
    if (source == 2) world = (inverse(MaterialViewMatrix()) * vec4(p, 1.0)).xyz;
    if (destination == 0) return (P.worldToLocal * vec4(world, 1.0)).xyz;
    if (destination == 2) return (MaterialViewMatrix() * vec4(world, 1.0)).xyz;
    return world;
}

#endif  // LUMA_MATERIAL_FUNCTIONS_GLSL
