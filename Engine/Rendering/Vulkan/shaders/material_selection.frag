// material_selection.frag - selection mask of the selected actor (R8G8):
// r = covered, g = visible (not behind other geometry). Drawn without depth
// testing; visibility compares against the scene depth. Masked materials clip
// like the G-buffer pass, so outlines follow cut-outs; translucent materials
// count their whole surface.

#include "material_pixel.glsl"

layout(location = 0) out vec2 outMask;

void main() {
#if defined(LUMA_BLEND_MASKED) || defined(LUMA_PIXEL_DEPTH_OFFSET)
    LumaShadePixel();
#endif
    float scene = texelFetch(lumaSceneTextures[kLumaSceneDepth], ivec2(gl_FragCoord.xy), 0).r;
    float visible = gl_FragCoord.z <= scene + 1e-6 ? 1.0 : 0.0;
    outMask = vec2(1.0, visible);
}
