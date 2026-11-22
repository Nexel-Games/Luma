#pragma once

#include <string>
#include <utility>
#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/Math/Math.h"
#include "Luma/MaterialGraph/Graph.h"

// What a compiled material exposes: its parameters (for Material Instances)
// and the layout of its parameter block (one vec4 per slot).

namespace Luma::MaterialCompiler {

// Sampler settings of a texture slot (Texture Sample "Sampler Type" etc.).
struct SamplerState {
    enum class Address : u8 { Wrap, Clamp, Mirror };
    enum class Filter : u8 { Linear, Nearest, Anisotropic };
    Address address = Address::Wrap;
    Filter filter = Filter::Anisotropic;
    bool operator==(const SamplerState&) const = default;
};

// How a texture's texels are decoded (Unreal's sampler types).
enum class SamplerType : u8 { Color, Linear, Normal, Masks, LinearGrayscale };

struct ParameterInfo {
    enum class Kind : u8 { Scalar, Vector, Texture, StaticSwitch };
    std::string name, group, description;
    Kind kind = Kind::Scalar;
    f32 sliderMin = 0.0f, sliderMax = 0.0f;
    i32 sortPriority = 32;
    Math::Vec4 defaultValue{0.0f, 0.0f, 0.0f, 0.0f};  // scalar in .x, switch as 0/1
    std::string defaultTexture;                         // virtual path
    MaterialGraph::NodeId node = 0;                     // first node declaring it
};

// One texture binding of the material. Stored in the parameter block as
// uvec4(floatBitsToUint): x = sampled-image index (view chosen by `srgb`),
// y = sampler index, z = 1 if bound.
struct TextureSlot {
    u32 slot = 0;                 // parameter-block vec4 index
    std::string defaultTexture;   // virtual path ("" = engine default texture)
    std::string parameter;        // Texture parameter name ("" = fixed texture)
    SamplerState sampler;
    SamplerType samplerType = SamplerType::Color;
    bool srgb = true;
    bool cube = false;
};

struct ParameterBlockLayout {
    u32 vec4Count = 0;
    std::vector<TextureSlot> textures;
    // Collection base offsets: (collection path, block slot).
    std::vector<std::pair<std::string, u32>> collectionSlots;
};

}  // namespace Luma::MaterialCompiler
