#include "Luma/Scene/ComponentReflection.h"

namespace Luma {

template <>
const TypeInfo<NameComponent>& GetTypeInfo<NameComponent>() {
    static const TypeInfo<NameComponent> info = [] {
        TypeBuilder<NameComponent> b("Name");
        b.Property("name", &NameComponent::name).Category("General");
        return b.Build();
    }();
    return info;
}

template <>
const TypeInfo<TransformComponent>& GetTypeInfo<TransformComponent>() {
    static const TypeInfo<TransformComponent> info = [] {
        TypeBuilder<TransformComponent> b("Transform");
        b.Property("position", &TransformComponent::position).Category("Transform");
        b.Property("rotationEuler", &TransformComponent::rotationEuler)
            .Category("Transform")
            .Tooltip("Euler angles in degrees");
        b.Property("scale", &TransformComponent::scale).Category("Transform");
        return b.Build();
    }();
    return info;
}

template <>
const TypeInfo<MeshRendererComponent>& GetTypeInfo<MeshRendererComponent>() {
    static const TypeInfo<MeshRendererComponent> info = [] {
        TypeBuilder<MeshRendererComponent> b("MeshRenderer");
        b.Property("primitive", &MeshRendererComponent::primitive).Category("Mesh");
        b.Property("mesh", &MeshRendererComponent::mesh)
            .Category("Mesh")
            .Tooltip("Static mesh asset (virtual path). Empty draws the primitive shape.");
        b.Property("materials", &MeshRendererComponent::materials)
            .Category("Materials")
            .Tooltip("Material overrides, one per mesh section (virtual asset paths; empty = "
                     "the mesh's own material)");
        return b.Build();
    }();
    return info;
}

template <>
const TypeInfo<CameraComponent>& GetTypeInfo<CameraComponent>() {
    static const TypeInfo<CameraComponent> info = [] {
        TypeBuilder<CameraComponent> b("Camera");
        b.Property("projection", &CameraComponent::projection).Category("Camera");
        b.Property("fovYDegrees", &CameraComponent::fovYDegrees)
            .Category("Camera")
            .Range(10.0, 170.0);
        b.Property("orthoHeight", &CameraComponent::orthoHeight).Category("Camera");
        b.Property("nearZ", &CameraComponent::nearZ).Category("Camera");
        b.Property("farZ", &CameraComponent::farZ).Category("Camera");
        b.Property("primary", &CameraComponent::primary).Category("Camera");
        return b.Build();
    }();
    return info;
}

template <>
const TypeInfo<LightComponent>& GetTypeInfo<LightComponent>() {
    static const TypeInfo<LightComponent> info = [] {
        TypeBuilder<LightComponent> b("Light");
        b.Property("type", &LightComponent::type).Category("Light");
        b.Property("color", &LightComponent::color).Category("Light");
        b.Property("intensity", &LightComponent::intensity)
            .Category("Light")
            .Range(0.0, 100.0);
        b.Property("range", &LightComponent::range).Category("Light");
        b.Property("innerAngleDeg", &LightComponent::innerAngleDeg)
            .Category("Spot")
            .Range(0.0, 90.0);
        b.Property("outerAngleDeg", &LightComponent::outerAngleDeg)
            .Category("Spot")
            .Range(0.0, 90.0);
        // Sun disk (directional)
        b.Property("sunDiskSizeDeg", &LightComponent::sunDiskSizeDeg)
            .Category("Sun")
            .Range(0.1, 15.0);
        b.Property("sunDiskIntensity", &LightComponent::sunDiskIntensity)
            .Category("Sun")
            .Range(0.0, 10.0);
        // Attenuation (point/spot/tube)
        b.Property("attenuationRadius", &LightComponent::attenuationRadius)
            .Category("Attenuation");
        b.Property("attenuationPower", &LightComponent::attenuationPower)
            .Category("Attenuation")
            .Range(0.5, 4.0);
        b.Property("length", &LightComponent::length)
            .Category("Attenuation")
            .Range(0.1, 20.0);
        // Shadows
        b.Property("castShadows", &LightComponent::castShadows).Category("Shadows");
        b.Property("shadowMapSize", &LightComponent::shadowMapSize)
            .Category("Shadows")
            .Range(256, 4096);
        b.Property("shadowBias", &LightComponent::shadowBias)
            .Category("Shadows")
            .Range(0.0, 0.05);
        b.Property("normalBias", &LightComponent::normalBias)
            .Category("Shadows")
            .Range(0.0, 1.0);
        b.Property("shadowSoftness", &LightComponent::shadowSoftness)
            .Category("Shadows")
            .Range(0.0, 8.0);
        // Cascaded shadow maps (directional)
        b.Property("cascadeCount", &LightComponent::cascadeCount)
            .Category("Shadows")
            .Range(1, 4);
        b.Property("shadowDistance", &LightComponent::shadowDistance)
            .Category("Shadows");
        b.Property("cascadeSplitLambda", &LightComponent::cascadeSplitLambda)
            .Category("Shadows")
            .Range(0.0, 1.0);
        return b.Build();
    }();
    return info;
}

template <>
const TypeInfo<EnvironmentComponent>& GetTypeInfo<EnvironmentComponent>() {
    static const TypeInfo<EnvironmentComponent> info = [] {
        TypeBuilder<EnvironmentComponent> b("Environment");
        // Atmosphere
        b.Property("skyEnabled", &EnvironmentComponent::skyEnabled).Category("Atmosphere");
        b.Property("rayleighScattering", &EnvironmentComponent::rayleighScattering)
            .Category("Atmosphere");
        b.Property("rayleighScaleHeight", &EnvironmentComponent::rayleighScaleHeight)
            .Category("Atmosphere");
        b.Property("mieScattering", &EnvironmentComponent::mieScattering).Category("Atmosphere");
        b.Property("mieAbsorption", &EnvironmentComponent::mieAbsorption).Category("Atmosphere");
        b.Property("mieScaleHeight", &EnvironmentComponent::mieScaleHeight).Category("Atmosphere");
        b.Property("mieAnisotropy", &EnvironmentComponent::mieAnisotropy)
            .Category("Atmosphere")
            .Range(0.0, 1.0);
        b.Property("ozoneScale", &EnvironmentComponent::ozoneScale).Category("Atmosphere");
        // Sky
        b.Property("skyIntensity", &EnvironmentComponent::skyIntensity).Category("Sky");
        b.Property("saturation", &EnvironmentComponent::saturation).Category("Sky");
        b.Property("exposure", &EnvironmentComponent::exposure).Category("Sky");
        b.Property("skyTint", &EnvironmentComponent::skyTint).Category("Sky");
        // Ground & Ambient
        b.Property("groundColor", &EnvironmentComponent::groundColor).Category("Ground");
        b.Property("iblIntensity", &EnvironmentComponent::iblIntensity).Category("Ambient");
        return b.Build();
    }();
    return info;
}

template <>
const TypeInfo<PostProcessComponent>& GetTypeInfo<PostProcessComponent>() {
    static const TypeInfo<PostProcessComponent> info = [] {
        TypeBuilder<PostProcessComponent> b("PostProcess");
        b.Property("enabled", &PostProcessComponent::enabled).Category("General");
        b.Property("priority", &PostProcessComponent::priority).Category("General");
        b.Property("blendRadius", &PostProcessComponent::blendRadius).Category("General");
        b.Property("blendWeight", &PostProcessComponent::blendWeight)
            .Category("General").Range(0.0, 1.0);
        // Bloom
        b.Property("bloomEnabled", &PostProcessComponent::bloomEnabled).Category("Bloom");
        b.Property("bloomIntensity", &PostProcessComponent::bloomIntensity)
            .Category("Bloom").Range(0.0, 10.0);
        b.Property("bloomThreshold", &PostProcessComponent::bloomThreshold)
            .Category("Bloom").Range(0.0, 10.0);
        b.Property("bloomSoftThreshold", &PostProcessComponent::bloomSoftThreshold)
            .Category("Bloom").Range(0.0, 2.0);
        b.Property("bloomPasses", &PostProcessComponent::bloomPasses)
            .Category("Bloom").Range(3, 7);
        b.Property("bloomTint", &PostProcessComponent::bloomTint).Category("Bloom");
        // Tone Mapping
        b.Property("toneMapper", &PostProcessComponent::toneMapper).Category("ToneMapping");
        b.Property("exposure", &PostProcessComponent::exposure)
            .Category("ToneMapping").Range(0.01, 10.0);
        b.Property("autoExposure", &PostProcessComponent::autoExposure).Category("ToneMapping");
        b.Property("autoExposureSpeed", &PostProcessComponent::autoExposureSpeed)
            .Category("ToneMapping").Range(0.1, 10.0);
        b.Property("whitePoint", &PostProcessComponent::whitePoint)
            .Category("ToneMapping").Range(1.0, 100.0);
        // SSAO
        b.Property("ssaoEnabled", &PostProcessComponent::ssaoEnabled).Category("SSAO");
        b.Property("ssaoRadius", &PostProcessComponent::ssaoRadius)
            .Category("SSAO").Range(1.0, 200.0);
        b.Property("ssaoIntensity", &PostProcessComponent::ssaoIntensity)
            .Category("SSAO").Range(0.0, 5.0);
        b.Property("ssaoSamples", &PostProcessComponent::ssaoSamples)
            .Category("SSAO").Range(4, 64);
        // SSR
        b.Property("ssrEnabled", &PostProcessComponent::ssrEnabled).Category("SSR");
        b.Property("ssrIntensity", &PostProcessComponent::ssrIntensity)
            .Category("SSR").Range(0.0, 2.0);
        b.Property("ssrMaxDistance", &PostProcessComponent::ssrMaxDistance)
            .Category("SSR").Range(10.0, 1000.0);
        // Motion Blur
        b.Property("motionBlurEnabled", &PostProcessComponent::motionBlurEnabled)
            .Category("MotionBlur");
        b.Property("motionBlurAmount", &PostProcessComponent::motionBlurAmount)
            .Category("MotionBlur").Range(0.0, 2.0);
        // DOF
        b.Property("dofMethod", &PostProcessComponent::dofMethod).Category("DOF");
        b.Property("dofFocalDistance", &PostProcessComponent::dofFocalDistance)
            .Category("DOF").Range(0.1, 10000.0);
        b.Property("dofScale", &PostProcessComponent::dofScale)
            .Category("DOF").Range(0.0, 5.0);
        // Color Grading
        b.Property("colorGradingEnabled", &PostProcessComponent::colorGradingEnabled)
            .Category("ColorGrading");
        b.Property("colorGradingSaturation", &PostProcessComponent::colorGradingSaturation)
            .Category("ColorGrading").Range(0.0, 3.0);
        b.Property("colorGradingContrast", &PostProcessComponent::colorGradingContrast)
            .Category("ColorGrading").Range(0.0, 3.0);
        b.Property("colorGradingGamma", &PostProcessComponent::colorGradingGamma)
            .Category("ColorGrading").Range(0.1, 5.0);
        b.Property("colorGradingTint", &PostProcessComponent::colorGradingTint)
            .Category("ColorGrading");
        // Vignette
        b.Property("vignetteEnabled", &PostProcessComponent::vignetteEnabled)
            .Category("Vignette");
        b.Property("vignetteIntensity", &PostProcessComponent::vignetteIntensity)
            .Category("Vignette").Range(0.0, 2.0);
        b.Property("vignetteRadius", &PostProcessComponent::vignetteRadius)
            .Category("Vignette").Range(0.0, 1.0);
        // Film Grain
        b.Property("filmGrainEnabled", &PostProcessComponent::filmGrainEnabled)
            .Category("FilmGrain");
        b.Property("filmGrainIntensity", &PostProcessComponent::filmGrainIntensity)
            .Category("FilmGrain").Range(0.0, 2.0);
        return b.Build();
    }();
    return info;
}

}  // namespace Luma
