#pragma once

#include <array>
#include <optional>
#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/Math/Math.h"

// What a backend needs to draw materials: compiled shaders per pass, fixed-
// function state, the texture table and parameter blocks. The enums mirror
// Luma::Material / Luma::MaterialCompiler so the RHI depends on neither; the
// editor's material bridge produces these from compiled material graphs.

namespace Luma {

using MeshHandle = u32;
using MaterialHandle = u32;
using GpuTextureHandle = u32;
constexpr u32 kInvalidHandle = ~0u;

// Material key 0 / handle 0 is always the Default Material.
constexpr u64 kDefaultMaterialKey = 0;
constexpr MaterialHandle kDefaultMaterialHandle = 0;

enum class GpuBlendMode : u8 { Opaque, Masked, Translucent, Additive, Modulate };
enum class GpuShadingModel : u8 { DefaultLit, Unlit, Subsurface, ClearCoat, TwoSidedFoliage };
enum class GpuPostProcessLocation : u8 { BeforeTonemapping, AfterTonemapping };
enum class GpuTextureView : u8 { SRGB, Linear };

// The passes a compiled material can draw in (Luma::MaterialCompiler::Pass).
enum class GpuPass : u8 { GBuffer, DepthOnly, SelectionMask, Forward, PostProcess, Count };
constexpr usize kGpuPassCount = static_cast<usize>(GpuPass::Count);

// The backend's immutable sampler table: filter x address (9 entries), then
// linear filtering without mips per address (render targets, scene textures).
enum class GpuSamplerFilter : u8 { Linear, Nearest, Anisotropic, LinearNoMips };
enum class GpuSamplerAddress : u8 { Wrap, Clamp, Mirror };
constexpr u32 kGpuSamplerCount = 12;
constexpr u32 GpuSamplerIndex(GpuSamplerFilter filter, GpuSamplerAddress address) {
    return static_cast<u32>(filter) * 3u + static_cast<u32>(address);
}

// SPIR-V of one pass. `key` identifies the pair (pipeline cache key part).
struct GpuShaderPair {
    std::vector<u32> vertex;
    std::vector<u32> fragment;
    u64 key = 0;
};

// What a parameter-block texture slot is bound to. The backend writes
// (image index, sampler index, bound, 0) into block[slot] when it uploads.
struct GpuTextureBinding {
    u32 slot = 0;
    GpuTextureHandle texture = kInvalidHandle;  // invalid = the backend's white texture
    bool srgb = false;
    u32 sampler = 0;  // GpuSamplerIndex
    bool cube = false;
};

// A material compiled for every pass it draws in (Luma::MaterialCompiler +
// shader compiler output). Backends know nothing about graphs: they get
// SPIR-V, fixed-function state and the texture table.
struct CompiledMaterialGpu {
    u64 shaderKey = 0;  // identity of the whole shader set (pipelines are cached by it)
    std::array<std::optional<GpuShaderPair>, kGpuPassCount> passes;
    GpuBlendMode blend = GpuBlendMode::Opaque;
    GpuShadingModel shading = GpuShadingModel::DefaultLit;
    bool twoSided = false;
    f32 opacityMaskClip = 0.3333f;
    bool writesCustomData = false;  // G-buffer RT4 (Subsurface / Clear Coat / Foliage)
    bool usesWPO = false;           // World Position Offset (no depth pre-pass reuse)
    bool usesRefraction = false;    // needs the scene-colour copy before the forward pass
    GpuPostProcessLocation ppLocation = GpuPostProcessLocation::BeforeTonemapping;
    i32 ppPriority = 0;
    std::vector<GpuTextureBinding> textures;

    bool HasPass(GpuPass pass) const { return passes[static_cast<usize>(pass)].has_value(); }
};

// One material's (or instance's) parameter block: one vec4 per slot, as the
// preshader evaluated it. Texture slots are rewritten by the backend.
struct MaterialParameterBlock {
    std::vector<Math::Vec4> values;

    bool operator==(const MaterialParameterBlock& o) const {
        if (values.size() != o.values.size()) return false;
        for (usize i = 0; i < values.size(); ++i) {
            const Math::Vec4& a = values[i];
            const Math::Vec4& b = o.values[i];
            if (a.x != b.x || a.y != b.y || a.z != b.z || a.w != b.w) return false;
        }
        return true;
    }
};

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
