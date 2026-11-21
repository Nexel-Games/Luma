// Material Parameter Collections: a Collection Parameter is a uniform the
// preshader reads from the collection's current value, so changing a value
// (Wind strength, time of day, ...) re-evaluates blocks without recompiling.

#include "Common.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/MaterialCompiler/Preshader.h"

namespace Luma::MaterialCompiler {

ParameterValueSource CollectionValues(Material::MaterialLibrary& library) {
    return [&library](const std::string& collection, const std::string& parameter) -> std::optional<Math::Vec4> {
        const Material::ParameterCollection* pc = library.GetCollection(collection);
        if (!pc) return std::nullopt;
        for (const auto& s : pc->scalars) {
            if (s.name == parameter) return Math::Vec4{s.value, s.value, s.value, s.value};
        }
        for (const auto& v : pc->vectors) {
            if (v.name == parameter) return v.value;
        }
        return std::nullopt;
    };
}

namespace Detail {
namespace {

using MaterialGraph::ExpressionDesc;
using MaterialGraph::Node;
using MaterialGraph::ValueType;

i32 CollectionParameter(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    const std::string path = PropS(d, n, "collection");
    const std::string name = PropS(d, n, "parameter");
    if (path.empty()) {
        t.Error(n.id, -1, "No Parameter Collection selected");
        return -1;
    }
    const Material::ParameterCollection* pc = t.Library() ? t.Library()->GetCollection(path) : nullptr;
    if (!pc) {
        t.Error(n.id, -1, "Parameter Collection '" + path + "' was not found");
        return -1;
    }
    for (const auto& s : pc->scalars) {
        if (s.name == name) return t.CollectionValue(path, name, ValueType::Float1);
    }
    for (const auto& v : pc->vectors) {
        if (v.name == name) return t.CollectionValue(path, name, ValueType::Float4);
    }
    t.Error(n.id, -1, "'" + path + "' has no parameter '" + name + "'");
    return -1;
}

}  // namespace

void RegisterCollections(Table& table) { table["CollectionParameter"] = &CollectionParameter; }

}  // namespace Detail
}  // namespace Luma::MaterialCompiler
