#version 450

// Unreal-style selection outline, drawn over the final display image (after
// FXAA, before the gizmo overlay): a ring `widthPx` wide outside the selected
// actor's silhouette (selection mask r). Where the actor is hidden behind
// other geometry (mask g = 0) the ring is drawn at `occludedAlpha`. Output is
// display-encoded (the target already holds sRGB-encoded values) and
// alpha-blended.

layout(set = 0, binding = 0) uniform sampler2D selectionMask;

layout(push_constant) uniform Push {
    vec4 color;          // display-encoded outline color
    float widthPx;
    float occludedAlpha;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(selectionMask, 0);
    if (texelFetch(selectionMask, p, 0).r > 0.5) discard;  // inside the actor

    // Search a fixed window (widths up to kMaxRadius - 1 px).
    const int kMaxRadius = 4;
    float nearest = 1e9;
    float nearestVisible = 1e9;
    for (int y = -kMaxRadius; y <= kMaxRadius; ++y) {
        for (int x = -kMaxRadius; x <= kMaxRadius; ++x) {
            ivec2 q = clamp(p + ivec2(x, y), ivec2(0), size - 1);
            vec2 m = texelFetch(selectionMask, q, 0).rg;
            float d = length(vec2(x, y));
            if (m.r > 0.5) {
                nearest = min(nearest, d);
                if (m.g > 0.5) nearestVisible = min(nearestVisible, d);
            }
        }
    }
    // Coverage of the ring with a one-pixel soft outer edge.
    float ring = clamp(pc.widthPx + 0.5 - nearest, 0.0, 1.0);
    if (ring <= 0.0) discard;
    float visible = clamp(pc.widthPx + 0.5 - nearestVisible, 0.0, 1.0);
    float alpha = ring * mix(pc.occludedAlpha, 1.0, visible);
    outColor = vec4(pc.color.rgb, alpha * pc.color.a);
}
