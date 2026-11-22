#include "Common.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace Luma::MaterialCompiler::Detail {

using MaterialGraph::ExpressionDesc;
using MaterialGraph::Node;
using MaterialGraph::NodeId;
using MaterialGraph::PinRef;
using MaterialGraph::PropertyOrDefault;
using MaterialGraph::ValueType;

f64 PropF(const ExpressionDesc& d, const Node& n, std::string_view key) { return PropertyOrDefault(d, n, key).AsFloat(); }
i64 PropI(const ExpressionDesc& d, const Node& n, std::string_view key) { return PropertyOrDefault(d, n, key).AsInt(); }
bool PropB(const ExpressionDesc& d, const Node& n, std::string_view key) { return PropertyOrDefault(d, n, key).AsBool(); }
std::string PropS(const ExpressionDesc& d, const Node& n, std::string_view key) {
    return PropertyOrDefault(d, n, key).AsString();
}
Math::Vec4 PropV(const ExpressionDesc& d, const Node& n, std::string_view key) { return PropertyOrDefault(d, n, key).AsVec4(); }

i32 In(Translator& t, const Node& n, u32 pin) { return t.CompileInput(n.id, pin, t.CurrentStage()); }

bool Linked(const Translator& t, const Node& n, u32 pin) {
    return t.CurrentGraph().Source(PinRef{n.id, pin}).has_value();
}

i32 InOr(Translator& t, const Node& n, u32 pin, i32 fallback) {
    return Linked(t, n, pin) ? In(t, n, pin) : fallback;
}

// The preshader op of a GLSL intrinsic pattern, if it has one.
std::optional<PreOp> PreOpFor(std::string_view pattern);

i32 DefaultUV(Translator& t) { return t.Reference("P.texcoords[0]", ValueType::Float2, Frequency::Varying); }

namespace {

f32 Lane(const Math::Vec4& v, u32 width, u32 i) {
    if (width == 1) return v.x;
    const f32 lanes[4] = {v.x, v.y, v.z, v.w};
    return i < 4 ? lanes[i] : 0.0f;
}

std::string Substitute(std::string_view pattern, std::initializer_list<const std::string*> args) {
    std::string out;
    for (usize i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '$' && i + 1 < pattern.size() && pattern[i + 1] >= '0' && pattern[i + 1] <= '9') {
            const usize index = static_cast<usize>(pattern[i + 1] - '0');
            out += *(args.begin()[index]);
            ++i;
        } else {
            out += pattern[i];
        }
    }
    return out;
}

template <usize N, typename Fold>
i32 MapN(Translator& t, NodeId at, std::array<i32, N> args, std::string_view pattern, Fold fold) {
    for (i32 a : args) {
        if (a < 0) return -1;
    }
    u32 w = 0;
    if constexpr (N == 1) {
        w = Width(t.ChunkAt(args[0]).type);
        if (w == 0) {
            t.Error(at, -1, "Needs a float input");
            return -1;
        }
    } else if constexpr (N == 2) {
        w = Unify(t, at, {&args[0], &args[1]});
    } else {
        w = Unify(t, at, {&args[0], &args[1], &args[2]});
    }
    if (w == 0) return -1;
    const ValueType type = FloatType(w);
    bool allConstant = fold != nullptr;
    Frequency freq = Frequency::Constant;
    for (i32 a : args) {
        allConstant = allConstant && t.ChunkAt(a).constant.has_value();
        freq = std::max(freq, t.ChunkAt(a).freq);
    }
    if (allConstant) {
        f32 out[4] = {0, 0, 0, 0};
        bool finite = true;
        for (u32 i = 0; i < w; ++i) {
            if constexpr (N == 1) {
                out[i] = fold(Lane(*t.ChunkAt(args[0]).constant, w, i));
            } else if constexpr (N == 2) {
                out[i] = fold(Lane(*t.ChunkAt(args[0]).constant, w, i), Lane(*t.ChunkAt(args[1]).constant, w, i));
            } else {
                out[i] = fold(Lane(*t.ChunkAt(args[0]).constant, w, i), Lane(*t.ChunkAt(args[1]).constant, w, i),
                              Lane(*t.ChunkAt(args[2]).constant, w, i));
            }
            finite = finite && std::isfinite(out[i]);
        }
        if (finite) return t.Constant({out[0], out[1], out[2], out[3]}, type);
    }
    if (freq == Frequency::Uniform) {
        if (const std::optional<PreOp> op = PreOpFor(pattern)) {
            i32 r = -1;
            if constexpr (N == 1) {
                r = t.Preshade(*op, {args[0]}, type);
            } else if constexpr (N == 2) {
                r = t.Preshade(*op, {args[0], args[1]}, type);
            } else {
                r = t.Preshade(*op, {args[0], args[1], args[2]}, type);
            }
            if (r >= 0) return r;
        }
    }
    std::string code;
    if constexpr (N == 1) {
        code = Substitute(pattern, {&t.ChunkAt(args[0]).code});
    } else if constexpr (N == 2) {
        code = Substitute(pattern, {&t.ChunkAt(args[0]).code, &t.ChunkAt(args[1]).code});
    } else {
        code = Substitute(pattern, {&t.ChunkAt(args[0]).code, &t.ChunkAt(args[1]).code, &t.ChunkAt(args[2]).code});
    }
    return t.Emit(std::move(code), type, freq);
}

}  // namespace

std::optional<PreOp> PreOpFor(std::string_view pattern) {
    static const std::pair<std::string_view, PreOp> kOps[] = {
        {"abs($0)", PreOp::Abs},           {"clamp($0, 0.0, 1.0)", PreOp::Saturate},
        {"sqrt($0)", PreOp::Sqrt},         {"floor($0)", PreOp::Floor},
        {"ceil($0)", PreOp::Ceil},         {"fract($0)", PreOp::Frac},
        {"sin($0)", PreOp::Sin},           {"cos($0)", PreOp::Cos},
        {"min($0, $1)", PreOp::Min},       {"max($0, $1)", PreOp::Max},
        {"pow($0, $1)", PreOp::Pow},       {"clamp($0, $1, $2)", PreOp::Clamp},
        {"mix($0, $1, $2)", PreOp::Lerp},
    };
    for (const auto& [glsl, op] : kOps) {
        if (glsl == pattern) return op;
    }
    return std::nullopt;
}

u32 Unify(Translator& t, NodeId at, std::initializer_list<i32*> chunks) {
    u32 w = 1;
    for (i32* c : chunks) {
        if (*c < 0) return 0;
        const u32 cw = Width(t.ChunkAt(*c).type);
        if (cw == 0) {
            t.Error(at, -1, "Needs float inputs");
            return 0;
        }
        if (cw != 1 && w != 1 && cw != w) {
            t.Error(at, -1,
                    "Can't combine a Float" + std::to_string(w) + " and a Float" + std::to_string(cw) +
                        " (use Append or Component Mask to match widths)");
            return 0;
        }
        w = std::max(w, cw);
    }
    for (i32* c : chunks) {
        *c = t.Cast(*c, FloatType(w), at);
        if (*c < 0) return 0;
    }
    return w;
}

i32 Map1(Translator& t, NodeId at, i32 a, std::string_view pattern, Lane1 fold) {
    return MapN<1>(t, at, {a}, pattern, fold);
}

i32 Map2(Translator& t, NodeId at, i32 a, i32 b, std::string_view pattern, Lane2 fold) {
    return MapN<2>(t, at, {a, b}, pattern, fold);
}

i32 Map3(Translator& t, NodeId at, i32 a, i32 b, i32 c, std::string_view pattern, Lane3 fold) {
    return MapN<3>(t, at, {a, b, c}, pattern, fold);
}

i32 Swizzle(Translator& t, i32 value, std::string_view components) {
    if (value < 0) return -1;
    const Chunk c = t.ChunkAt(value);
    const u32 w = Width(c.type);
    const u32 n = static_cast<u32>(components.size());
    const ValueType type = FloatType(n);
    if (c.constant) {
        f32 out[4] = {0, 0, 0, 0};
        for (u32 i = 0; i < n; ++i) {
            const u32 lane = static_cast<u32>(std::string_view("xyzw").find(components[i]));
            out[i] = Lane(*c.constant, w, lane);
        }
        return t.Constant({out[0], out[1], out[2], out[3]}, type);
    }
    if (w == 1) return t.Cast(value, type, 0);  // a Float1 replicates
    if (t.Preshadable(value)) {
        std::array<u8, 5> extra{static_cast<u8>(n), 0, 0, 0, 0};
        for (u32 i = 0; i < n; ++i) {
            extra[i + 1] = static_cast<u8>(std::string_view("xyzw").find(components[i]));
        }
        const i32 r = t.Preshade(PreOp::Swizzle, {value}, type, extra);
        if (r >= 0) return r;
    }
    return t.Reference(c.code + "." + std::string(components), type, c.freq);
}

i32 Concat(Translator& t, NodeId at, std::initializer_list<i32> parts) {
    u32 width = 0;
    bool allConstant = true;
    Frequency freq = Frequency::Constant;
    f32 lanes[4] = {0, 0, 0, 0};
    std::string code;
    for (i32 p : parts) {
        if (p < 0) return -1;
        const Chunk& c = t.ChunkAt(p);
        const u32 w = Width(c.type);
        if (w == 0) {
            t.Error(at, -1, "Append needs float inputs");
            return -1;
        }
        if (width + w > 4) {
            t.Error(at, -1, "Append result would have more than 4 components");
            return -1;
        }
        if (c.constant) {
            for (u32 i = 0; i < w; ++i) lanes[width + i] = Lane(*c.constant, w, i);
        } else {
            allConstant = false;
        }
        freq = std::max(freq, c.freq);
        code += (code.empty() ? "" : ", ") + c.code;
        width += w;
    }
    const ValueType type = FloatType(width);
    if (allConstant) return t.Constant({lanes[0], lanes[1], lanes[2], lanes[3]}, type);
    if (freq == Frequency::Uniform &&
        std::all_of(parts.begin(), parts.end(), [&t](i32 p) { return t.Preshadable(p); })) {
        // Fold the parts left to right with Append.
        i32 acc = *parts.begin();
        u32 accWidth = Width(t.ChunkAt(acc).type);
        for (auto it = parts.begin() + 1; it != parts.end() && acc >= 0; ++it) {
            const u32 w = Width(t.ChunkAt(*it).type);
            acc = t.Preshade(PreOp::Append, {acc, *it}, FloatType(accWidth + w),
                             {static_cast<u8>(accWidth), static_cast<u8>(w), 0, 0, 0});
            accWidth += w;
        }
        if (acc >= 0) return acc;
    }
    return t.Emit(std::string(GlslType(type)) + "(" + code + ")", type, freq);
}

TextureSlot SlotFromProperties(const ExpressionDesc& d, const Node& n) {
    TextureSlot slot;
    slot.defaultTexture = PropS(d, n, "texture");
    slot.samplerType = static_cast<SamplerType>(std::clamp<i64>(PropI(d, n, "sampler"), 0, 4));
    slot.sampler.address = static_cast<SamplerState::Address>(std::clamp<i64>(PropI(d, n, "address"), 0, 2));
    slot.sampler.filter = static_cast<SamplerState::Filter>(std::clamp<i64>(PropI(d, n, "filter"), 0, 2));
    slot.srgb = slot.samplerType == SamplerType::Color;
    return slot;
}

ParameterInfo ParamFromProperties(const ExpressionDesc& d, const Node& n, ParameterInfo::Kind kind) {
    ParameterInfo p;
    p.kind = kind;
    p.name = PropS(d, n, "name");
    p.group = PropS(d, n, "group");
    p.description = PropS(d, n, "description");
    p.sortPriority = static_cast<i32>(PropI(d, n, "sortPriority"));
    p.node = n.id;
    return p;
}

i32 ColorOutput(Translator& t, i32 rgba, u32 outputPin) {
    switch (outputPin) {
        case 0: return Swizzle(t, rgba, "xyz");
        case 1: return Swizzle(t, rgba, "x");
        case 2: return Swizzle(t, rgba, "y");
        case 3: return Swizzle(t, rgba, "z");
        case 4: return Swizzle(t, rgba, "w");
        default: return rgba;
    }
}

i32 SampleOutput(Translator& t, const Node& n, const ExpressionDesc& d, u32 outputPin, i32 texture,
                 SamplerType type, const SampleInputs& inputs) {
    if (texture < 0) return -1;
    const bool cube = t.ChunkAt(texture).type == ValueType::TextureCube;
    const i32 rgba = t.Memo(n.id, "sample", [&]() -> i32 {
        const std::string slot = t.ChunkAt(texture).code;
        i32 uv = -1;
        if (Linked(t, n, static_cast<u32>(inputs.uvPin))) {
            uv = t.Cast(In(t, n, static_cast<u32>(inputs.uvPin)), cube ? ValueType::Float3 : ValueType::Float2, n.id);
        } else {
            uv = cube ? t.Reference("reflect(-P.cameraVector, P.worldNormal)", ValueType::Float3, Frequency::Varying)
                      : DefaultUV(t);
        }
        if (uv < 0) return -1;
        const std::string uvCode = t.ChunkAt(uv).code;
        const char* kind = cube ? "MSampleCube" : "MSample2D";
        // Implicit derivatives don't exist in the vertex shader: sample MIP 0.
        i64 mode = PropI(d, n, "mipMode");
        if (t.CurrentStage() == Stage::Vertex && (mode == 0 || mode == 1)) mode = 2;
        std::string mip = "0.0";
        if (inputs.mipValuePin >= 0 && Linked(t, n, static_cast<u32>(inputs.mipValuePin))) {
            const i32 m = t.Cast(In(t, n, static_cast<u32>(inputs.mipValuePin)), ValueType::Float1, n.id);
            if (m < 0) return -1;
            mip = t.ChunkAt(m).code;
        }
        std::string code;
        if (mode == 1) {
            code = std::string(kind) + "Bias(" + slot + ", " + uvCode + ", " + mip + ")";
        } else if (mode == 2) {
            code = std::string(kind) + "Level(" + slot + ", " + uvCode + ", " + mip + ")";
        } else if (mode == 3 && inputs.ddxPin >= 0 && !cube) {
            const i32 dx = t.Cast(InOr(t, n, static_cast<u32>(inputs.ddxPin),
                                       t.Constant({0, 0, 0, 0}, ValueType::Float2)),
                                  ValueType::Float2, n.id);
            const i32 dy = t.Cast(InOr(t, n, static_cast<u32>(inputs.ddyPin),
                                       t.Constant({0, 0, 0, 0}, ValueType::Float2)),
                                  ValueType::Float2, n.id);
            if (dx < 0 || dy < 0) return -1;
            code = std::string(kind) + "Grad(" + slot + ", " + uvCode + ", " + t.ChunkAt(dx).code + ", " +
                   t.ChunkAt(dy).code + ")";
        } else {
            code = std::string(kind) + "(" + slot + ", " + uvCode + ")";
        }
        if (type == SamplerType::Normal) code = "MUnpackNormal(" + code + ")";
        if (type == SamplerType::LinearGrayscale) code += ".rrrr";
        return t.Emit(std::move(code), ValueType::Float4, Frequency::Varying);
    });
    return ColorOutput(t, rgba, outputPin);
}

}  // namespace Luma::MaterialCompiler::Detail
