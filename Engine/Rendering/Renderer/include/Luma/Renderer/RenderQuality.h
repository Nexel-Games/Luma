#pragma once

#include "Luma/Core/Types.h"

// Quality and performance switches the deferred renderer reads every frame.
// Filled from the project's graphics settings (Luma::Graphics) by the host
// application; the renderer applies changes on the next RenderScene (shadow
// resources are rebuilt when their size changes).

namespace Luma::Renderer2 {

struct RenderQuality {
    bool shadows = true;
    u32 shadowMapSize = 2048;   // per cascade (power of two)
    u32 maxCascades = 4;        // [1, 4]
    f32 maxShadowDistance = 150.0f;
    bool fxaa = true;
    bool bloom = true;
    bool autoExposure = true;
    bool frustumCulling = true;
    bool shadowCasterCulling = true;
    // Mesh levels of detail: pick each instance's LOD by its screen size;
    // `lodBias` shifts the choice (positive = coarser, negative = finer).
    bool meshLods = true;
    i32 lodBias = 0;

    bool operator==(const RenderQuality&) const = default;
};

}  // namespace Luma::Renderer2
