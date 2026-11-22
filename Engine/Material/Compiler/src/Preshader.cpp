#include "Luma/MaterialCompiler/Preshader.h"

#include <algorithm>
#include <bit>
#include <cmath>

#include "Luma/MaterialCompiler/Translator.h"

namespace Luma::MaterialCompiler {
namespace {

using V4 = Math::Vec4;

u16 ReadU16(const std::vector<u8>& code, usize& pc) {
    const u16 v = static_cast<u16>(code[pc] | (code[pc + 1] << 8));
    pc += 2;
    return v;
}

f32 At(const V4& v, u32 i) {
    const f32 lanes[4] = {v.x, v.y, v.z, v.w};
    return lanes[std::min(i, 3u)];
}

template <typename F>
V4 Lanes(const V4& a, F f) {
    return {f(a.x), f(a.y), f(a.z), f(a.w)};
}
template <typename F>
V4 Lanes(const V4& a, const V4& b, F f) {
    return {f(a.x, b.x), f(a.y, b.y), f(a.z, b.z), f(a.w, b.w)};
}
template <typename F>
V4 Lanes(const V4& a, const V4& b, const V4& c, F f) {
    return {f(a.x, b.x, c.x), f(a.y, b.y, c.y), f(a.z, b.z, c.z), f(a.w, b.w, c.w)};
}

V4 Broadcast(f32 x) { return {x, x, x, x}; }

// Instruction lengths (bytes after the opcode).
usize OperandBytes(PreOp op, const std::vector<u8>& code, usize pc) {
    switch (op) {
        case PreOp::PushConst:
        case PreOp::PushScalarParam:
        case PreOp::PushVectorParam:
        case PreOp::PushCollection:
        case PreOp::Store: return 2;
        case PreOp::Dot: return 1;
        case PreOp::Swizzle: return 5;
        case PreOp::Append: return 2;
        default: (void)code; (void)pc; return 0;
    }
}

}  // namespace

u32 PreshaderProgram::OpCount() const {
    u32 count = 0;
    for (usize pc = 0; pc < code.size(); ++count) {
        const PreOp op = static_cast<PreOp>(code[pc]);
        pc += 1 + OperandBytes(op, code, pc + 1);
    }
    return count;
}

u64 StaticSwitchKey(const StaticSwitchSet& set) {
    u64 h = 1469598103934665603ull;
    for (const auto& [name, value] : set) {  // std::map: sorted, so order-independent
        for (unsigned char c : name) {
            h ^= c;
            h *= 1099511628211ull;
        }
        h ^= value ? 0xB1u : 0xB0u;
        h *= 1099511628211ull;
    }
    return h;
}

std::vector<V4> EvaluatePreshader(const PreshaderProgram& program, const ParameterBlockLayout& layout,
                                  std::span<const Material::ParameterOverride> overrides,
                                  const ParameterValueSource& collections, const TextureResolver& textures) {
    using Kind = Material::ParameterOverride::Kind;
    std::vector<V4> block(layout.vec4Count, V4{0.0f, 0.0f, 0.0f, 0.0f});

    auto overrideFor = [&](const std::string& name, Kind kind) -> const Material::ParameterOverride* {
        for (const Material::ParameterOverride& o : overrides) {
            if (o.name == name && o.kind == kind) return &o;
        }
        return nullptr;
    };

    std::vector<V4> stack;
    stack.reserve(16);
    auto pop = [&stack] {
        const V4 v = stack.back();
        stack.pop_back();
        return v;
    };
    const std::vector<u8>& code = program.code;
    for (usize pc = 0; pc < code.size();) {
        const PreOp op = static_cast<PreOp>(code[pc++]);
        switch (op) {
            case PreOp::PushConst: stack.push_back(program.constants[ReadU16(code, pc)]); break;
            case PreOp::PushScalarParam: {
                const PreshaderParam& p = program.params[ReadU16(code, pc)];
                const auto* o = overrideFor(p.name, Kind::Scalar);
                stack.push_back(Broadcast(o ? o->scalar : p.defaultValue.x));
                break;
            }
            case PreOp::PushVectorParam: {
                const PreshaderParam& p = program.params[ReadU16(code, pc)];
                const auto* o = overrideFor(p.name, Kind::Vector);
                stack.push_back(o ? o->vector : p.defaultValue);
                break;
            }
            case PreOp::PushCollection: {
                const auto& [collection, name] = program.collectionValues[ReadU16(code, pc)];
                std::optional<V4> value = collections ? collections(collection, name) : std::nullopt;
                stack.push_back(value.value_or(V4{0.0f, 0.0f, 0.0f, 0.0f}));
                break;
            }
            case PreOp::Add: { const V4 b = pop(), a = pop(); stack.push_back(Lanes(a, b, [](f32 x, f32 y) { return x + y; })); break; }
            case PreOp::Sub: { const V4 b = pop(), a = pop(); stack.push_back(Lanes(a, b, [](f32 x, f32 y) { return x - y; })); break; }
            case PreOp::Mul: { const V4 b = pop(), a = pop(); stack.push_back(Lanes(a, b, [](f32 x, f32 y) { return x * y; })); break; }
            case PreOp::Div: { const V4 b = pop(), a = pop(); stack.push_back(Lanes(a, b, [](f32 x, f32 y) { return x / y; })); break; }
            case PreOp::Min: { const V4 b = pop(), a = pop(); stack.push_back(Lanes(a, b, [](f32 x, f32 y) { return std::min(x, y); })); break; }
            case PreOp::Max: { const V4 b = pop(), a = pop(); stack.push_back(Lanes(a, b, [](f32 x, f32 y) { return std::max(x, y); })); break; }
            case PreOp::Pow: { const V4 b = pop(), a = pop(); stack.push_back(Lanes(a, b, [](f32 x, f32 y) { return std::pow(x, y); })); break; }
            case PreOp::Neg: stack.push_back(Lanes(pop(), [](f32 x) { return -x; })); break;
            case PreOp::Saturate: stack.push_back(Lanes(pop(), [](f32 x) { return std::clamp(x, 0.0f, 1.0f); })); break;
            case PreOp::Abs: stack.push_back(Lanes(pop(), [](f32 x) { return std::fabs(x); })); break;
            case PreOp::Floor: stack.push_back(Lanes(pop(), [](f32 x) { return std::floor(x); })); break;
            case PreOp::Ceil: stack.push_back(Lanes(pop(), [](f32 x) { return std::ceil(x); })); break;
            case PreOp::Frac: stack.push_back(Lanes(pop(), [](f32 x) { return x - std::floor(x); })); break;
            case PreOp::Sqrt: stack.push_back(Lanes(pop(), [](f32 x) { return std::sqrt(x); })); break;
            case PreOp::Sin: stack.push_back(Lanes(pop(), [](f32 x) { return std::sin(x); })); break;
            case PreOp::Cos: stack.push_back(Lanes(pop(), [](f32 x) { return std::cos(x); })); break;
            case PreOp::Clamp: {
                const V4 hi = pop(), lo = pop(), x = pop();
                stack.push_back(Lanes(x, lo, hi, [](f32 v, f32 l, f32 h) { return std::clamp(v, l, std::max(l, h)); }));
                break;
            }
            case PreOp::Lerp: {
                const V4 s = pop(), b = pop(), a = pop();
                stack.push_back(Lanes(a, b, s, [](f32 x, f32 y, f32 t) { return x + (y - x) * t; }));
                break;
            }
            case PreOp::Dot: {
                const u32 width = code[pc++];
                const V4 b = pop(), a = pop();
                f32 sum = 0.0f;
                for (u32 i = 0; i < width; ++i) sum += At(a, i) * At(b, i);
                stack.push_back(Broadcast(sum));
                break;
            }
            case PreOp::Swizzle: {
                const u32 count = code[pc++];
                const u8 lanes[4] = {code[pc], code[pc + 1], code[pc + 2], code[pc + 3]};
                pc += 4;
                const V4 a = pop();
                if (count == 1) {
                    stack.push_back(Broadcast(At(a, lanes[0])));
                } else {
                    f32 out[4] = {0, 0, 0, 0};
                    for (u32 i = 0; i < count; ++i) out[i] = At(a, lanes[i]);
                    stack.push_back({out[0], out[1], out[2], out[3]});
                }
                break;
            }
            case PreOp::Append: {
                const u32 wa = code[pc++], wb = code[pc++];
                const V4 b = pop(), a = pop();
                f32 out[4] = {0, 0, 0, 0};
                for (u32 i = 0; i < wa && i < 4; ++i) out[i] = At(a, i);
                for (u32 i = 0; i < wb && wa + i < 4; ++i) out[wa + i] = At(b, i);
                stack.push_back({out[0], out[1], out[2], out[3]});
                break;
            }
            case PreOp::Store: {
                const u16 slot = ReadU16(code, pc);
                if (slot < block.size()) block[slot] = pop();
                else pop();
                break;
            }
        }
    }

    for (const TextureSlot& slot : layout.textures) {
        std::string path = slot.defaultTexture;
        if (!slot.parameter.empty()) {
            if (const auto* o = overrideFor(slot.parameter, Kind::Texture)) path = o->texture;
        }
        const TextureBinding b = textures ? textures(path, slot) : TextureBinding{};
        if (slot.slot < block.size()) {
            block[slot.slot] = {std::bit_cast<f32>(b.image), std::bit_cast<f32>(b.sampler),
                                std::bit_cast<f32>(b.bound ? 1u : 0u), 0.0f};
        }
    }
    return block;
}

}  // namespace Luma::MaterialCompiler
