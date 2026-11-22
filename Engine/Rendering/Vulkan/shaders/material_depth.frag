// material_depth.frag - depth-only pass (shadow casters): the material runs so
// Masked cut-outs and Pixel Depth Offset match what the G-buffer pass draws.
// World Position Offset already moved the vertices.

#include "material_pixel.glsl"

void main() {
#if defined(LUMA_BLEND_MASKED) || defined(LUMA_PIXEL_DEPTH_OFFSET)
    LumaShadePixel();
#endif
}
