#pragma once

#include <algorithm>
#include <cmath>

#include "Luma/Core/Types.h"
#include "Luma/Math/Math.h"

// Cascaded shadow map (CSM) fitting for a directional light (the sun).
//
// Pure math, backend-agnostic: any renderer (Vulkan deferred, forward, future
// backends) calls FitSunCascades() and gets light view-projection matrices +
// split distances to render depth into and sample from.
//
// Stability: each cascade is fit with a *bounding sphere* of its frustum slice
// (constant size under camera rotation) and its center is snapped to whole
// shadow-map texels in light space (constant sub-texel alignment under camera
// translation). Together these remove the shimmering/crawling edges a tight
// AABB fit produces while the camera moves.

namespace Luma::Renderer2 {

inline constexpr u32 kMaxSunCascades = 4;

struct SunCascadeInput {
    Math::Mat4 view = Math::Mat4::Identity();  // camera view matrix
    f32 fovYRadians = 0.9f;
    f32 aspect = 16.0f / 9.0f;
    f32 nearZ = 0.1f;
    f32 farZ = 80.0f;            // already clamped to the shadow distance
    Math::Vec3 sunDirection{0.35f, 0.65f, 0.55f};  // world dir TO the sun
    u32 cascadeCount = 4;
    f32 splitLambda = 0.6f;      // 0 = uniform splits, 1 = logarithmic
    u32 shadowMapSize = 2048;    // texels per side of one cascade layer
    f32 casterDistance = 100.0f; // extends each cascade toward the sun so
                                 // off-slice casters (tall objects) still cast
};

struct SunCascades {
    u32 count = 0;
    Math::Mat4 viewProj[kMaxSunCascades];
    f32 splitFar[kMaxSunCascades] = {};        // view-space depth of each far edge
    f32 texelWorldSize[kMaxSunCascades] = {};  // world units per shadow texel
};

namespace CascadeDetail {
inline Math::Vec3 TransformPoint(const Math::Mat4& m, const Math::Vec3& p) {
    return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
            m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
            m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]};
}
}  // namespace CascadeDetail

inline SunCascades FitSunCascades(const SunCascadeInput& in) {
    using namespace Math;
    using CascadeDetail::TransformPoint;

    SunCascades out;
    out.count = std::clamp<u32>(in.cascadeCount, 1u, kMaxSunCascades);

    const Mat4 invView = Inverse(in.view);
    const Vec3 camPos{invView.m[12], invView.m[13], invView.m[14]};
    const Vec3 camRight = Normalize(Vec3(invView.m[0], invView.m[1], invView.m[2]));
    const Vec3 camUp = Normalize(Vec3(invView.m[4], invView.m[5], invView.m[6]));
    const Vec3 camFwd = Normalize(Vec3(-invView.m[8], -invView.m[9], -invView.m[10]));

    const Vec3 sunDir = Normalize(in.sunDirection);
    const Vec3 up = std::fabs(sunDir.y) > 0.99f ? Vec3(0, 0, 1) : Vec3(0, 1, 0);
    // Rotation-only light basis (eye at the origin looking along -sunDir) used
    // to snap cascade centers to the shadow texel grid.
    const Mat4 lightRot = LookAt(Vec3(0, 0, 0), sunDir * -1.0f, up);
    const Mat4 invLightRot = Inverse(lightRot);

    const f32 nearZ = std::max(in.nearZ, 0.01f);
    const f32 farZ = std::max(in.farZ, nearZ + 0.1f);
    const f32 tanY = std::tan(in.fovYRadians * 0.5f);
    const f32 tanX = tanY * in.aspect;
    const f32 size = static_cast<f32>(std::max(in.shadowMapSize, 1u));
    const f32 lambda = std::clamp(in.splitLambda, 0.0f, 1.0f);

    f32 sliceNear = nearZ;
    for (u32 c = 0; c < out.count; ++c) {
        // Practical split scheme: blend logarithmic and uniform distributions.
        const f32 p = static_cast<f32>(c + 1) / static_cast<f32>(out.count);
        const f32 logSplit = nearZ * std::pow(farZ / nearZ, p);
        const f32 uniSplit = nearZ + (farZ - nearZ) * p;
        const f32 sliceFar = lambda * logSplit + (1.0f - lambda) * uniSplit;
        out.splitFar[c] = sliceFar;

        // World-space corners of this frustum slice.
        Vec3 corners[8];
        int k = 0;
        for (f32 zd : {sliceNear, sliceFar}) {
            const Vec3 center = camPos + camFwd * zd;
            const f32 hh = tanY * zd, hw = tanX * zd;
            corners[k++] = center + camUp * hh + camRight * hw;
            corners[k++] = center + camUp * hh - camRight * hw;
            corners[k++] = center - camUp * hh + camRight * hw;
            corners[k++] = center - camUp * hh - camRight * hw;
        }
        sliceNear = sliceFar;

        // Bounding sphere: rotation-invariant extent.
        Vec3 center{0, 0, 0};
        for (const Vec3& v : corners) center = center + v;
        center = center * (1.0f / 8.0f);
        f32 radius = 0.0f;
        for (const Vec3& v : corners) {
            const Vec3 d = v - center;
            radius = std::max(radius, std::sqrt(Dot(d, d)));
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;  // quantize: stable size

        // Snap the center to whole texels in light space: translation-stable.
        const f32 texelWorld = (2.0f * radius) / size;
        Vec3 ls = TransformPoint(lightRot, center);
        ls.x = std::floor(ls.x / texelWorld) * texelWorld;
        ls.y = std::floor(ls.y / texelWorld) * texelWorld;
        center = TransformPoint(invLightRot, ls);

        const f32 pad = std::max(in.casterDistance, 0.0f);
        const Mat4 lightView = LookAt(center + sunDir * (radius + pad), center, up);
        const Mat4 lightProj =
            Ortho(-radius, radius, -radius, radius, 0.01f, 2.0f * radius + pad);
        out.viewProj[c] = lightProj * lightView;
        out.texelWorldSize[c] = texelWorld;
    }
    return out;
}

}  // namespace Luma::Renderer2
