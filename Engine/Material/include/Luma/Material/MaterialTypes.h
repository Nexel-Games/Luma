#pragma once

#include "Luma/Core/Types.h"

// Core enums of the Luma material model (Unreal UMaterial-style, PBR
// metallic/roughness). Persisted by name (see ParameterKey), never by value,
// so the enums may be extended freely.

namespace Luma::Material {

// The material's inputs (Unreal "material attributes" for Default Lit).
enum class MaterialParameter : u8 {
    BaseColor,
    Metallic,
    Specular,
    Roughness,
    Normal,
    EmissiveColor,
    AmbientOcclusion,
    OpacityMask,
    Count,
};

inline constexpr usize kMaterialParameterCount =
    static_cast<usize>(MaterialParameter::Count);

// Stable JSON key for a parameter ("baseColor", ...).
const char* ParameterKey(MaterialParameter p);
// Display name for UI ("Base Color", ...).
const char* ParameterDisplayName(MaterialParameter p);
// Color inputs are sampled through an sRGB view when their texture is sRGB;
// every other input is data and always sampled linear.
bool IsColorParameter(MaterialParameter p);

// Which texture channel(s) feed an input. RGB for colors/normals; a single
// channel lets one packed (ORM/ARM) texture drive several scalar inputs.
enum class TextureChannel : u8 { RGB, R, G, B, A };

enum class BlendMode : u8 { Opaque, Masked };
enum class ShadingModel : u8 { DefaultLit, Unlit };

// How a material input's texture is viewed by the GPU.
enum class TextureView : u8 { SRGB, Linear };

// Color inputs use the SRGB view when the texture is sRGB; data inputs
// (normal, roughness, metallic, AO, specular, opacity mask) are always linear
// so they stay correct regardless of how the texture was flagged on import.
TextureView SelectTextureView(MaterialParameter p, bool textureIsSRGB);

}  // namespace Luma::Material
