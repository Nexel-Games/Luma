#ifndef LUMA_SKYLIGHT_COMMON_GLSL
#define LUMA_SKYLIGHT_COMMON_GLSL

// Shared by the sky light capture compute passes (capture, mip generation,
// GGX specular convolution, SH diffuse projection). Modeled on Unreal's
// real-time SkyLight capture (ReflectionEnvironmentRealTimeCapture).

const float SL_PI = 3.14159265358979;

// Push constants shared by every sky light compute pass.
layout(push_constant) uniform SkyLightPush {
    int mip;         // output mip level
    int numMips;     // mips in the cube chain
    float roughness; // GGX roughness of the output mip (filter pass)
    int size;        // output mip edge length in texels
} pc;

// World direction of the center of a cube texel. `uv` in [-1,1]; face order
// +X,-X,+Y,-Y,+Z,-Z. Inverse of Vulkan's cube face selection (sc/tc table), so
// a samplerCube lookup with this direction returns this exact texel.
vec3 CubeTexelDir(uint face, vec2 uv) {
    vec3 d;
    if (face == 0u)      d = vec3( 1.0, -uv.y, -uv.x);
    else if (face == 1u) d = vec3(-1.0, -uv.y,  uv.x);
    else if (face == 2u) d = vec3( uv.x,  1.0,  uv.y);
    else if (face == 3u) d = vec3( uv.x, -1.0, -uv.y);
    else if (face == 4u) d = vec3( uv.x, -uv.y,  1.0);
    else                 d = vec3(-uv.x, -uv.y, -1.0);
    return normalize(d);
}

vec3 TexelDirection(uvec3 id, int size) {
    vec2 uv = (vec2(id.xy) + 0.5) / float(size) * 2.0 - 1.0;
    return CubeTexelDir(id.z, uv);
}

// Low-discrepancy 2D sequence.
vec2 Hammersley(uint i, uint n) {
    return vec2(float(i) / float(n), float(bitfieldReverse(i)) * 2.3283064365386963e-10);
}

// Orthonormal basis around N (Frisvad / Duff et al.).
mat3 TangentBasis(vec3 n) {
    float s = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float b = n.x * n.y * a;
    vec3 t = vec3(1.0 + s * n.x * n.x * a, s * b, -s * n.x);
    vec3 bt = vec3(b, s + n.y * n.y * a, -n.y);
    return mat3(t, bt, n);
}

#endif  // LUMA_SKYLIGHT_COMMON_GLSL
