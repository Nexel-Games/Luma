// Utility, function and post-process expressions.

#include <algorithm>
#include <array>
#include <sstream>

#include "Common.h"

namespace Luma::MaterialCompiler::Detail {
namespace {

using MaterialGraph::ExpressionDesc;
using MaterialGraph::Node;
using MaterialGraph::ValueType;

std::string Code(const Translator& t, i32 chunk) { return t.ChunkAt(chunk).code; }

std::string Trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::vector<std::string> SplitList(const std::string& text) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = Trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

i32 Fresnel(Translator& t, const Node& n, const ExpressionDesc&, u32) {
    const i32 exponent = t.Cast(In(t, n, 0), ValueType::Float1, n.id);
    const i32 base = t.Cast(In(t, n, 1), ValueType::Float1, n.id);
    const i32 normal = t.Cast(InOr(t, n, 2, t.Reference("P.worldNormal", ValueType::Float3, Frequency::Varying)),
                              ValueType::Float3, n.id);
    if (exponent < 0 || base < 0 || normal < 0) return -1;
    return t.Emit("LumaFresnel(" + Code(t, exponent) + ", " + Code(t, base) + ", " + Code(t, normal) +
                      ", P.cameraVector)",
                  ValueType::Float1, Frequency::Varying);
}

i32 BumpOffset(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    const i32 coord = t.Cast(InOr(t, n, 0, DefaultUV(t)), ValueType::Float2, n.id);
    const i32 height = t.Cast(In(t, n, 1), ValueType::Float1, n.id);
    const i32 ratio = t.Cast(In(t, n, 2), ValueType::Float1, n.id);
    if (coord < 0 || height < 0 || ratio < 0) return -1;
    const std::string plane = GlslLiteral({static_cast<f32>(PropF(d, n, "referencePlane")), 0, 0, 0}, ValueType::Float1);
    return t.Emit("LumaBumpOffset(" + Code(t, coord) + ", " + Code(t, height) + ", " + Code(t, ratio) + ", " + plane + ")",
                  ValueType::Float2, Frequency::Varying);
}

i32 Texture2DInput(Translator& t, const Node& n, u32 pin, const char* name) {
    const i32 tex = In(t, n, pin);
    if (tex < 0) return -1;
    if (t.ChunkAt(tex).type != ValueType::Texture2D) {
        t.Error(n.id, static_cast<i32>(pin), std::string(name) + " needs a 2D texture object");
        return -1;
    }
    return tex;
}

i32 ParallaxOcclusionMapping(Translator& t, const Node& n, const ExpressionDesc& d, u32 outputPin) {
    const i32 result = t.Memo(n.id, "pom", [&]() -> i32 {
        const i32 tex = Texture2DInput(t, n, 0, "Heightmap Texture");
        const i32 ratio = t.Cast(In(t, n, 1), ValueType::Float1, n.id);
        const i32 minSteps = t.Cast(In(t, n, 2), ValueType::Float1, n.id);
        const i32 maxSteps = t.Cast(In(t, n, 3), ValueType::Float1, n.id);
        const i32 uv = t.Cast(InOr(t, n, 4, DefaultUV(t)), ValueType::Float2, n.id);
        if (tex < 0 || ratio < 0 || minSteps < 0 || maxSteps < 0 || uv < 0) return -1;
        const std::string channel = std::to_string(std::clamp<i64>(PropI(d, n, "channel"), 0, 3));
        // xy = parallax UVs, z = pixel depth offset.
        return t.Emit("LumaParallaxOcclusion(" + Code(t, tex) + ", " + Code(t, uv) + ", " + Code(t, ratio) + ", " +
                          Code(t, minSteps) + ", " + Code(t, maxSteps) + ", " + channel + ")",
                      ValueType::Float3, Frequency::Varying);
    });
    if (result < 0) return -1;
    return Swizzle(t, result, outputPin == 0 ? "xy" : "z");
}

i32 NormalFromHeight(Translator& t, const Node& n, const ExpressionDesc&, u32) {
    const i32 tex = Texture2DInput(t, n, 0, "Height Map");
    const i32 intensity = t.Cast(In(t, n, 1), ValueType::Float1, n.id);
    const i32 uv = t.Cast(InOr(t, n, 2, DefaultUV(t)), ValueType::Float2, n.id);
    if (tex < 0 || intensity < 0 || uv < 0) return -1;
    return t.Emit("LumaNormalFromHeight(" + Code(t, tex) + ", " + Code(t, uv) + ", " + Code(t, intensity) + ")",
                  ValueType::Float3, Frequency::Varying);
}

i32 BlendAngleCorrectedNormals(Translator& t, const Node& n, const ExpressionDesc&, u32) {
    const std::array<i32, 2> args{t.Cast(In(t, n, 0), ValueType::Float3, n.id), t.Cast(In(t, n, 1), ValueType::Float3, n.id)};
    return t.Call("LumaBlendAngleCorrectedNormals", args, ValueType::Float3, n.id);
}

template <bool Position>
i32 Transform(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    const i32 v = t.Cast(In(t, n, 0), ValueType::Float3, n.id);
    if (v < 0) return -1;
    const i64 src = PropI(d, n, "source"), dst = PropI(d, n, "destination");
    if (src == dst) return v;
    return t.Emit(std::string(Position ? "LumaTransformPosition(" : "LumaTransformVector(") + Code(t, v) + ", " +
                      std::to_string(src) + ", " + std::to_string(dst) + ")",
                  ValueType::Float3, Frequency::Varying);
}

i32 Derivative(Translator& t, const Node& n, const char* fn) {
    const i32 v = In(t, n, 0);
    if (v < 0) return -1;
    const std::array<i32, 1> args{v};
    return t.Call(fn, args, t.ChunkAt(v).type, n.id, Frequency::Varying);
}
i32 Ddx(Translator& t, const Node& n, const ExpressionDesc&, u32) { return Derivative(t, n, "dFdx"); }
i32 Ddy(Translator& t, const Node& n, const ExpressionDesc&, u32) { return Derivative(t, n, "dFdy"); }

// Custom: the code becomes `<out> CustomExpressionN(<inputs>, inout <extra outputs>)`.
i32 Custom(Translator& t, const Node& n, const ExpressionDesc& d, u32 outputPin) {
    struct Extra {
        std::string name;
        ValueType type;
    };
    std::vector<Extra> extras;
    for (const std::string& entry : SplitList(PropS(d, n, "outputs"))) {
        const auto colon = entry.find(':');
        const std::string name = Trim(entry.substr(0, colon));
        const std::string type = colon == std::string::npos ? "Float1" : Trim(entry.substr(colon + 1));
        const u32 width = type.size() == 6 && type.starts_with("Float") ? static_cast<u32>(type[5] - '0') : 0;
        if (name.empty() || width < 1 || width > 4) {
            t.Error(n.id, -1, "Additional output '" + entry + "' must be Name:Float1..Float4");
            return -1;
        }
        extras.push_back({name, FloatType(width)});
    }
    const ValueType outType = FloatType(static_cast<u32>(std::clamp<i64>(PropI(d, n, "outputType"), 0, 3)) + 1);
    const i32 call = t.Memo(n.id, "call", [&]() -> i32 {
        const std::vector<std::string> inputs = SplitList(PropS(d, n, "inputs"));
        std::string params, args;
        for (u32 i = 0; i < inputs.size(); ++i) {
            const i32 v = In(t, n, i);
            if (v < 0) return -1;
            const Chunk c = t.ChunkAt(v);
            if (Width(c.type) == 0) {
                t.Error(n.id, static_cast<i32>(i), "Custom input '" + inputs[i] + "' must be a float value");
                return -1;
            }
            params += (params.empty() ? "" : ", ") + std::string(GlslType(c.type)) + " " + inputs[i];
            args += (args.empty() ? "" : ", ") + c.code;
        }
        const u32 index = t.FunctionCount();
        const std::string fn = "CustomExpression" + std::to_string(index);
        const std::string localPrefix = "custom" + std::to_string(n.id) + "_";
        for (const Extra& e : extras) {
            params += (params.empty() ? "" : ", ") + std::string("inout ") + GlslType(e.type) + " " + e.name;
            t.Statement(std::string(GlslType(e.type)) + " " + localPrefix + e.name + " = " +
                        GlslLiteral({0, 0, 0, 0}, e.type) + ";");
            args += (args.empty() ? "" : ", ") + localPrefix + e.name;
        }
        t.AddFunction(std::string(GlslType(outType)) + " " + fn + "(" + params + ")\n{\n" + PropS(d, n, "code") + "\n}");
        for (const std::string& inc : SplitList(PropS(d, n, "includes"))) t.AddInclude(inc);
        return t.Emit(fn + "(" + args + ")", outType, Frequency::Varying);
    });
    if (call < 0) return -1;
    if (outputPin == 0) return call;
    if (outputPin - 1 >= extras.size()) {
        t.Error(n.id, -1, "Custom has no output " + std::to_string(outputPin));
        return -1;
    }
    // The extra outputs are locals the call above writes.
    const Extra& e = extras[outputPin - 1];
    return t.Reference("custom" + std::to_string(n.id) + "_" + e.name, e.type, Frequency::Varying);
}

i32 VertexInterpolator(Translator& t, const Node& n, const ExpressionDesc&, u32) {
    const i32 vs = t.CompileInput(n.id, 0, Stage::Vertex);
    if (vs < 0) return -1;
    const Chunk c = t.ChunkAt(vs);
    if (t.CurrentStage() == Stage::Vertex || c.freq != Frequency::Varying) return vs;
    if (Width(c.type) == 0) {
        t.Error(n.id, 0, "Vertex Interpolator needs a float value");
        return -1;
    }
    const u32 index = t.AddInterpolator(c.type, c.code);
    return t.Reference("lumaInterp" + std::to_string(index), c.type, Frequency::Varying);
}

i32 Passthrough(Translator& t, const Node& n, const ExpressionDesc&, u32) { return In(t, n, 0); }

i32 NamedRerouteUsage(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    const std::string name = PropS(d, n, "name");
    const auto* declDesc = MaterialGraph::ExpressionRegistry::Get().Find("NamedRerouteDeclaration");
    for (const Node& other : t.MaterialAsset().graph.Nodes()) {
        if (other.type == "NamedRerouteDeclaration" && declDesc && PropS(*declDesc, other, "name") == name) {
            return t.CompileOutput(other.id, 0, t.CurrentStage());
        }
    }
    t.Error(n.id, -1, "No Named Reroute Declaration called '" + name + "'");
    return -1;
}

i32 MaterialFunctionCall(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    const std::string path = PropS(d, n, "function");
    t.Error(n.id, -1, path.empty() ? "Material Function Call has no function"
                                   : "Material Function '" + path + "' was not found");
    return -1;
}

i32 FunctionPin(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    t.Error(n.id, -1, d.displayName + " can only be used inside a Material Function");
    return -1;
}

i32 SceneTexture(Translator& t, const Node& n, const ExpressionDesc& d, u32 outputPin) {
    const i64 id = std::clamp<i64>(PropI(d, n, "id"), 0, 8);
    const Material::MaterialSettings& s = t.MaterialAsset().settings;
    const bool post = s.domain == Material::MaterialDomain::PostProcess;
    const bool translucent = s.blendMode == Material::BlendMode::Translucent ||
                             s.blendMode == Material::BlendMode::Additive || s.blendMode == Material::BlendMode::Modulate;
    // Scene Color / Depth exist for translucency; the G-buffer only for post process.
    if (!post && !(translucent && id <= 1)) {
        t.Error(n.id, -1, "Scene Texture can only read this buffer in post-process materials");
        return -1;
    }
    const std::string ids = std::to_string(id);
    if (outputPin == 1) return t.Reference("MSceneTextureSize(" + ids + ")", ValueType::Float2, Frequency::Varying);
    if (outputPin == 2) {
        return t.Reference("(1.0 / MSceneTextureSize(" + ids + "))", ValueType::Float2, Frequency::Varying);
    }
    const i32 uv = t.Cast(InOr(t, n, 0, t.Reference("P.screenUV", ValueType::Float2, Frequency::Varying)),
                          ValueType::Float2, n.id);
    if (uv < 0) return -1;
    return t.Emit("MSceneTexture(" + ids + ", " + Code(t, uv) + ")", ValueType::Float4, Frequency::Varying);
}

i32 SceneColor(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    const Material::MaterialSettings& s = t.MaterialAsset().settings;
    const bool translucent = s.blendMode == Material::BlendMode::Translucent ||
                             s.blendMode == Material::BlendMode::Additive || s.blendMode == Material::BlendMode::Modulate;
    if (s.domain != Material::MaterialDomain::Surface || !translucent) {
        t.Error(n.id, -1, d.displayName + " can only be used in translucent materials");
        return -1;
    }
    const i32 offset = t.Cast(In(t, n, 0), ValueType::Float2, n.id);
    if (offset < 0) return -1;
    return t.Emit("MSceneColor(P.screenUV + " + Code(t, offset) + ")", ValueType::Float3, Frequency::Varying);
}

}  // namespace

void RegisterUtility(Table& table) {
    table["Fresnel"] = &Fresnel;
    table["BumpOffset"] = &BumpOffset;
    table["ParallaxOcclusionMapping"] = &ParallaxOcclusionMapping;
    table["NormalFromHeight"] = &NormalFromHeight;
    table["BlendAngleCorrectedNormals"] = &BlendAngleCorrectedNormals;
    table["Transform"] = &Transform<false>;
    table["TransformPosition"] = &Transform<true>;
    table["DDX"] = &Ddx;
    table["DDY"] = &Ddy;
    table["Custom"] = &Custom;
    table["VertexInterpolator"] = &VertexInterpolator;
    table["NamedRerouteDeclaration"] = &Passthrough;
    table["NamedRerouteUsage"] = &NamedRerouteUsage;
    table["Reroute"] = &Passthrough;
    table["MaterialFunctionCall"] = &MaterialFunctionCall;
    table["FunctionInput"] = &FunctionPin;
    table["FunctionOutput"] = &FunctionPin;
    table["SceneTexture"] = &SceneTexture;
    table["SceneColor"] = &SceneColor;
}

}  // namespace Luma::MaterialCompiler::Detail
