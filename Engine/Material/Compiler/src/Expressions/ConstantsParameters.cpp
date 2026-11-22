// Constants and parameters.

#include "Common.h"

namespace Luma::MaterialCompiler::Detail {
namespace {

using MaterialGraph::ExpressionDesc;
using MaterialGraph::Node;
using MaterialGraph::ValueType;

i32 ConstantScalar(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    return t.Constant({static_cast<f32>(PropF(d, n, "value")), 0.0f, 0.0f, 0.0f}, ValueType::Float1);
}

template <u32 N>
i32 ConstantVector(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    return t.Constant(PropV(d, n, "value"), FloatType(N));
}

i32 StaticBool(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    return t.Constant({PropB(d, n, "value") ? 1.0f : 0.0f, 0, 0, 0}, ValueType::Bool);
}

i32 ScalarParameter(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    ParameterInfo p = ParamFromProperties(d, n, ParameterInfo::Kind::Scalar);
    p.defaultValue = {static_cast<f32>(PropF(d, n, "default")), 0.0f, 0.0f, 0.0f};
    p.sliderMin = static_cast<f32>(PropF(d, n, "sliderMin"));
    p.sliderMax = static_cast<f32>(PropF(d, n, "sliderMax"));
    return t.ScalarParameter(std::move(p));
}

i32 VectorParameter(Translator& t, const Node& n, const ExpressionDesc& d, u32 outputPin) {
    ParameterInfo p = ParamFromProperties(d, n, ParameterInfo::Kind::Vector);
    p.defaultValue = PropV(d, n, "default");
    return ColorOutput(t, t.VectorParameter(std::move(p)), outputPin);
}

i32 TextureParameterChunk(Translator& t, const Node& n, const ExpressionDesc& d, bool cube) {
    ParameterInfo p = ParamFromProperties(d, n, ParameterInfo::Kind::Texture);
    TextureSlot slot = SlotFromProperties(d, n);
    slot.cube = cube;
    p.defaultTexture = slot.defaultTexture;
    return t.Texture(std::move(slot), &p);
}

template <bool Cube>
i32 TextureSampleParameter(Translator& t, const Node& n, const ExpressionDesc& d, u32 outputPin) {
    const i32 texture = t.Memo(n.id, "texture", [&] { return TextureParameterChunk(t, n, d, Cube); });
    const auto type = static_cast<SamplerType>(PropI(d, n, "sampler"));
    return SampleOutput(t, n, d, outputPin, texture, type, SampleInputs{0, -1, -1, -1});
}

i32 TextureObjectParameter(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    return TextureParameterChunk(t, n, d, false);
}

i32 StaticSwitchParameter(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    ParameterInfo p = ParamFromProperties(d, n, ParameterInfo::Kind::StaticSwitch);
    const bool def = PropB(d, n, "default");
    p.defaultValue = {def ? 1.0f : 0.0f, 0, 0, 0};
    if (!t.DeclareParameter(p)) return -1;
    // Only the chosen branch is compiled: the other one costs nothing.
    return In(t, n, t.StaticSwitch(p.name, def) ? 0u : 1u);
}

i32 StaticBoolParameter(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    ParameterInfo p = ParamFromProperties(d, n, ParameterInfo::Kind::StaticSwitch);
    const bool def = PropB(d, n, "default");
    p.defaultValue = {def ? 1.0f : 0.0f, 0, 0, 0};
    if (!t.DeclareParameter(p)) return -1;
    return t.Constant({t.StaticSwitch(p.name, def) ? 1.0f : 0.0f, 0, 0, 0}, ValueType::Bool);
}

}  // namespace

void RegisterConstantsParameters(Table& table) {
    table["Constant"] = &ConstantScalar;
    table["Constant2Vector"] = &ConstantVector<2>;
    table["Constant3Vector"] = &ConstantVector<3>;
    table["Constant4Vector"] = &ConstantVector<4>;
    table["StaticBool"] = &StaticBool;
    table["ScalarParameter"] = &ScalarParameter;
    table["VectorParameter"] = &VectorParameter;
    table["TextureSampleParameter2D"] = &TextureSampleParameter<false>;
    table["TextureSampleParameterCube"] = &TextureSampleParameter<true>;
    table["TextureObjectParameter"] = &TextureObjectParameter;
    table["StaticSwitchParameter"] = &StaticSwitchParameter;
    table["StaticBoolParameter"] = &StaticBoolParameter;
}

}  // namespace Luma::MaterialCompiler::Detail
