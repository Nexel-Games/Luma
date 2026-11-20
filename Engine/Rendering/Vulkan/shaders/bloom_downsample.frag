#version 450

// Bloom downsample (physically based bloom, Jimenez 2014 "Next Generation Post
// Processing in Call of Duty"). 13 bilinear taps arranged as five overlapping
// 2x2 boxes; weights sum to exactly 1 so energy is preserved down the chain.
//
// The first pass (prefilter = 1) reads the HDR scene: it applies a soft-knee
// luminance threshold and a Karis (1/(1+luma)) average per box so isolated
// super-bright pixels (fireflies) can't dominate the bloom.

layout(set = 0, binding = 0) uniform sampler2D inputTex;
layout(push_constant) uniform Params {
    vec2 texelSize;   // 1 / source resolution
    float threshold;  // linear luminance where bloom starts
    float knee;       // soft-knee width as a fraction of threshold (0..1)
    float prefilter;  // 1 on the first downsample (threshold + Karis), else 0
} params;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// Soft-knee threshold (as in UE/Unity): a quadratic ramp around the threshold
// instead of a hard cut, so bloom fades in smoothly rather than popping.
vec3 Threshold(vec3 c) {
    float br = max(c.r, max(c.g, c.b));
    float t = params.threshold;
    float k = max(t * params.knee, 1e-5);
    float rq = clamp(br - t + k, 0.0, 2.0 * k);
    rq = (rq * rq) / (4.0 * k);
    float w = max(rq, br - t) / max(br, 1e-5);
    return c * w;
}

// Karis weight for one 2x2 box average: bright boxes count for less.
float KarisWeight(vec3 box) { return 1.0 / (1.0 + Luma(box)); }

void main() {
    float x = params.texelSize.x;
    float y = params.texelSize.y;

    // Outer 3x3 grid (2 texels apart) + inner 2x2 (1 texel apart).
    vec3 a = texture(inputTex, uv + vec2(-2.0 * x,  2.0 * y)).rgb;
    vec3 b = texture(inputTex, uv + vec2( 0.0,      2.0 * y)).rgb;
    vec3 c = texture(inputTex, uv + vec2( 2.0 * x,  2.0 * y)).rgb;
    vec3 d = texture(inputTex, uv + vec2(-2.0 * x,  0.0)).rgb;
    vec3 e = texture(inputTex, uv).rgb;
    vec3 f = texture(inputTex, uv + vec2( 2.0 * x,  0.0)).rgb;
    vec3 g = texture(inputTex, uv + vec2(-2.0 * x, -2.0 * y)).rgb;
    vec3 h = texture(inputTex, uv + vec2( 0.0,     -2.0 * y)).rgb;
    vec3 i = texture(inputTex, uv + vec2( 2.0 * x, -2.0 * y)).rgb;
    vec3 j = texture(inputTex, uv + vec2(-x,  y)).rgb;
    vec3 k = texture(inputTex, uv + vec2( x,  y)).rgb;
    vec3 l = texture(inputTex, uv + vec2(-x, -y)).rgb;
    vec3 m = texture(inputTex, uv + vec2( x, -y)).rgb;

    vec3 result;
    if (params.prefilter > 0.5) {
        a = Threshold(a); b = Threshold(b); c = Threshold(c);
        d = Threshold(d); e = Threshold(e); f = Threshold(f);
        g = Threshold(g); h = Threshold(h); i = Threshold(i);
        j = Threshold(j); k = Threshold(k); l = Threshold(l); m = Threshold(m);
        // Five 2x2 box averages with the standard weights (0.5 inner, 0.125
        // per corner box), each further scaled by its Karis weight and
        // normalized by the weight sum: a firefly-resistant weighted average.
        vec3 inner = (j + k + l + m) * 0.25;
        vec3 tl = (a + b + d + e) * 0.25;
        vec3 tr = (b + c + e + f) * 0.25;
        vec3 bl = (d + e + g + h) * 0.25;
        vec3 br = (e + f + h + i) * 0.25;
        float wi = 0.5 * KarisWeight(inner);
        float w0 = 0.125 * KarisWeight(tl);
        float w1 = 0.125 * KarisWeight(tr);
        float w2 = 0.125 * KarisWeight(bl);
        float w3 = 0.125 * KarisWeight(br);
        result = (inner * wi + tl * w0 + tr * w1 + bl * w2 + br * w3) /
                 max(wi + w0 + w1 + w2 + w3, 1e-5);
    } else {
        result = e * 0.125
               + (a + c + g + i) * 0.03125
               + (b + d + f + h) * 0.0625
               + (j + k + l + m) * 0.125;
    }
    fragColor = vec4(max(result, vec3(0.0)), 1.0);
}
