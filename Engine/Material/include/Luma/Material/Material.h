#pragma once

#include <array>
#include <string>

#include "Luma/Material/MaterialTypes.h"
#include "Luma/Math/Math.h"

// The Material asset (.lmat) — Unreal UMaterial equivalent for Luma's fixed
// Default Lit template: material settings plus one input per parameter. Each
// input is a constant or a texture (tinted, channel-selected, tiled).

namespace Luma::Material {

struct MaterialInput {
    // Constant value (scalars use .x, colors .rgb, normal .xyz tangent space).
    Math::Vec4 constant{0.0f, 0.0f, 0.0f, 0.0f};
    // Texture as a virtual asset path (/Game/...); empty = use the constant.
    std::string texture;
    TextureChannel channel = TextureChannel::RGB;
    // Multiplied with the texture sample (scalars use .x).
    Math::Vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};
    Math::Vec2 uvTiling{1.0f, 1.0f};
    Math::Vec2 uvOffset{0.0f, 0.0f};
    // Scalar inputs: use 1 - value (e.g. a gloss map driving Roughness).
    bool invert = false;
    // Strength: Normal (normal strength), Height (height scale, UV units),
    // Bump (bump strength).
    f32 normalStrength = 1.0f;
    bool flipGreen = false;  // DirectX-style (-Y) normal maps
    // Emissive input only: HDR multiplier.
    f32 emissiveIntensity = 1.0f;
};

struct MaterialSettings {
    MaterialDomain domain = MaterialDomain::Surface;
    BlendMode blendMode = BlendMode::Opaque;
    ShadingModel shadingModel = ShadingModel::DefaultLit;
    bool twoSided = false;
    f32 opacityMaskClip = 0.3333f;  // Unreal's default clip value
    TranslucencyLighting translucencyLighting = TranslucencyLighting::SurfacePerPixel;
    RefractionMethod refraction = RefractionMethod::None;
    PostProcessLocation ppLocation = PostProcessLocation::BeforeTonemapping;
    i32 ppPriority = 0;  // post-process materials run in ascending priority
};

using MaterialInputs = std::array<MaterialInput, kMaterialParameterCount>;

struct Material {
    std::string name;
    MaterialSettings settings;
    MaterialInputs inputs = DefaultInputs();

    MaterialInput& Input(MaterialParameter p) { return inputs[static_cast<usize>(p)]; }
    const MaterialInput& Input(MaterialParameter p) const {
        return inputs[static_cast<usize>(p)];
    }

    // Per-parameter defaults: base color (0.82, 0.82, 0.85) — the old
    // Mesh Renderer default, so migrated scenes look identical — metallic 0,
    // specular 0.5, roughness 0.5, flat normal, no emission, AO 1, mask 1.
    static MaterialInputs DefaultInputs();
    // The engine Default Material (/Engine/Materials/DefaultMaterial).
    static Material MakeDefault();
};

}  // namespace Luma::Material
