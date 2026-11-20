#pragma once

#include <array>

#include "Luma/Core/Types.h"
#include "Luma/Math/Math.h"

// What a backend needs to shade one material: blend/shading state plus one
// resolved input per material parameter. These mirror the Luma::Material
// enums so the RHI does not depend on the material module; the editor's
// material bridge converts a resolved material into this description.

namespace Luma {

using MeshHandle = u32;
using MaterialHandle = u32;
using GpuTextureHandle = u32;
constexpr u32 kInvalidHandle = ~0u;

// Material key 0 / handle 0 is always the Default Material.
constexpr u64 kDefaultMaterialKey = 0;
constexpr MaterialHandle kDefaultMaterialHandle = 0;

// Same order as Luma::Material::MaterialParameter.
enum class GpuMaterialInput : u8 {
    BaseColor,
    Metallic,
    Specular,
    Roughness,
    Normal,
    EmissiveColor,
    AmbientOcclusion,
    OpacityMask,
    Cavity,
    Height,
    Bump,
    Count
};
constexpr usize kMaterialGpuInputCount = static_cast<usize>(GpuMaterialInput::Count);

enum class GpuBlendMode : u8 { Opaque, Masked };
enum class GpuShadingModel : u8 { DefaultLit, Unlit };
enum class GpuTextureView : u8 { SRGB, Linear };
enum class GpuTextureChannel : u8 { RGB, R, G, B, A };

struct MaterialGpuDesc {
    GpuBlendMode blend = GpuBlendMode::Opaque;
    GpuShadingModel shading = GpuShadingModel::DefaultLit;
    bool twoSided = false;
    f32 opacityMaskClip = 0.3333f;

    struct Input {
        Math::Vec4 constant{0.0f, 0.0f, 0.0f, 0.0f};
        GpuTextureHandle texture = kInvalidHandle;  // invalid = use constant
        GpuTextureView view = GpuTextureView::Linear;
        GpuTextureChannel channel = GpuTextureChannel::RGB;
        Math::Vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};
        Math::Vec2 tiling{1.0f, 1.0f};
        Math::Vec2 offset{0.0f, 0.0f};
        f32 normalStrength = 1.0f;  // Normal / Height (scale) / Bump strength
        bool flipGreen = false;
        bool invert = false;         // scalar inputs: 1 - value
        f32 emissiveIntensity = 1.0f;
    };
    std::array<Input, kMaterialGpuInputCount> inputs{};
};

// The engine Default Material (/Engine/Materials/DefaultMaterial) as a GPU
// description: the backend's fallback until the editor uploads the asset.
inline MaterialGpuDesc MakeDefaultMaterialGpuDesc() {
    MaterialGpuDesc d;
    auto set = [&d](GpuMaterialInput p, Math::Vec4 v, GpuTextureChannel ch) {
        auto& in = d.inputs[static_cast<usize>(p)];
        in.constant = v;
        in.channel = ch;
    };
    set(GpuMaterialInput::BaseColor, {0.82f, 0.82f, 0.85f, 1.0f}, GpuTextureChannel::RGB);
    set(GpuMaterialInput::Metallic, {0.0f, 0.0f, 0.0f, 0.0f}, GpuTextureChannel::R);
    set(GpuMaterialInput::Specular, {0.5f, 0.5f, 0.5f, 0.5f}, GpuTextureChannel::R);
    set(GpuMaterialInput::Roughness, {0.5f, 0.5f, 0.5f, 0.5f}, GpuTextureChannel::R);
    set(GpuMaterialInput::Normal, {0.0f, 0.0f, 1.0f, 0.0f}, GpuTextureChannel::RGB);
    set(GpuMaterialInput::EmissiveColor, {0.0f, 0.0f, 0.0f, 0.0f}, GpuTextureChannel::RGB);
    set(GpuMaterialInput::AmbientOcclusion, {1.0f, 1.0f, 1.0f, 1.0f}, GpuTextureChannel::R);
    set(GpuMaterialInput::OpacityMask, {1.0f, 1.0f, 1.0f, 1.0f}, GpuTextureChannel::R);
    set(GpuMaterialInput::Cavity, {1.0f, 1.0f, 1.0f, 1.0f}, GpuTextureChannel::R);
    set(GpuMaterialInput::Height, {0.0f, 0.0f, 0.0f, 0.0f}, GpuTextureChannel::R);
    set(GpuMaterialInput::Bump, {0.0f, 0.0f, 0.0f, 0.0f}, GpuTextureChannel::R);
    d.inputs[static_cast<usize>(GpuMaterialInput::Height)].normalStrength = 0.05f;
    return d;
}

// Pixel layouts a backend accepts for material textures. 8-bit gray and
// gray-alpha sample as (g, g, g, a); R16 samples as (g, g, g, 1).
enum class TexturePixelFormat : u8 { R8, RG8, RGBA8, R16, RGBA16 };

inline u32 TexturePixelBytes(TexturePixelFormat f) {
    switch (f) {
        case TexturePixelFormat::R8: return 1;
        case TexturePixelFormat::RG8: return 2;
        case TexturePixelFormat::RGBA8: return 4;
        case TexturePixelFormat::R16: return 2;
        case TexturePixelFormat::RGBA16: return 8;
    }
    return 4;
}

struct TextureUploadDesc {
    u32 width = 0;
    u32 height = 0;
    TexturePixelFormat format = TexturePixelFormat::RGBA8;
    const void* pixels = nullptr;  // tightly packed rows, top row first
    bool srgb = true;              // texels are sRGB-encoded color
};

}  // namespace Luma
