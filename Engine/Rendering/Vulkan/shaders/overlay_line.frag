#version 450

// Overlay colors are display-referred (drawn after tone mapping + FXAA).
// vColor.w = signed distance from the line center in pixels; the outermost
// `feather` pixels fade out for an anti-aliased edge.
layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Params {
    float halfWidth;  // core half width + feather (px)
    float feather;    // px
} p;

void main() {
    float d = abs(vColor.w);
    float alpha = clamp((p.halfWidth - d) / p.feather, 0.0, 1.0);
    outColor = vec4(vColor.rgb, alpha);
}
