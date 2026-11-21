#include "ExpressionCompilers.h"

#include <array>
#include <unordered_map>

namespace Luma::MaterialCompiler::Detail {
namespace {

using MaterialGraph::ExpressionDesc;
using MaterialGraph::Node;
using MaterialGraph::PropertyOrDefault;
using MaterialGraph::ValueType;

// --- Constants -------------------------------------------------------------

i32 ConstantScalar(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    const f32 v = static_cast<f32>(PropertyOrDefault(d, n, "value").AsFloat());
    return t.Constant({v, 0.0f, 0.0f, 0.0f}, ValueType::Float1);
}

template <u32 N>
i32 ConstantVector(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    Math::Vec4 v = PropertyOrDefault(d, n, "value").AsVec4();
    if constexpr (N < 4) v.w = 0.0f;
    if constexpr (N < 3) v.z = 0.0f;
    return t.Constant(v, FloatType(N));
}

// --- Arithmetic ------------------------------------------------------------

template <char Op>
i32 Arithmetic(Translator& t, const Node& n, const ExpressionDesc&, u32) {
    const i32 a = t.CompileInput(n.id, 0, t.CurrentStage());
    const i32 b = t.CompileInput(n.id, 1, t.CurrentStage());
    if (a < 0 || b < 0) return -1;
    const char op[2] = {Op, '\0'};
    return t.Binary(op, a, b, n.id);
}

// --- Derivatives (pixel only; the registry marks them pixelOnly) ------------

template <const char* Fn>
i32 Derivative(Translator& t, const Node& n, const ExpressionDesc&, u32) {
    const i32 v = t.CompileInput(n.id, 0, t.CurrentStage());
    if (v < 0) return -1;
    const std::array<i32, 1> args{v};
    return t.Call(Fn, args, t.ChunkAt(v).type, n.id, Frequency::Varying);
}

constexpr char kDdx[] = "dFdx";
constexpr char kDdy[] = "dFdy";

const std::unordered_map<std::string_view, CompileFn>& Table() {
    static const std::unordered_map<std::string_view, CompileFn> table = {
        {"Constant", &ConstantScalar},
        {"Constant2Vector", &ConstantVector<2>},
        {"Constant3Vector", &ConstantVector<3>},
        {"Constant4Vector", &ConstantVector<4>},
        {"Add", &Arithmetic<'+'>},
        {"Subtract", &Arithmetic<'-'>},
        {"Multiply", &Arithmetic<'*'>},
        {"Divide", &Arithmetic<'/'>},
        {"DDX", &Derivative<kDdx>},
        {"DDY", &Derivative<kDdy>},
    };
    return table;
}

}  // namespace

CompileFn FindCompiler(std::string_view type) {
    const auto& table = Table();
    const auto it = table.find(type);
    return it == table.end() ? nullptr : it->second;
}

}  // namespace Luma::MaterialCompiler::Detail
