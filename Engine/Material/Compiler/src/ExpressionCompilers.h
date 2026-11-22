#pragma once

#include <string_view>

#include "Luma/MaterialCompiler/Translator.h"

// Per-expression compile functions (Unreal: UMaterialExpression::Compile).
// The catalogue in Luma::MaterialGraph is pure data; this table gives each
// expression type its code generation.

namespace Luma::MaterialCompiler::Detail {

// Compiles output pin `outputPin` of `node` at the translator's current stage.
using CompileFn = i32 (*)(Translator& t, const MaterialGraph::Node& node,
                          const MaterialGraph::ExpressionDesc& desc, u32 outputPin);

// Null when the expression type has no code generation.
CompileFn FindCompiler(std::string_view type);

}  // namespace Luma::MaterialCompiler::Detail
