#pragma once

#include <optional>
#include <string>

#include "Luma/Core/Types.h"
#include "Luma/Math/Math.h"
#include "Luma/MaterialGraph/Graph.h"

// The translator's unit of generated code (Unreal: a "code chunk").

namespace Luma::MaterialCompiler {

// How often a value changes. Ordered: the frequency of an expression is the
// highest of its operands'.
enum class Frequency : u8 {
    Constant,  // known at compile time (folded)
    Uniform,   // depends only on parameters / collections (CPU preshader)
    Varying,   // per vertex / pixel
};

enum class Stage : u8 { Vertex, Pixel };

struct Chunk {
    std::string code;  // a GLSL expression: literal, "vN" local, or uniform read
    MaterialGraph::ValueType type = MaterialGraph::ValueType::Float1;
    Frequency freq = Frequency::Constant;
    std::optional<Math::Vec4> constant;  // set when freq == Constant (unused lanes 0)
    Stage stage = Stage::Pixel;
};

// Components of a float type (1..4); 0 for non-float types.
u32 Width(MaterialGraph::ValueType type);
// The float type with `width` components (1..4).
MaterialGraph::ValueType FloatType(u32 width);
// "float", "vec2", "vec3", "vec4".
const char* GlslType(MaterialGraph::ValueType type);
// Canonical GLSL literal for a constant of `type` ("5.0", "vec3(1.0, 2.0, 3.0)").
std::string GlslLiteral(const Math::Vec4& v, MaterialGraph::ValueType type);

}  // namespace Luma::MaterialCompiler
