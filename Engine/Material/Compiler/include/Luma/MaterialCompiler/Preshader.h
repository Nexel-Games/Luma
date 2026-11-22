#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Luma/Material/GraphAssets.h"
#include "Luma/MaterialCompiler/Parameters.h"

// Uniform expressions (Unreal's "preshaders"): every value of a material
// that depends only on parameters, collections and constants is computed on
// the CPU by a small bytecode program and stored in the material's parameter
// block (one vec4 per slot). Material Instances re-run the program with their
// overrides -- no shader recompile.
//
// Values are vec4s; a Float1 is kept broadcast in all four lanes, so every
// lane-wise op works on any width and GLSL reads a scalar as MP(slot).x.

namespace Luma::Material {
class MaterialLibrary;
}

namespace Luma::MaterialCompiler {

enum class PreOp : u8 {
    PushConst,        // u16 constant index
    PushScalarParam,  // u16 parameter index (broadcast)
    PushVectorParam,  // u16 parameter index
    PushCollection,   // u16 collection-value index (broadcast for scalars)
    Add, Sub, Mul, Div, Neg, Min, Max, Clamp, Saturate, Pow, Abs, Floor, Ceil, Frac, Sqrt, Sin, Cos, Lerp,
    Dot,      // u8 width
    Swizzle,  // u8 count, u8 lanes[4] (count 1 broadcasts)
    Append,   // u8 width of A, u8 width of B
    Store,    // u16 slot
};

struct PreshaderParam {
    std::string name;
    bool vector = false;
    Math::Vec4 defaultValue{0.0f, 0.0f, 0.0f, 0.0f};
};

struct PreshaderProgram {
    std::vector<u8> code;
    std::vector<Math::Vec4> constants;
    std::vector<PreshaderParam> params;
    // (collection virtual path, parameter name) read by PushCollection.
    std::vector<std::pair<std::string, std::string>> collectionValues;
    u32 OpCount() const;
};

// What a texture slot is bound to (from the renderer's texture registry).
struct TextureBinding {
    u32 image = 0;    // sampled-image index (view per TextureSlot::srgb)
    u32 sampler = 0;  // sampler-state index
    bool bound = false;
};
using TextureResolver = std::function<TextureBinding(const std::string& path, const TextureSlot& slot)>;
// Current value of a collection parameter (nullopt = unknown: uses 0).
using ParameterValueSource =
    std::function<std::optional<Math::Vec4>(const std::string& collection, const std::string& parameter)>;

// Current values of the library's Parameter Collections (scalars broadcast).
// The library must outlive the returned source.
ParameterValueSource CollectionValues(Material::MaterialLibrary& library);

// Runs the program with `overrides` (by parameter name) and fills texture
// slots through `textures` (unbound when null). Returns the whole block.
std::vector<Math::Vec4> EvaluatePreshader(const PreshaderProgram& program, const ParameterBlockLayout& layout,
                                          std::span<const Material::ParameterOverride> overrides,
                                          const ParameterValueSource& collections, const TextureResolver& textures);

}  // namespace Luma::MaterialCompiler
