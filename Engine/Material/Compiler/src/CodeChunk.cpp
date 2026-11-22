#include "Luma/MaterialCompiler/CodeChunk.h"

#include <charconv>
#include <cmath>

namespace Luma::MaterialCompiler {

using MaterialGraph::ValueType;

u32 Width(ValueType type) {
    switch (type) {
        case ValueType::Float1: return 1;
        case ValueType::Float2: return 2;
        case ValueType::Float3: return 3;
        case ValueType::Float4: return 4;
        default: return 0;
    }
}

ValueType FloatType(u32 width) {
    switch (width) {
        case 2: return ValueType::Float2;
        case 3: return ValueType::Float3;
        case 4: return ValueType::Float4;
        default: return ValueType::Float1;
    }
}

const char* GlslType(ValueType type) {
    switch (type) {
        case ValueType::Float2: return "vec2";
        case ValueType::Float3: return "vec3";
        case ValueType::Float4: return "vec4";
        default: return "float";
    }
}

namespace {

// Shortest round-trip decimal, always with a decimal point or exponent so
// GLSL reads it as a float ("5" -> "5.0", "1e+20" stays a float literal).
std::string Number(f32 value) {
    if (value == 0.0f) value = 0.0f;  // fold -0 into 0 (stable output)
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    std::string text(buffer, result.ptr);
    if (text.find_first_of(".e") == std::string::npos) text += ".0";
    return text;
}

}  // namespace

std::string GlslLiteral(const Math::Vec4& v, ValueType type) {
    const u32 n = std::max(Width(type), 1u);
    if (n == 1) return Number(v.x);
    const f32 lanes[4] = {v.x, v.y, v.z, v.w};
    std::string text = std::string(GlslType(type)) + "(";
    for (u32 i = 0; i < n; ++i) {
        if (i > 0) text += ", ";
        text += Number(lanes[i]);
    }
    return text + ")";
}

}  // namespace Luma::MaterialCompiler
