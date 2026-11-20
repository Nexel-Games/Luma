#version 450

// Final HDR -> display pass (runs every frame, bloom or not):
//   scene-referred linear HDR + bloom  ->  exposure  ->  tone map  ->  sRGB.
// Tone mapper ids match PostProcessComponent::ToneMapper:
//   0 = ACES (Hill's fitted RRT+ODT), 1 = Reinhard, 2 = AgX, 3 = Linear.

layout(set = 0, binding = 0) uniform sampler2D sceneTex;  // linear HDR
layout(set = 0, binding = 1) uniform sampler2D bloomTex;  // bloom mip 0 (HDR)
// Eye adaptation result (VulkanEyeAdaptation): exposureScale maps the scene's
// adapted average luminance to 18% grey.
layout(std430, set = 0, binding = 2) readonly buffer ExposureState {
    float exposureScale;
    float adaptedExposure;
    float targetExposure;
    float initialized;
} eye;

layout(push_constant) uniform Params {
    vec4 bloomTint;     // rgb = tint, w = intensity (pre-normalized per level)
    float exposure;     // linear exposure (manual, or compensation when auto)
    float toneMapper;   // see above
    float autoExposure; // 1 = multiply by the eye adaptation exposure
    float pad;          // 1 = view-mode visualization (bypass post)
} params;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

// --- ACES (Stephen Hill's fit of the RRT + sRGB ODT) -------------------------
const mat3 ACES_IN = mat3(
    0.59719, 0.07600, 0.02840,
    0.35458, 0.90834, 0.13383,
    0.04823, 0.01566, 0.83777);
const mat3 ACES_OUT = mat3(
     1.60475, -0.10208, -0.00327,
    -0.53108,  1.10813, -0.07276,
    -0.07367, -0.00605,  1.07602);
vec3 RRTAndODTFit(vec3 v) {
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}
vec3 ToneACES(vec3 c) {
    return clamp(ACES_OUT * RRTAndODTFit(ACES_IN * c), 0.0, 1.0);
}

// --- AgX (minimal AgX by Benjamin Wrensch; default contrast look) -----------
vec3 AgXContrast(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x +
           0.4298 * x2 + 0.1191 * x - 0.00232;
}
vec3 ToneAgX(vec3 c) {
    const mat3 AGX_IN = mat3(
        0.842479062253094, 0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772, 0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 AGX_OUT = mat3(
        1.19687900512017, -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393;
    const float maxEv = 4.026069;
    c = AGX_IN * max(c, vec3(1e-10));
    c = clamp(log2(c), minEv, maxEv);
    c = (c - minEv) / (maxEv - minEv);
    c = AgXContrast(c);
    c = AGX_OUT * c;
    // The curve output is display-encoded; return display-linear so the
    // shared sRGB encode below applies uniformly to every tone mapper.
    return pow(clamp(c, 0.0, 1.0), vec3(2.2));
}

vec3 ToneReinhard(vec3 c) { return c / (1.0 + c); }

vec3 LinearToSRGB(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}

// Interleaved gradient noise (Jimenez): cheap per-pixel dither that breaks up
// 8-bit banding in smooth gradients (sky, soft shadows) after quantization.
float IGN(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main() {
    if (params.pad > 0.5) {
        // View-mode visualization: the lighting pass already wrote linear
        // display values; only encode to sRGB (no bloom/exposure/tone map).
        fragColor = vec4(LinearToSRGB(texture(sceneTex, uv).rgb), 1.0);
        return;
    }
    vec3 hdr = texture(sceneTex, uv).rgb;
    vec3 bloom = texture(bloomTex, uv).rgb;
    hdr += bloom * params.bloomTint.rgb * params.bloomTint.w;
    float exposure = params.exposure;
    if (params.autoExposure > 0.5) exposure *= eye.exposureScale;
    hdr *= exposure;

    int mapper = int(params.toneMapper + 0.5);
    vec3 ldr;
    if (mapper == 1)      ldr = ToneReinhard(hdr);
    else if (mapper == 2) ldr = ToneAgX(hdr);
    else if (mapper == 3) ldr = clamp(hdr, 0.0, 1.0);
    else                  ldr = ToneACES(hdr);

    vec3 display = LinearToSRGB(ldr);
    display += (IGN(gl_FragCoord.xy) - 0.5) / 255.0;
    fragColor = vec4(display, 1.0);
}
