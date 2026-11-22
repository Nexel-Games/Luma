// Texture and coordinate expressions.

#include <algorithm>

#include "Common.h"

namespace Luma::MaterialCompiler::Detail {
namespace {

using MaterialGraph::ExpressionDesc;
using MaterialGraph::Node;
using MaterialGraph::ValueType;

// --- Texture -----------------------------------------------------------------------

i32 TextureFromNode(Translator& t, const Node& n, const ExpressionDesc& d) {
    TextureSlot slot = SlotFromProperties(d, n);
    if (slot.defaultTexture.empty()) {
        t.Error(n.id, -1, d.displayName + " has no texture");
        return -1;
    }
    return t.Texture(std::move(slot), nullptr);
}

i32 TextureSample(Translator& t, const Node& n, const ExpressionDesc& d, u32 outputPin) {
    // Tex (pin 1) overrides the node's own texture.
    const i32 texture = t.Memo(n.id, "texture", [&]() -> i32 {
        if (!Linked(t, n, 1)) return TextureFromNode(t, n, d);
        const i32 tex = In(t, n, 1);
        if (tex < 0) return -1;
        const ValueType type = t.ChunkAt(tex).type;
        if (type != ValueType::Texture2D && type != ValueType::TextureCube) {
            t.Error(n.id, 1, "Tex needs a texture object");
            return -1;
        }
        return tex;
    });
    const auto type = static_cast<SamplerType>(std::clamp<i64>(PropI(d, n, "sampler"), 0, 4));
    return SampleOutput(t, n, d, outputPin, texture, type, SampleInputs{0, 2, 3, 4});
}

i32 TextureObject(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    TextureSlot slot;
    slot.defaultTexture = PropS(d, n, "texture");
    return t.Texture(std::move(slot), nullptr);
}

i32 TextureProperty(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    const i32 tex = In(t, n, 0);
    if (tex < 0) return -1;
    if (t.ChunkAt(tex).type != ValueType::Texture2D) {
        t.Error(n.id, 0, "Texture needs a 2D texture object");
        return -1;
    }
    const std::string size = "MTexSize(" + t.ChunkAt(tex).code + ")";
    const bool texel = PropI(d, n, "property") == 1;
    return t.Reference(texel ? "(1.0 / " + size + ")" : size, ValueType::Float2, Frequency::Uniform);
}

// --- Coordinates ---------------------------------------------------------------------

i32 Varying(Translator& t, const char* code, ValueType type) { return t.Reference(code, type, Frequency::Varying); }

i32 TextureCoordinate(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    const i64 index = std::clamp<i64>(PropI(d, n, "index"), 0, 7);
    i32 uv = t.Reference("P.texcoords[" + std::to_string(index) + "]", ValueType::Float2, Frequency::Varying);
    const bool unU = PropB(d, n, "unMirrorU"), unV = PropB(d, n, "unMirrorV");
    if (unU || unV) {
        uv = t.Emit(std::string("LumaUnMirror(") + t.ChunkAt(uv).code + ", bvec2(" + (unU ? "true" : "false") + ", " +
                        (unV ? "true" : "false") + "))",
                    ValueType::Float2, Frequency::Varying);
    }
    const f32 u = static_cast<f32>(PropF(d, n, "uTiling")), v = static_cast<f32>(PropF(d, n, "vTiling"));
    if (u != 1.0f || v != 1.0f) uv = t.Binary("*", uv, t.Constant({u, v, 0, 0}, ValueType::Float2), n.id);
    return uv;
}

i32 Time(Translator& t) { return Varying(t, "MaterialTime()", ValueType::Float1); }

i32 Panner(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    const i32 coord = t.Cast(InOr(t, n, 0, DefaultUV(t)), ValueType::Float2, n.id);
    const i32 time = t.Cast(InOr(t, n, 1, Time(t)), ValueType::Float1, n.id);
    const i32 speed = Linked(t, n, 2)
                          ? t.Cast(In(t, n, 2), ValueType::Float2, n.id)
                          : t.Constant({static_cast<f32>(PropF(d, n, "speedX")), static_cast<f32>(PropF(d, n, "speedY")), 0, 0},
                                       ValueType::Float2);
    const i32 moved = t.Binary("+", coord, t.Binary("*", speed, time, n.id), n.id);
    if (moved < 0 || !PropB(d, n, "fractionalPart")) return moved;
    return Map1(t, n.id, moved, "fract($0)", nullptr);
}

i32 Rotator(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    const i32 coord = t.Cast(InOr(t, n, 0, DefaultUV(t)), ValueType::Float2, n.id);
    const i32 time = t.Cast(InOr(t, n, 1, Time(t)), ValueType::Float1, n.id);
    if (coord < 0 || time < 0) return -1;
    const std::string center = GlslLiteral(
        {static_cast<f32>(PropF(d, n, "centerX")), static_cast<f32>(PropF(d, n, "centerY")), 0, 0}, ValueType::Float2);
    const std::string speed = GlslLiteral({static_cast<f32>(PropF(d, n, "speed")), 0, 0, 0}, ValueType::Float1);
    return t.Emit("LumaRotator(" + t.ChunkAt(coord).code + ", " + t.ChunkAt(time).code + ", " + center + ", " + speed + ")",
                  ValueType::Float2, Frequency::Varying);
}

template <const char* Code, ValueType Type>
i32 Builtin(Translator& t, const Node&, const ExpressionDesc&, u32) {
    return Varying(t, Code, Type);
}

constexpr char kObjectPos[] = "P.objectPos";
constexpr char kObjectRadius[] = "P.objectRadius";
constexpr char kObjectBounds[] = "P.objectBounds";
constexpr char kCameraPos[] = "MaterialCameraPosition()";
constexpr char kCameraVector[] = "P.cameraVector";
constexpr char kVertexNormal[] = "P.worldNormal";
constexpr char kPixelNormal[] = "P.pixelNormal";
constexpr char kVertexTangent[] = "P.worldTangent";
constexpr char kReflection[] = "reflect(-P.cameraVector, P.worldNormal)";
constexpr char kViewSize[] = "MaterialViewSize()";
constexpr char kPixelDepth[] = "P.pixelDepth";
constexpr char kDeltaTime[] = "MaterialDeltaTime()";

i32 WorldPosition(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    (void)n;
    return Varying(t, PropI(d, n, "offsets") == 1 ? "P.worldPosNoOffset" : "P.worldPos", ValueType::Float3);
}

i32 ScreenPosition(Translator& t, const Node&, const ExpressionDesc&, u32 outputPin) {
    return Varying(t, outputPin == 0 ? "P.screenUV" : "P.pixelPos", ValueType::Float2);
}

bool SceneBuffersAvailable(const Translator& t) {
    const Material::MaterialSettings& s = t.MaterialAsset().settings;
    return s.domain == Material::MaterialDomain::PostProcess || s.blendMode == Material::BlendMode::Translucent ||
           s.blendMode == Material::BlendMode::Additive || s.blendMode == Material::BlendMode::Modulate;
}

i32 SceneDepth(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    if (!SceneBuffersAvailable(t)) {
        t.Error(n.id, -1, d.displayName + " can only be used in translucent or post-process materials");
        return -1;
    }
    const i32 uv = t.Cast(InOr(t, n, 0, Varying(t, "P.screenUV", ValueType::Float2)), ValueType::Float2, n.id);
    if (uv < 0) return -1;
    return t.Emit("MSceneDepth(" + t.ChunkAt(uv).code + ")", ValueType::Float1, Frequency::Varying);
}

i32 TimeNode(Translator& t, const Node& n, const ExpressionDesc& d, u32) {
    (void)n;
    const f32 period = static_cast<f32>(PropF(d, n, "period"));
    if (period <= 0.0f) return Time(t);
    return t.Emit("mod(MaterialTime(), " + GlslLiteral({period, 0, 0, 0}, ValueType::Float1) + ")", ValueType::Float1,
                  Frequency::Varying);
}

i32 VertexColor(Translator& t, const Node&, const ExpressionDesc&, u32 outputPin) {
    return ColorOutput(t, Varying(t, "P.vertexColor", ValueType::Float4), outputPin);
}

}  // namespace

void RegisterTextureCoordinates(Table& table) {
    table["TextureSample"] = &TextureSample;
    table["TextureObject"] = &TextureObject;
    table["TextureProperty"] = &TextureProperty;
    table["TextureCoordinate"] = &TextureCoordinate;
    table["Panner"] = &Panner;
    table["Rotator"] = &Rotator;
    table["WorldPosition"] = &WorldPosition;
    table["ObjectPosition"] = &Builtin<kObjectPos, ValueType::Float3>;
    table["ObjectRadius"] = &Builtin<kObjectRadius, ValueType::Float1>;
    table["ObjectBounds"] = &Builtin<kObjectBounds, ValueType::Float3>;
    table["ActorPosition"] = &Builtin<kObjectPos, ValueType::Float3>;
    table["CameraPositionWS"] = &Builtin<kCameraPos, ValueType::Float3>;
    table["CameraVectorWS"] = &Builtin<kCameraVector, ValueType::Float3>;
    table["VertexNormalWS"] = &Builtin<kVertexNormal, ValueType::Float3>;
    table["PixelNormalWS"] = &Builtin<kPixelNormal, ValueType::Float3>;
    table["VertexTangentWS"] = &Builtin<kVertexTangent, ValueType::Float3>;
    table["ReflectionVectorWS"] = &Builtin<kReflection, ValueType::Float3>;
    table["ScreenPosition"] = &ScreenPosition;
    table["ViewSize"] = &Builtin<kViewSize, ValueType::Float2>;
    table["PixelDepth"] = &Builtin<kPixelDepth, ValueType::Float1>;
    table["SceneDepth"] = &SceneDepth;
    table["Time"] = &TimeNode;
    table["DeltaTime"] = &Builtin<kDeltaTime, ValueType::Float1>;
    table["VertexColor"] = &VertexColor;
}

}  // namespace Luma::MaterialCompiler::Detail
