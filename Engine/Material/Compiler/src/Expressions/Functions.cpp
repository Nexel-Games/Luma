// Material Function calls: the function graph compiles inline per call, its
// Function Inputs bound to the caller's links (or their preview values).

#include <algorithm>

#include "Common.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/MaterialCompiler/Functions.h"

namespace Luma::MaterialCompiler {

using MaterialGraph::ExpressionDesc;
using MaterialGraph::ExpressionRegistry;
using MaterialGraph::Node;
using MaterialGraph::PropertyOrDefault;
using MaterialGraph::ValueType;

ValueType FunctionInputType(i64 inputType) {
    switch (inputType) {
        case 0: return ValueType::Float1;
        case 1: return ValueType::Float2;
        case 2: return ValueType::Float3;
        case 3: return ValueType::Float4;
        case 4: return ValueType::Texture2D;
        case 5: return ValueType::TextureCube;
        case 6: return ValueType::Bool;
        default: return ValueType::Float3;
    }
}

FunctionSignature SignatureOf(const Material::MaterialFunction& function) {
    FunctionSignature s;
    const ExpressionDesc* in = ExpressionRegistry::Get().Find("FunctionInput");
    const ExpressionDesc* out = ExpressionRegistry::Get().Find("FunctionOutput");
    for (const Node& n : function.graph.Nodes()) {
        const bool isInput = n.type == "FunctionInput";
        if (!isInput && n.type != "FunctionOutput") continue;
        const ExpressionDesc& d = isInput ? *in : *out;
        FunctionPin pin;
        pin.node = n.id;
        pin.name = PropertyOrDefault(d, n, "name").AsString();
        pin.description = PropertyOrDefault(d, n, "description").AsString();
        pin.sortPriority = PropertyOrDefault(d, n, "sortPriority").AsInt();
        if (isInput) pin.type = FunctionInputType(PropertyOrDefault(d, n, "inputType").AsInt());
        (isInput ? s.inputs : s.outputs).push_back(std::move(pin));
    }
    auto order = [](const FunctionPin& a, const FunctionPin& b) {
        return a.sortPriority != b.sortPriority ? a.sortPriority < b.sortPriority : a.node < b.node;
    };
    std::sort(s.inputs.begin(), s.inputs.end(), order);
    std::sort(s.outputs.begin(), s.outputs.end(), order);
    return s;
}

namespace Detail {
namespace {

i32 MaterialFunctionCall(Translator& t, const Node& n, const ExpressionDesc& d, u32 outputPin) {
    const std::string path = PropS(d, n, "function");
    if (path.empty()) {
        t.Error(n.id, -1, "No Material Function selected");
        return -1;
    }
    const Material::MaterialFunction* function = t.Library() ? t.Library()->GetFunction(path) : nullptr;
    if (!function) {
        t.Error(n.id, -1, "Material Function '" + path + "' was not found");
        return -1;
    }
    const FunctionSignature signature = SignatureOf(*function);
    if (outputPin >= signature.outputs.size()) {
        t.Error(n.id, -1, "'" + path + "' has no output " + std::to_string(outputPin));
        return -1;
    }
    return t.CompileFunctionOutput(n.id, path, *function, signature.outputs[outputPin].node);
}

// Converts a bound value to the input's declared type.
i32 AsInputType(Translator& t, const Node& n, const std::string& name, i32 value, ValueType type) {
    if (value < 0) return -1;
    if (Width(type) != 0) return t.Cast(value, type, n.id);
    if (t.ChunkAt(value).type != type) {
        t.Error(n.id, -1, "Input '" + name + "' needs a " +
                              (type == ValueType::Bool ? std::string("Static Bool")
                               : type == ValueType::TextureCube ? std::string("Texture Cube")
                                                                : std::string("Texture 2D")));
        return -1;
    }
    return value;
}

i32 FunctionInput(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    const std::string name = PropS(d, n, "name");
    const ValueType type = FunctionInputType(PropI(d, n, "inputType"));
    const Material::MaterialFunction* function = t.CurrentFunction();
    if (!function) {
        // Function previews compile a material that calls the function.
        t.Error(n.id, -1, "Function Input is only valid inside a Material Function");
        return -1;
    }
    const FunctionSignature signature = SignatureOf(*function);
    const auto it = std::find_if(signature.inputs.begin(), signature.inputs.end(),
                                 [&n](const FunctionPin& p) { return p.node == n.id; });
    bool linked = false;
    const i32 bound = t.CompileCallerInput(static_cast<u32>(it - signature.inputs.begin()), linked);
    if (linked) return AsInputType(t, n, name, bound, type);
    if (!PropB(d, n, "usePreviewAsDefault")) {
        t.Error(n.id, -1, "Input '" + name + "' isn't connected");
        return -1;
    }
    return AsInputType(t, n, name, In(t, n, 0), type);  // the preview value
}

}  // namespace

void RegisterFunctions(Table& table) {
    table["MaterialFunctionCall"] = &MaterialFunctionCall;
    table["FunctionInput"] = &FunctionInput;
}

}  // namespace Detail
}  // namespace Luma::MaterialCompiler
