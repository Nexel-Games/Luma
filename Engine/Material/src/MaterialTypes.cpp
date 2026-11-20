#include "Luma/Material/Material.h"
#include "Luma/Material/MaterialTypes.h"

namespace Luma::Material {

const char* ParameterKey(MaterialParameter p) {
    switch (p) {
        case MaterialParameter::BaseColor: return "baseColor";
        case MaterialParameter::Metallic: return "metallic";
        case MaterialParameter::Specular: return "specular";
        case MaterialParameter::Roughness: return "roughness";
        case MaterialParameter::Normal: return "normal";
        case MaterialParameter::EmissiveColor: return "emissiveColor";
        case MaterialParameter::AmbientOcclusion: return "ambientOcclusion";
        case MaterialParameter::OpacityMask: return "opacityMask";
        case MaterialParameter::Cavity: return "cavity";
        case MaterialParameter::Height: return "height";
        case MaterialParameter::Bump: return "bump";
        case MaterialParameter::Count: break;
    }
    return "";
}

const char* ParameterDisplayName(MaterialParameter p) {
    switch (p) {
        case MaterialParameter::BaseColor: return "Base Color";
        case MaterialParameter::Metallic: return "Metallic";
        case MaterialParameter::Specular: return "Specular";
        case MaterialParameter::Roughness: return "Roughness";
        case MaterialParameter::Normal: return "Normal";
        case MaterialParameter::EmissiveColor: return "Emissive Color";
        case MaterialParameter::AmbientOcclusion: return "Ambient Occlusion";
        case MaterialParameter::OpacityMask: return "Opacity Mask";
        case MaterialParameter::Cavity: return "Cavity";
        case MaterialParameter::Height: return "Height";
        case MaterialParameter::Bump: return "Bump";
        case MaterialParameter::Count: break;
    }
    return "";
}

bool IsColorParameter(MaterialParameter p) {
    return p == MaterialParameter::BaseColor || p == MaterialParameter::EmissiveColor;
}

TextureView SelectTextureView(MaterialParameter p, bool textureIsSRGB) {
    return IsColorParameter(p) && textureIsSRGB ? TextureView::SRGB : TextureView::Linear;
}

MaterialInputs Material::DefaultInputs() {
    MaterialInputs in{};
    auto set = [&in](MaterialParameter p, Math::Vec4 v) {
        in[static_cast<usize>(p)].constant = v;
    };
    set(MaterialParameter::BaseColor, {0.82f, 0.82f, 0.85f, 1.0f});
    set(MaterialParameter::Metallic, {0.0f, 0.0f, 0.0f, 0.0f});
    set(MaterialParameter::Specular, {0.5f, 0.0f, 0.0f, 0.0f});
    set(MaterialParameter::Roughness, {0.5f, 0.0f, 0.0f, 0.0f});
    set(MaterialParameter::Normal, {0.0f, 0.0f, 1.0f, 0.0f});
    set(MaterialParameter::EmissiveColor, {0.0f, 0.0f, 0.0f, 0.0f});
    set(MaterialParameter::AmbientOcclusion, {1.0f, 0.0f, 0.0f, 0.0f});
    set(MaterialParameter::OpacityMask, {1.0f, 0.0f, 0.0f, 0.0f});
    set(MaterialParameter::Cavity, {1.0f, 0.0f, 0.0f, 0.0f});
    // Height and Bump only act through a texture.
    set(MaterialParameter::Height, {0.0f, 0.0f, 0.0f, 0.0f});
    set(MaterialParameter::Bump, {0.0f, 0.0f, 0.0f, 0.0f});
    in[static_cast<usize>(MaterialParameter::Height)].normalStrength = 0.05f;  // height scale
    // Scalar inputs read a single channel when textured.
    for (MaterialParameter p : {MaterialParameter::Metallic, MaterialParameter::Specular,
                                MaterialParameter::Roughness,
                                MaterialParameter::AmbientOcclusion,
                                MaterialParameter::OpacityMask, MaterialParameter::Cavity,
                                MaterialParameter::Height, MaterialParameter::Bump}) {
        in[static_cast<usize>(p)].channel = TextureChannel::R;
    }
    return in;
}

Material Material::MakeDefault() {
    Material m;
    m.name = "DefaultMaterial";
    m.inputs = DefaultInputs();
    return m;
}

}  // namespace Luma::Material
