#version 450

// Selection mask (R8G8): r = covered by the selected actor, g = visible (not
// behind other geometry). Drawn without depth testing; visibility compares
// against the scene depth. Blending is MAX, so a pixel is visible when any
// covering fragment is.

layout(set = 0, binding = 0) uniform sampler2D sceneDepth;

layout(location = 0) out vec2 outMask;

void main() {
    float scene = texelFetch(sceneDepth, ivec2(gl_FragCoord.xy), 0).r;
    float visible = gl_FragCoord.z <= scene + 1e-6 ? 1.0 : 0.0;
    outMask = vec2(1.0, visible);
}
