#pragma once

#include <string>
#include <vector>

#include "Luma/Material/GraphAssets.h"

// The pins of a Material Function as callers see them: its Function Input
// nodes become the call's inputs, its Function Output nodes the call's
// outputs, each ordered by Sort Priority, then creation order. The compiler
// and the graph editor share this so pin indices agree.

namespace Luma::MaterialCompiler {

struct FunctionPin {
    MaterialGraph::NodeId node = 0;  // Function Input / Output node in the function graph
    std::string name;
    std::string description;
    MaterialGraph::ValueType type = MaterialGraph::ValueType::Any;  // outputs: Any (known once compiled)
    i64 sortPriority = 0;
};

struct FunctionSignature {
    std::vector<FunctionPin> inputs;
    std::vector<FunctionPin> outputs;
};

FunctionSignature SignatureOf(const Material::MaterialFunction& function);

// A Function Input's "Input Type" enum value as a value type.
MaterialGraph::ValueType FunctionInputType(i64 inputType);

}  // namespace Luma::MaterialCompiler
