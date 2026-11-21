#include "ExpressionCompilers.h"

#include "Expressions/Common.h"

namespace Luma::MaterialCompiler::Detail {
namespace {

const Table& BuildTable() {
    static const Table table = [] {
        Table t;
        RegisterConstantsParameters(t);
        RegisterMath(t);
        RegisterTextureCoordinates(t);
        RegisterUtility(t);
        return t;
    }();
    return table;
}

}  // namespace

CompileFn FindCompiler(std::string_view type) {
    const Table& table = BuildTable();
    const auto it = table.find(type);
    return it == table.end() ? nullptr : it->second;
}

}  // namespace Luma::MaterialCompiler::Detail
