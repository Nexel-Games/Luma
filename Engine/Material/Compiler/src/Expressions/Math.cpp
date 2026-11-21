// Math expressions (Unreal's broadcast rules; constant inputs fold).

#include <algorithm>
#include <cmath>

#include "Common.h"

namespace Luma::MaterialCompiler::Detail {
namespace {

using MaterialGraph::ExpressionDesc;
using MaterialGraph::Node;
using MaterialGraph::ValueType;

constexpr f32 kTwoPi = 6.28318530717958647692f;

template <char Op>
i32 Arithmetic(Translator& t, const Node& n, const ExpressionDesc&, u32) {
    const char op[2] = {Op, '\0'};
    return t.Binary(op, In(t, n, 0), In(t, n, 1), n.id);
}

// --- One input, same type out ---------------------------------------------------
struct UnaryDef {
    const char* pattern;
    Lane1 fold;
};

template <const UnaryDef& Def>
i32 Unary(Translator& t, const Node& n, const ExpressionDesc&, u32) {
    return Map1(t, n.id, In(t, n, 0), Def.pattern, Def.fold);
}

constexpr UnaryDef kAbs{"abs($0)", [](f32 x) { return std::fabs(x); }};
constexpr UnaryDef kSaturate{"clamp($0, 0.0, 1.0)", [](f32 x) { return std::clamp(x, 0.0f, 1.0f); }};
constexpr UnaryDef kOneMinus{"1.0 - $0", [](f32 x) { return 1.0f - x; }};
constexpr UnaryDef kSqrt{"sqrt($0)", [](f32 x) { return std::sqrt(x); }};
constexpr UnaryDef kExp{"exp($0)", [](f32 x) { return std::exp(x); }};
constexpr UnaryDef kLog2{"log2($0)", [](f32 x) { return std::log2(x); }};
constexpr UnaryDef kTan{"tan($0)", [](f32 x) { return std::tan(x); }};
constexpr UnaryDef kAsin{"asin($0)", [](f32 x) { return std::asin(x); }};
constexpr UnaryDef kAcos{"acos($0)", [](f32 x) { return std::acos(x); }};
constexpr UnaryDef kFloor{"floor($0)", [](f32 x) { return std::floor(x); }};
constexpr UnaryDef kCeil{"ceil($0)", [](f32 x) { return std::ceil(x); }};
constexpr UnaryDef kRound{"round($0)", [](f32 x) { return std::round(x); }};
constexpr UnaryDef kFrac{"fract($0)", [](f32 x) { return x - std::floor(x); }};
constexpr UnaryDef kSign{"sign($0)", [](f32 x) { return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f); }};

// --- Two inputs ----------------------------------------------------------------------
struct BinaryDef {
    const char* pattern;
    Lane2 fold;
};

template <const BinaryDef& Def>
i32 BinaryFn(Translator& t, const Node& n, const ExpressionDesc&, u32) {
    return Map2(t, n.id, In(t, n, 0), In(t, n, 1), Def.pattern, Def.fold);
}

constexpr BinaryDef kMin{"min($0, $1)", [](f32 a, f32 b) { return std::min(a, b); }};
constexpr BinaryDef kMax{"max($0, $1)", [](f32 a, f32 b) { return std::max(a, b); }};
constexpr BinaryDef kPow{"pow($0, $1)", [](f32 a, f32 b) { return std::pow(a, b); }};
// Fmod keeps the sign of A (Unreal / HLSL fmod), unlike GLSL mod.
constexpr BinaryDef kFmod{"LumaFmod($0, $1)", [](f32 a, f32 b) { return std::fmod(a, b); }};
constexpr BinaryDef kAtan2{"atan($0, $1)", [](f32 y, f32 x) { return std::atan2(y, x); }};
// Step(Y, X): 1 where X >= Y.
constexpr BinaryDef kStep{"step($0, $1)", [](f32 y, f32 x) { return x >= y ? 1.0f : 0.0f; }};

// --- Three inputs --------------------------------------------------------------------
struct TernaryDef {
    const char* pattern;
    Lane3 fold;
};

template <const TernaryDef& Def>
i32 TernaryFn(Translator& t, const Node& n, const ExpressionDesc&, u32) {
    return Map3(t, n.id, In(t, n, 0), In(t, n, 1), In(t, n, 2), Def.pattern, Def.fold);
}

constexpr TernaryDef kClamp{"clamp($0, $1, $2)", [](f32 x, f32 lo, f32 hi) { return std::clamp(x, lo, std::max(lo, hi)); }};
constexpr TernaryDef kLerp{"mix($0, $1, $2)", [](f32 a, f32 b, f32 s) { return a + (b - a) * s; }};
constexpr TernaryDef kSmoothStep{"smoothstep($0, $1, $2)", [](f32 lo, f32 hi, f32 x) {
                                     const f32 s = std::clamp((x - lo) / (hi - lo), 0.0f, 1.0f);
                                     return s * s * (3.0f - 2.0f * s);
                                 }};

// --- Vector functions --------------------------------------------------------------------
f32 ConstLane(const Chunk& c, u32 i) {
    if (Width(c.type) == 1) return c.constant->x;
    const f32 lanes[4] = {c.constant->x, c.constant->y, c.constant->z, c.constant->w};
    return lanes[i];
}

i32 Dot(Translator& t, const Node& n, const ExpressionDesc&, u32) {
    i32 a = In(t, n, 0), b = In(t, n, 1);
    const u32 w = Unify(t, n.id, {&a, &b});
    if (w == 0) return -1;
    const Chunk ca = t.ChunkAt(a), cb = t.ChunkAt(b);
    if (ca.constant && cb.constant) {
        f32 sum = 0.0f;
        for (u32 i = 0; i < w; ++i) sum += ConstLane(ca, i) * ConstLane(cb, i);
        return t.Constant({sum, 0, 0, 0}, ValueType::Float1);
    }
    return t.Emit("dot(" + ca.code + ", " + cb.code + ")", ValueType::Float1, std::max(ca.freq, cb.freq));
}

i32 Cross(Translator& t, const Node& n, const ExpressionDesc&, u32) {
    const i32 a = t.Cast(In(t, n, 0), ValueType::Float3, n.id);
    const i32 b = t.Cast(In(t, n, 1), ValueType::Float3, n.id);
    if (a < 0 || b < 0) return -1;
    const Chunk ca = t.ChunkAt(a), cb = t.ChunkAt(b);
    if (ca.constant && cb.constant) {
        const Math::Vec4 x = *ca.constant, y = *cb.constant;
        return t.Constant({x.y * y.z - x.z * y.y, x.z * y.x - x.x * y.z, x.x * y.y - x.y * y.x, 0}, ValueType::Float3);
    }
    return t.Emit("cross(" + ca.code + ", " + cb.code + ")", ValueType::Float3, std::max(ca.freq, cb.freq));
}

i32 Normalize(Translator& t, const Node& n, const ExpressionDesc&, u32) {
    const i32 a = In(t, n, 0);
    if (a < 0) return -1;
    const Chunk c = t.ChunkAt(a);
    const u32 w = Width(c.type);
    if (w == 0) {
        t.Error(n.id, -1, "Normalize needs a float input");
        return -1;
    }
    if (c.constant) {
        f32 len = 0.0f;
        for (u32 i = 0; i < w; ++i) len += ConstLane(c, i) * ConstLane(c, i);
        len = std::sqrt(len);
        if (len > 0.0f) {
            f32 out[4] = {0, 0, 0, 0};
            for (u32 i = 0; i < w; ++i) out[i] = ConstLane(c, i) / len;
            return t.Constant({out[0], out[1], out[2], out[3]}, c.type);
        }
    }
    return t.Emit("normalize(" + c.code + ")", c.type, c.freq);
}

i32 Length(Translator& t, const Node& n, const ExpressionDesc&, u32) {
    const i32 a = In(t, n, 0);
    if (a < 0) return -1;
    const Chunk c = t.ChunkAt(a);
    const u32 w = Width(c.type);
    if (w == 0) {
        t.Error(n.id, -1, "Length needs a float input");
        return -1;
    }
    if (c.constant) {
        f32 len = 0.0f;
        for (u32 i = 0; i < w; ++i) len += ConstLane(c, i) * ConstLane(c, i);
        return t.Constant({std::sqrt(len), 0, 0, 0}, ValueType::Float1);
    }
    return t.Emit("length(" + c.code + ")", ValueType::Float1, c.freq);
}

i32 Distance(Translator& t, const Node& n, const ExpressionDesc&, u32) {
    i32 a = In(t, n, 0), b = In(t, n, 1);
    const u32 w = Unify(t, n.id, {&a, &b});
    if (w == 0) return -1;
    const Chunk ca = t.ChunkAt(a), cb = t.ChunkAt(b);
    if (ca.constant && cb.constant) {
        f32 sum = 0.0f;
        for (u32 i = 0; i < w; ++i) {
            const f32 d = ConstLane(ca, i) - ConstLane(cb, i);
            sum += d * d;
        }
        return t.Constant({std::sqrt(sum), 0, 0, 0}, ValueType::Float1);
    }
    return t.Emit("distance(" + ca.code + ", " + cb.code + ")", ValueType::Float1, std::max(ca.freq, cb.freq));
}

// Sine / Cosine: Input in "periods" (Period > 0) or radians (Period 0).
template <bool Cos>
i32 Periodic(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    i32 x = In(t, n, 0);
    const f32 period = static_cast<f32>(PropF(d, n, "period"));
    if (period > 0.0f) x = t.Binary("*", x, t.Constant({kTwoPi / period, 0, 0, 0}, ValueType::Float1), n.id);
    if constexpr (Cos) {
        return Map1(t, n.id, x, "cos($0)", [](f32 v) { return std::cos(v); });
    } else {
        return Map1(t, n.id, x, "sin($0)", [](f32 v) { return std::sin(v); });
    }
}

i32 If(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    const i32 a = t.Cast(In(t, n, 0), ValueType::Float1, n.id);
    const i32 b = t.Cast(In(t, n, 1), ValueType::Float1, n.id);
    i32 gt = In(t, n, 2), eq = In(t, n, 3), lt = In(t, n, 4);
    if (a < 0 || b < 0) return -1;
    if (Unify(t, n.id, {&gt, &eq, &lt}) == 0) return -1;
    const f32 threshold = static_cast<f32>(PropF(d, n, "threshold"));
    const Chunk ca = t.ChunkAt(a), cb = t.ChunkAt(b);
    if (ca.constant && cb.constant) {
        // A constant comparison keeps only the chosen branch.
        const f32 x = ca.constant->x, y = cb.constant->x;
        return x > y + threshold ? gt : (x < y - threshold ? lt : eq);
    }
    const std::string thr = GlslLiteral({threshold, 0, 0, 0}, ValueType::Float1);
    const Frequency freq = std::max({ca.freq, cb.freq, t.ChunkAt(gt).freq, t.ChunkAt(eq).freq, t.ChunkAt(lt).freq});
    return t.Emit("(" + ca.code + " > " + cb.code + " + " + thr + ") ? " + t.ChunkAt(gt).code + " : ((" + ca.code +
                      " < " + cb.code + " - " + thr + ") ? " + t.ChunkAt(lt).code + " : " + t.ChunkAt(eq).code + ")",
                  t.ChunkAt(gt).type, freq);
}

i32 Append(Translator& t, const Node& n, const ExpressionDesc&, u32) {
    return Concat(t, n.id, {In(t, n, 0), In(t, n, 1)});
}

i32 ComponentMask(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    const i32 v = In(t, n, 0);
    if (v < 0) return -1;
    const u32 w = Width(t.ChunkAt(v).type);
    std::string components;
    const char* keys[4] = {"r", "g", "b", "a"};
    for (u32 i = 0; i < 4; ++i) {
        if (!PropB(d, n, keys[i])) continue;
        if (i >= w) {
            t.Error(n.id, -1,
                    std::string("Component Mask selects ") + "RGBA"[i] + " but the input is a Float" +
                        std::to_string(w));
            return -1;
        }
        components += "xyzw"[i];
    }
    if (components.empty()) {
        t.Error(n.id, -1, "Component Mask selects no channels");
        return -1;
    }
    return Swizzle(t, v, components);
}

template <u32 N>
i32 BreakOut(Translator& t, const Node& n, const ExpressionDesc&, u32 outputPin) {
    const i32 v = t.Cast(In(t, n, 0), FloatType(N), n.id);
    if (v < 0 || outputPin >= N) return -1;
    return Swizzle(t, v, std::string(1, "xyzw"[outputPin]));
}

i32 MakeFloat2(Translator& t, const Node& n, const ExpressionDesc&, u32) {
    return Concat(t, n.id, {t.Cast(In(t, n, 0), ValueType::Float1, n.id), t.Cast(In(t, n, 1), ValueType::Float1, n.id)});
}
i32 MakeFloat3(Translator& t, const Node& n, const ExpressionDesc&, u32) {
    return Concat(t, n.id, {t.Cast(In(t, n, 0), ValueType::Float1, n.id), t.Cast(In(t, n, 1), ValueType::Float1, n.id),
                            t.Cast(In(t, n, 2), ValueType::Float1, n.id)});
}
i32 MakeFloat4(Translator& t, const Node& n, const ExpressionDesc&, u32) {
    return Concat(t, n.id, {t.Cast(In(t, n, 0), ValueType::Float1, n.id), t.Cast(In(t, n, 1), ValueType::Float1, n.id),
                            t.Cast(In(t, n, 2), ValueType::Float1, n.id), t.Cast(In(t, n, 3), ValueType::Float1, n.id)});
}

i32 Desaturation(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    const i32 color = t.Cast(In(t, n, 0), ValueType::Float3, n.id);
    const i32 fraction = t.Cast(In(t, n, 1), ValueType::Float1, n.id);
    if (color < 0 || fraction < 0) return -1;
    const Math::Vec4 weights = PropV(d, n, "luminanceFactors");
    const i32 lum = t.Constant(weights, ValueType::Float3);
    // gray = dot(color, weights); result = mix(color, gray, fraction)
    i32 gray = -1;
    {
        const Chunk c = t.ChunkAt(color);
        if (c.constant) {
            gray = t.Constant({c.constant->x * weights.x + c.constant->y * weights.y + c.constant->z * weights.z, 0, 0, 0},
                              ValueType::Float1);
        } else {
            gray = t.Emit("dot(" + c.code + ", " + t.ChunkAt(lum).code + ")", ValueType::Float1, c.freq);
        }
    }
    return Map3(t, n.id, color, gray, fraction, "mix($0, $1, $2)",
                [](f32 a, f32 b, f32 s) { return a + (b - a) * s; });
}

i32 RemapValueRange(Translator& t, const Node& n, const ExpressionDesc&, u32) {
    // TargetLow + (Input - InputLow) / (InputHigh - InputLow) * (TargetHigh - TargetLow)
    const i32 input = In(t, n, 0), inLow = In(t, n, 1), inHigh = In(t, n, 2);
    const i32 outLow = In(t, n, 3), outHigh = In(t, n, 4);
    const i32 s = t.Binary("/", t.Binary("-", input, inLow, n.id), t.Binary("-", inHigh, inLow, n.id), n.id);
    return t.Binary("+", outLow, t.Binary("*", s, t.Binary("-", outHigh, outLow, n.id), n.id), n.id);
}

}  // namespace

void RegisterMath(Table& table) {
    table["Add"] = &Arithmetic<'+'>;
    table["Subtract"] = &Arithmetic<'-'>;
    table["Multiply"] = &Arithmetic<'*'>;
    table["Divide"] = &Arithmetic<'/'>;
    table["Power"] = &BinaryFn<kPow>;
    table["Abs"] = &Unary<kAbs>;
    table["Min"] = &BinaryFn<kMin>;
    table["Max"] = &BinaryFn<kMax>;
    table["Clamp"] = &TernaryFn<kClamp>;
    table["Saturate"] = &Unary<kSaturate>;
    table["OneMinus"] = &Unary<kOneMinus>;
    table["Lerp"] = &TernaryFn<kLerp>;
    table["Dot"] = &Dot;
    table["Cross"] = &Cross;
    table["Normalize"] = &Normalize;
    table["Length"] = &Length;
    table["Distance"] = &Distance;
    table["SquareRoot"] = &Unary<kSqrt>;
    table["Exp"] = &Unary<kExp>;
    table["Log2"] = &Unary<kLog2>;
    table["Sine"] = &Periodic<false>;
    table["Cosine"] = &Periodic<true>;
    table["Tangent"] = &Unary<kTan>;
    table["Arcsine"] = &Unary<kAsin>;
    table["Arccosine"] = &Unary<kAcos>;
    table["Arctangent2"] = &BinaryFn<kAtan2>;
    table["Floor"] = &Unary<kFloor>;
    table["Ceil"] = &Unary<kCeil>;
    table["Round"] = &Unary<kRound>;
    table["Frac"] = &Unary<kFrac>;
    table["Fmod"] = &BinaryFn<kFmod>;
    table["Sign"] = &Unary<kSign>;
    table["Step"] = &BinaryFn<kStep>;
    table["SmoothStep"] = &TernaryFn<kSmoothStep>;
    table["If"] = &If;
    table["AppendVector"] = &Append;
    table["ComponentMask"] = &ComponentMask;
    table["BreakOutFloat2Components"] = &BreakOut<2>;
    table["BreakOutFloat3Components"] = &BreakOut<3>;
    table["BreakOutFloat4Components"] = &BreakOut<4>;
    table["MakeFloat2"] = &MakeFloat2;
    table["MakeFloat3"] = &MakeFloat3;
    table["MakeFloat4"] = &MakeFloat4;
    table["Desaturation"] = &Desaturation;
    table["RemapValueRange"] = &RemapValueRange;
}

}  // namespace Luma::MaterialCompiler::Detail
