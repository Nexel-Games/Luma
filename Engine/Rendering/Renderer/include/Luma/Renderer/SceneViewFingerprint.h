#pragma once

#include <bit>
#include <type_traits>
#include "Luma/Renderer/DeferredSceneView.h"

namespace Luma::Renderer2 {
namespace detail {
// Hash values independently of padding, vector capacity, and allocation address.
class ViewHash {
public:
    u64 value = 14695981039346656037ull;
    template<class T> requires (std::is_integral_v<T> || std::is_enum_v<T>)
    void Add(T word) { value = (value ^ static_cast<u64>(word)) * 1099511628211ull; }
    void Add(f32 number) { Add(std::bit_cast<u32>(number)); }
    void Add(const Math::Vec3& v) { Fields(v.x, v.y, v.z); }
    void Add(const Math::Mat4& m) { for (auto v : m.m) Add(v); }
    template<class... T> void Fields(const T&... fields) { (Add(fields), ...); }
};
}  // namespace detail

// Time and delta are intentionally excluded: time-driven effects pause in an idle viewport.
inline u64 SceneViewFingerprint(const DeferredSceneView& view, u64 contentRevision) {
    detail::ViewHash h;
    h.Fields(contentRevision, view.width, view.height, view.viewProjectionMatrix, view.viewMatrix,
             view.projectionMatrix, view.cameraPosition, view.cameraDirection, view.fov, view.nearPlane, view.farPlane);
    if (!view.sceneData) return h.value;
    const auto& s = *view.sceneData;
    const auto& sky = s.sky;
    h.Fields(sky.enabled, sky.sunDirection, sky.sunColor, sky.sunIntensity, sky.sunDiskSizeDeg,
             sky.sunDiskIntensity, sky.rayleighScattering, sky.rayleighScaleHeight, sky.mieScattering,
             sky.mieAbsorption, sky.mieScaleHeight, sky.mieAnisotropy, sky.ozoneScale, sky.skyIntensity,
             sky.saturation, sky.exposure, sky.skyTint, sky.groundColor);
    const auto& g = s.grid;
    h.Fields(g.enabled, g.minorColor, g.majorColor, g.axisX, g.axisZ, g.cellSize, g.majorEvery, g.fadeStart, g.fadeEnd);
    if (view.lightingParams) {
        const auto& l = *view.lightingParams;
        h.Fields(l.sunDirection, l.sunColor, l.sunIntensity, l.skyZenith, l.skyHorizon, l.groundColor, l.iblIntensity,
                 l.sunShadows, l.shadowDistance, l.shadowSoftness, l.shadowBias, l.normalBias, l.cascadeSplitLambda,
                 l.cascadeCount);
    }
    const auto& p = s.postProcess;
    h.Fields(s.viewMode, p.bloomEnabled, p.bloomIntensity, p.bloomThreshold, p.bloomSoftThreshold, p.bloomTint,
             p.toneMapper, p.exposure, p.autoExposure, p.autoExposureSpeed, p.autoExposureMinLuminance,
             p.autoExposureMaxLuminance, p.materials.size());
    for (const auto& material : p.materials) h.Fields(material.material, material.weight);
    h.Fields(s.instanceCount, s.lightCount, s.lineVertexCount, s.overlayLineVertexCount);
    for (u32 i = 0; i < s.instanceCount; ++i) {
        const auto& obj = s.instances[i];
        h.Fields(obj.model, obj.mesh, obj.primitive, obj.selected, obj.sectionCount);
        for (auto slot : obj.sectionMaterials) h.Add(slot);
    }
    for (u32 i = 0; i < s.lightCount; ++i) {
        const auto& l = s.lights[i];
        h.Fields(l.position, l.type, l.direction, l.range, l.color, l.intensity, l.cosInner,
                 l.cosOuter, l.falloffExponent);
    }
    for (u32 i = 0; i < s.lineVertexCount; ++i) h.Fields(s.lines[i].position, s.lines[i].color);
    for (u32 i = 0; i < s.overlayLineVertexCount; ++i) h.Fields(s.overlayLines[i].position, s.overlayLines[i].color);
    return h.value;
}
}  // namespace Luma::Renderer2
