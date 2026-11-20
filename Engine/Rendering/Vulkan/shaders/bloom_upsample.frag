#version 450

// Bloom upsample: 3x3 tent filter of the smaller mip. The pipeline blends this
// *additively* onto the next-larger mip (which still holds its own downsample),
// so each level's blur accumulates up the chain and mip 0 ends up with the
// full multi-scale bloom.

layout(set = 0, binding = 0) uniform sampler2D inputTex;
layout(push_constant) uniform Params {
    vec2 texelSize;     // 1 / resolution of the (smaller) input mip
    float unused0;
    float unused1;
    float filterRadius; // tent radius in input texels
} params;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

void main() {
    vec2 t = params.texelSize * params.filterRadius;

    vec3 a = texture(inputTex, uv + vec2(-t.x,  t.y)).rgb;
    vec3 b = texture(inputTex, uv + vec2( 0.0,  t.y)).rgb;
    vec3 c = texture(inputTex, uv + vec2( t.x,  t.y)).rgb;
    vec3 d = texture(inputTex, uv + vec2(-t.x,  0.0)).rgb;
    vec3 e = texture(inputTex, uv).rgb;
    vec3 f = texture(inputTex, uv + vec2( t.x,  0.0)).rgb;
    vec3 g = texture(inputTex, uv + vec2(-t.x, -t.y)).rgb;
    vec3 h = texture(inputTex, uv + vec2( 0.0, -t.y)).rgb;
    vec3 i = texture(inputTex, uv + vec2( t.x, -t.y)).rgb;

    // 1-2-1 tent weights (sum = 16).
    vec3 col = e * 4.0 + (b + d + f + h) * 2.0 + (a + c + g + i);
    fragColor = vec4(col * (1.0 / 16.0), 1.0);
}
