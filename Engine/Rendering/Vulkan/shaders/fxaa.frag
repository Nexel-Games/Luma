#version 450

// FXAA 3.11 (Timothy Lottes) — reads the scene color texture and applies
// fast approximate anti-aliasing to smooth geometry and texture edges.
// The vertex shader is the same fullscreen triangle (deferred_lighting.vert).

layout(binding = 0) uniform sampler2D sceneColor;

layout(push_constant) uniform Push {
    vec2 resolution;  // viewport width, height
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

// --- FXAA3.11 constants ---
const float FXAA_REDUCE_MIN   = 1.0 / 128.0;
const float FXAA_REDUCE_MUL   = 1.0 / 8.0;
const float FXAA_SPAN_MAX     = 8.0;

float rgb2luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec2 texelSize = 1.0 / pc.resolution;

    // Sample center and 4 neighbors
    vec3 rgbM  = texture(sceneColor, vUV).rgb;
    float lumaM  = rgb2luma(rgbM);

    float lumaN  = rgb2luma(texture(sceneColor, vUV + vec2( 0, -1) * texelSize).rgb);
    float lumaS  = rgb2luma(texture(sceneColor, vUV + vec2( 0,  1) * texelSize).rgb);
    float lumaE  = rgb2luma(texture(sceneColor, vUV + vec2( 1,  0) * texelSize).rgb);
    float lumaW  = rgb2luma(texture(sceneColor, vUV + vec2(-1,  0) * texelSize).rgb);

    // Find the highest and lowest luma
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));

    // Edge direction
    float lumaRange = lumaMax - lumaMin;

    // If the luma range is below the threshold, no edge — skip
    if (lumaRange < max(FXAA_REDUCE_MIN, lumaMax * FXAA_REDUCE_MUL)) {
        outColor = vec4(rgbM, 1.0);
        return;
    }

    // Sample diagonal neighbors
    float lumaNW = rgb2luma(texture(sceneColor, vUV + vec2(-1, -1) * texelSize).rgb);
    float lumaNE = rgb2luma(texture(sceneColor, vUV + vec2( 1, -1) * texelSize).rgb);
    float lumaSW = rgb2luma(texture(sceneColor, vUV + vec2(-1,  1) * texelSize).rgb);
    float lumaSE = rgb2luma(texture(sceneColor, vUV + vec2( 1,  1) * texelSize).rgb);

    // Compute edge direction using Sobel-like operators
    float edgeH = abs(-2.0 * lumaW + lumaNW + lumaSW) +
                  abs(-2.0 * lumaM + lumaN  + lumaS ) * 2.0 +
                  abs(-2.0 * lumaE + lumaNE + lumaSE);
    float edgeV = abs(-2.0 * lumaN + lumaNW + lumaNE) +
                  abs(-2.0 * lumaM + lumaW  + lumaE ) * 2.0 +
                  abs(-2.0 * lumaS + lumaSW + lumaSE);

    bool isHorizontal = (edgeH >= edgeV);

    // Select the two texels to step in the edge direction
    float stepLength = isHorizontal ? texelSize.y : texelSize.x;
    float luma1 = isHorizontal ? lumaN : lumaW;
    float luma2 = isHorizontal ? lumaS : lumaE;

    // Compute gradient
    float gradient1 = luma1 - lumaM;
    float gradient2 = luma2 - lumaM;

    // Choose the steepest gradient direction
    bool isGradient1Steeper = abs(gradient1) >= abs(gradient2);
    float stepDir = isGradient1Steeper ? -1.0 : 1.0;

    // If the steepest gradient is not steep enough, skip
    float lumaLocalAverage;
    if (isGradient1Steeper) {
        lumaLocalAverage = (luma1 + lumaM) * 0.5;
    } else {
        lumaLocalAverage = (luma2 + lumaM) * 0.5;
    }

    // Compute the blend factor based on the gradient
    float subpixelOffset = clamp(abs(lumaM - lumaLocalAverage) / lumaRange, 0.0, 1.0);
    float blendFactor = smoothstep(0.0, 1.0, subpixelOffset);
    blendFactor = blendFactor * blendFactor * 0.75;

    // Trace along the edge
    vec2 traceDir = isHorizontal ? vec2(texelSize.x, 0.0) : vec2(0.0, texelSize.y);

    // Find edge boundaries
    vec2 uv1 = vUV;
    vec2 uv2 = vUV;

    if (isGradient1Steeper) {
        uv1 += stepDir * traceDir * 0.5;
    } else {
        uv1 -= stepDir * traceDir * 0.5;
    }

    // Step along the edge to find the boundary
    float lumaEnd1 = lumaM;
    float lumaEnd2 = lumaM;

    for (int i = 0; i < int(FXAA_SPAN_MAX); ++i) {
        uv1 += traceDir * stepDir * texelSize;
        lumaEnd1 = rgb2luma(texture(sceneColor, uv1).rgb);

        // Check if we've gone past the edge
        if (abs(lumaEnd1 - lumaM) >= abs(gradient1) * 0.5) break;
    }

    for (int i = 0; i < int(FXAA_SPAN_MAX); ++i) {
        uv2 -= traceDir * stepDir * texelSize;
        lumaEnd2 = rgb2luma(texture(sceneColor, uv2).rgb);

        if (abs(lumaEnd2 - lumaM) >= abs(gradient2) * 0.5) break;
    }

    // Compute final blend weight
    float distance1 = isHorizontal ? (vUV.x - uv1.x) : (vUV.y - uv1.y);
    float distance2 = isHorizontal ? (uv2.x - vUV.x) : (uv2.y - vUV.y);

    bool isCloserTo1 = abs(distance1) < abs(distance2);
    float finalLuma = isCloserTo1 ? lumaEnd1 : lumaEnd2;

    // Only apply FXAA if the edge is on the same side as the center
    if (((lumaM - luma1) * (lumaM - luma2)) < 0.0) {
        // Blend the final color
        vec2 finalUV = isCloserTo1 ? uv1 : uv2;
        vec3 finalColor = texture(sceneColor, finalUV).rgb;
        outColor = vec4(mix(rgbM, finalColor, blendFactor), 1.0);
    } else {
        outColor = vec4(rgbM, 1.0);
    }
}
