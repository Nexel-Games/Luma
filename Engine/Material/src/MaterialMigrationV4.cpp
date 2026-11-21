#include "Luma/Material/MaterialMigrationV4.h"

#include <string_view>

#include "Luma/MaterialGraph/ExpressionRegistry.h"

namespace Luma::Material {

using MaterialGraph::Graph;
using MaterialGraph::NodeId;
using MaterialGraph::PropertyValue;

namespace {

// Pin indices of the expressions used here (see the catalogue registration).
namespace Pin {
constexpr u32 kRGB = 0, kR = 1, kG = 2, kB = 3, kA = 4;    // sample / vector parameter outputs
constexpr u32 kA_ = 0, kB_ = 1;                            // binary math inputs
constexpr u32 kSwitchTrue = 0, kSwitchFalse = 1;
constexpr u32 kSampleUVs = 0;
constexpr u32 kPomHeightmap = 0, kPomRatio = 1, kPomUVs = 4, kPomOutUVs = 0;
constexpr u32 kNfhHeightMap = 0, kNfhIntensity = 1, kNfhUVs = 2;
}  // namespace Pin

// Sampler Type enum indices (TextureSample* "sampler").
constexpr i64 kSamplerColor = 0, kSamplerLinear = 1, kSamplerNormal = 2;

u32 OutputPin(std::string_view name) {
    const auto* d = MaterialGraph::ExpressionRegistry::Get().Find(Graph::kOutputType);
    for (u32 i = 0; i < d->inputs.size(); ++i) {
        if (d->inputs[i].name == name) return i;
    }
    return 0;
}

std::string Name(MaterialParameter p) { return ParameterDisplayName(p); }

class Builder {
public:
    explicit Builder(Graph& g) : m_g(g) {}

    NodeId Node(const char* type, f32 x, f32 y) { return m_g.AddNode(type, {x, y}); }
    void Set(NodeId n, const char* key, PropertyValue v) { m_g.Find(n)->properties[key] = std::move(v); }
    void Link(NodeId from, u32 fromPin, NodeId to, u32 toPin) {
        m_g.Connect({from, fromPin}, {to, toPin}, nullptr);
    }
    NodeId Scalar(const std::string& name, f32 value, f32 x, f32 y, f32 sliderMin = 0, f32 sliderMax = 0,
                  const char* group = "Migrated") {
        const NodeId n = Node("ScalarParameter", x, y);
        Set(n, "name", PropertyValue(name));
        Set(n, "default", PropertyValue(static_cast<f64>(value)));
        Set(n, "group", PropertyValue(group));
        if (sliderMax > sliderMin) {
            Set(n, "sliderMin", PropertyValue(static_cast<f64>(sliderMin)));
            Set(n, "sliderMax", PropertyValue(static_cast<f64>(sliderMax)));
        }
        return n;
    }
    NodeId Vector(const std::string& name, Math::Vec4 value, f32 x, f32 y, const char* group = "Migrated") {
        const NodeId n = Node("VectorParameter", x, y);
        Set(n, "name", PropertyValue(name));
        Set(n, "default", PropertyValue(value));
        Set(n, "group", PropertyValue(group));
        return n;
    }
    NodeId Switch(const std::string& name, bool def, f32 x, f32 y) {
        const NodeId n = Node("StaticSwitchParameter", x, y);
        Set(n, "name", PropertyValue(name));
        Set(n, "default", PropertyValue(def));
        Set(n, "group", PropertyValue("Migrated"));
        return n;
    }
    NodeId Binary(const char* type, NodeId a, u32 aPin, NodeId b, u32 bPin, f32 x, f32 y) {
        const NodeId n = Node(type, x, y);
        Link(a, aPin, n, Pin::kA_);
        Link(b, bPin, n, Pin::kB_);
        return n;
    }
    // Float2 from a vector parameter's RGB output (tiling / offset).
    NodeId XY(NodeId vectorParam, f32 x, f32 y) {
        const NodeId m = Node("ComponentMask", x, y);
        Set(m, "r", PropertyValue(true));
        Set(m, "g", PropertyValue(true));
        Set(m, "b", PropertyValue(false));
        Set(m, "a", PropertyValue(false));
        Link(vectorParam, Pin::kRGB, m, 0);
        return m;
    }

private:
    Graph& m_g;
};

// Output pin of a texture sample for a Phase-1 channel on a scalar input.
u32 ScalarChannelPin(TextureChannel c) {
    switch (c) {
        case TextureChannel::G: return Pin::kG;
        case TextureChannel::B: return Pin::kB;
        case TextureChannel::A: return Pin::kA;
        case TextureChannel::R:
        case TextureChannel::RGB: break;  // Phase-1 read .x for RGB on scalars
    }
    return Pin::kR;
}

const char* ChannelText(TextureChannel c) {
    switch (c) {
        case TextureChannel::R: return "R";
        case TextureChannel::G: return "G";
        case TextureChannel::B: return "B";
        case TextureChannel::A: return "A";
        case TextureChannel::RGB: break;
    }
    return "RGB";
}

struct Migrator {
    const Material& old;
    std::vector<std::string>* notes;
    GraphMaterial out;
    Builder b{out.graph};
    NodeId baseUV = 0;     // Texture Coordinate 0
    NodeId uvSource = 0;   // base or parallax-displaced UV (switch)
    u32 uvSourcePin = 0;

    void Note(std::string s) {
        if (notes) notes->push_back(std::move(s));
    }
    const MaterialInput& In(MaterialParameter p) const { return old.Input(p); }
    static f32 Row(MaterialParameter p) { return static_cast<f32>(p) * 300.0f; }

    // UV for `p`: uvSource * Tiling + Offset (both instance-tunable parameters).
    NodeId UVFor(MaterialParameter p, f32 y) {
        const MaterialInput& in = In(p);
        const std::string n = Name(p);
        const NodeId tiling = b.Vector(n + " Tiling", {in.uvTiling.x, in.uvTiling.y, 0, 0}, -1500, y);
        const NodeId offset = b.Vector(n + " Offset", {in.uvOffset.x, in.uvOffset.y, 0, 0}, -1500, y + 110);
        const NodeId mul = b.Binary("Multiply", uvSource, uvSourcePin, b.XY(tiling, -1300, y), 0, -1150, y);
        return b.Binary("Add", mul, 0, b.XY(offset, -1300, y + 110), 0, -1000, y);
    }

    // Texture branch of a color / scalar input (before the switch).
    NodeId TexturedBranch(MaterialParameter p, f32 y, bool color) {
        const MaterialInput& in = In(p);
        const std::string n = Name(p);
        const NodeId sample = b.Node("TextureSampleParameter2D", -850, y);
        b.Set(sample, "name", PropertyValue(n + " Texture"));
        b.Set(sample, "texture", PropertyValue(in.texture));
        b.Set(sample, "group", PropertyValue("Migrated"));
        b.Set(sample, "sampler", PropertyValue(color ? kSamplerColor : kSamplerLinear));
        b.Link(UVFor(p, y), 0, sample, Pin::kSampleUVs);
        NodeId value = sample;
        u32 valuePin = color ? Pin::kRGB : ScalarChannelPin(in.channel);
        if (color && in.channel != TextureChannel::RGB) {
            Note(n + ": channel " + ChannelText(in.channel) + " on a colour input reads that channel only");
            valuePin = ScalarChannelPin(in.channel);
        }
        if (in.invert) {
            const NodeId inv = b.Node("OneMinus", -650, y);
            b.Link(value, valuePin, inv, 0);
            value = inv;
            valuePin = 0;
        }
        const NodeId tint = color ? b.Vector(n + " Tint", in.tint, -650, y + 120)
                                  : b.Scalar(n + " Scale", in.tint.x, -650, y + 120, 0, 4);
        return b.Binary("Multiply", value, valuePin, tint, 0, -480, y);
    }

    // A color / scalar input with both branches behind "Use <N> Texture".
    NodeId Switchable(MaterialParameter p, bool color) {
        const MaterialInput& in = In(p);
        const std::string n = Name(p);
        const f32 y = Row(p);
        const NodeId textured = TexturedBranch(p, y, color);
        const NodeId constant = color ? b.Vector(n, in.constant, -480, y + 150)
                                      : b.Scalar(n, in.constant.x, -480, y + 150, 0, 1);
        const NodeId sw = b.Switch("Use " + n + " Texture", !in.texture.empty(), -300, y);
        b.Link(textured, 0, sw, Pin::kSwitchTrue);
        b.Link(constant, 0, sw, Pin::kSwitchFalse);
        Note(n + (in.texture.empty() ? ": constant" : ": texture " + in.texture) +
             " -> parameters '" + n + "'");
        return sw;
    }

    // Height: one Parallax Occlusion Mapping node shared by every sample.
    void BuildParallax() {
        const MaterialInput& h = In(MaterialParameter::Height);
        baseUV = b.Node("TextureCoordinate", -2200, -300);
        uvSource = baseUV;
        uvSourcePin = 0;
        const f32 y = -600;
        const NodeId tiling = b.Vector("Height Tiling", {h.uvTiling.x, h.uvTiling.y, 0, 0}, -2200, y);
        const NodeId offset = b.Vector("Height Offset", {h.uvOffset.x, h.uvOffset.y, 0, 0}, -2200, y + 110);
        const NodeId tilingXY = b.XY(tiling, -2050, y);
        const NodeId offsetXY = b.XY(offset, -2050, y + 110);
        // POM marches in the height texture's tiled space ...
        const NodeId heightUV = b.Binary("Add", b.Binary("Multiply", baseUV, 0, tilingXY, 0, -1900, y), 0,
                                         offsetXY, 0, -1800, y);
        const NodeId heightTex = b.Node("TextureObjectParameter", -1900, y - 150);
        b.Set(heightTex, "name", PropertyValue(std::string("Height Texture")));
        b.Set(heightTex, "texture", PropertyValue(h.texture));
        b.Set(heightTex, "group", PropertyValue("Migrated"));
        // ... so its ratio is the Phase-1 scale (base-UV units) times the U tiling.
        const NodeId scale = b.Scalar("Height Scale", h.normalStrength, -1900, y + 220, 0, 0.3f);
        const NodeId tilingU = b.Node("ComponentMask", -1800, y + 300);
        b.Set(tilingU, "r", PropertyValue(true));
        b.Set(tilingU, "g", PropertyValue(false));
        b.Set(tilingU, "b", PropertyValue(false));
        b.Set(tilingU, "a", PropertyValue(false));
        b.Link(tiling, Pin::kRGB, tilingU, 0);
        const NodeId ratio = b.Binary("Multiply", scale, 0, tilingU, 0, -1700, y + 220);
        const NodeId pom = b.Node("ParallaxOcclusionMapping", -1600, y);
        b.Set(pom, "channel", PropertyValue(static_cast<i64>(ScalarChannelPin(h.channel) - 1)));
        b.Link(heightTex, 0, pom, Pin::kPomHeightmap);
        b.Link(ratio, 0, pom, Pin::kPomRatio);
        b.Link(heightUV, 0, pom, Pin::kPomUVs);
        // Back to base-UV space: (displaced - offset) / tiling.
        const NodeId back = b.Binary("Divide", b.Binary("Subtract", pom, Pin::kPomOutUVs, offsetXY, 0, -1450, y), 0,
                                     tilingXY, 0, -1350, y);
        const NodeId sw = b.Switch("Use Height Texture", !h.texture.empty(), -1250, -300);
        b.Link(back, 0, sw, Pin::kSwitchTrue);
        b.Link(baseUV, 0, sw, Pin::kSwitchFalse);
        uvSource = sw;
        uvSourcePin = 0;
        if (h.uvTiling.x != h.uvTiling.y) {
            Note("Height: non-uniform tiling — parallax depth uses the U tiling");
        }
        if (h.invert) Note("Height: invert isn't supported by the parallax node; the height is used as-is");
        Note(std::string("Height: ") + (h.texture.empty() ? "none" : h.texture) +
             " -> Parallax Occlusion Mapping ('Height Texture', 'Height Scale')");
    }

    NodeId BuildNormal() {
        const MaterialParameter p = MaterialParameter::Normal;
        const MaterialInput& in = In(p);
        const f32 y = Row(p);
        const NodeId sample = b.Node("TextureSampleParameter2D", -850, y);
        b.Set(sample, "name", PropertyValue(std::string("Normal Texture")));
        b.Set(sample, "texture", PropertyValue(in.texture));
        b.Set(sample, "group", PropertyValue("Migrated"));
        b.Set(sample, "sampler", PropertyValue(kSamplerNormal));
        b.Link(UVFor(p, y), 0, sample, Pin::kSampleUVs);
        NodeId n = sample;
        if (in.flipGreen) {
            const NodeId flip = b.Node("Constant3Vector", -700, y + 120);
            b.Set(flip, "value", PropertyValue(Math::Vec4{1, -1, 1, 0}));
            n = b.Binary("Multiply", sample, Pin::kRGB, flip, 0, -650, y);
        }
        // xy *= strength, then renormalise.
        const NodeId xy = b.Node("ComponentMask", -550, y);
        b.Set(xy, "r", PropertyValue(true));
        b.Set(xy, "g", PropertyValue(true));
        b.Set(xy, "b", PropertyValue(false));
        b.Set(xy, "a", PropertyValue(false));
        b.Link(n, n == sample ? Pin::kRGB : 0, xy, 0);
        const NodeId z = b.Node("ComponentMask", -550, y + 90);
        b.Set(z, "r", PropertyValue(false));
        b.Set(z, "g", PropertyValue(false));
        b.Set(z, "b", PropertyValue(true));
        b.Set(z, "a", PropertyValue(false));
        b.Link(n, n == sample ? Pin::kRGB : 0, z, 0);
        const NodeId strength = b.Scalar("Normal Strength", in.normalStrength, -550, y + 180, 0, 8);
        const NodeId scaled = b.Binary("Multiply", xy, 0, strength, 0, -420, y);
        const NodeId append = b.Binary("AppendVector", scaled, 0, z, 0, -330, y);
        const NodeId normalized = b.Node("Normalize", -250, y);
        b.Link(append, 0, normalized, 0);
        const NodeId constant = b.Vector("Normal", in.constant, -330, y + 220);
        const NodeId sw = b.Switch("Use Normal Texture", !in.texture.empty(), -150, y);
        b.Link(normalized, 0, sw, Pin::kSwitchTrue);
        b.Link(constant, Pin::kRGB, sw, Pin::kSwitchFalse);
        Note(std::string("Normal: ") + (in.texture.empty() ? "constant" : in.texture) +
             " -> 'Normal Texture', 'Normal Strength'" + (in.flipGreen ? " (green flipped)" : ""));
        return sw;
    }

    // Bump: Normal From Heightmap blended onto the normal.
    NodeId BuildBump(NodeId normal) {
        const MaterialParameter p = MaterialParameter::Bump;
        const MaterialInput& in = In(p);
        const f32 y = Row(p);
        const NodeId tex = b.Node("TextureObjectParameter", -850, y);
        b.Set(tex, "name", PropertyValue(std::string("Bump Texture")));
        b.Set(tex, "texture", PropertyValue(in.texture));
        b.Set(tex, "group", PropertyValue("Migrated"));
        const NodeId strength = b.Scalar("Bump Strength", in.normalStrength, -850, y + 120, 0, 16);
        const NodeId nfh = b.Node("NormalFromHeight", -650, y);
        b.Link(tex, 0, nfh, Pin::kNfhHeightMap);
        b.Link(strength, 0, nfh, Pin::kNfhIntensity);
        b.Link(UVFor(p, y), 0, nfh, Pin::kNfhUVs);
        const NodeId blend = b.Binary("BlendAngleCorrectedNormals", normal, 0, nfh, 0, -150, y);
        const NodeId sw = b.Switch("Use Bump Texture", !in.texture.empty(), 0, y);
        b.Link(blend, 0, sw, Pin::kSwitchTrue);
        b.Link(normal, 0, sw, Pin::kSwitchFalse);
        if (in.invert || (in.channel != TextureChannel::R && in.channel != TextureChannel::RGB) ||
            in.tint.x != 1.0f) {
            Note("Bump: channel/invert/tint are folded into 'Bump Strength' only partially — check the result");
        }
        Note(std::string("Bump: ") + (in.texture.empty() ? "none" : in.texture) +
             " -> Normal From Heightmap ('Bump Texture', 'Bump Strength')");
        return sw;
    }

    GraphMaterial Run() {
        out.name = old.name;
        out.settings.blendMode = old.settings.blendMode;
        out.settings.shadingModel = old.settings.shadingModel;
        out.settings.twoSided = old.settings.twoSided;
        out.settings.opacityMaskClip = old.settings.opacityMaskClip;
        Graph& g = out.graph;
        const NodeId output = g.OutputNode();
        g.Find(output)->position = {300, 0};

        BuildParallax();
        auto wire = [&](NodeId from, const char* pin) { b.Link(from, 0, output, OutputPin(pin)); };
        wire(Switchable(MaterialParameter::BaseColor, true), "BaseColor");
        wire(Switchable(MaterialParameter::Metallic, false), "Metallic");
        wire(Switchable(MaterialParameter::Specular, false), "Specular");
        wire(Switchable(MaterialParameter::Roughness, false), "Roughness");
        wire(BuildBump(BuildNormal()), "Normal");
        {
            const NodeId emissive = Switchable(MaterialParameter::EmissiveColor, true);
            const f32 y = Row(MaterialParameter::EmissiveColor);
            const NodeId intensity = b.Scalar("Emissive Intensity",
                                              In(MaterialParameter::EmissiveColor).emissiveIntensity,
                                              -300, y + 150, 0, 100);
            wire(b.Binary("Multiply", emissive, 0, intensity, 0, -100, y), "EmissiveColor");
        }
        {
            const NodeId ao = Switchable(MaterialParameter::AmbientOcclusion, false);
            const NodeId cavity = Switchable(MaterialParameter::Cavity, false);
            wire(b.Binary("Multiply", ao, 0, cavity, 0, -100, Row(MaterialParameter::AmbientOcclusion)),
                 "AmbientOcclusion");
        }
        wire(Switchable(MaterialParameter::OpacityMask, false), "OpacityMask");
        return std::move(out);
    }
};

}  // namespace

GraphMaterial MigrateV3(const Material& old, std::vector<std::string>* notes) {
    Migrator m{old, notes};
    if (notes) notes->push_back("Converted '" + old.name + "' from the Phase-1 format to a node graph");
    return m.Run();
}

GraphMaterialInstance MigrateInstanceV1(const MaterialInstance& instance, const Material& parent,
                                        std::vector<std::string>* notes) {
    GraphMaterialInstance out;
    out.name = instance.name;
    out.parent = instance.parent;
    out.twoSided = instance.twoSided;
    out.opacityMaskClip = instance.opacityMaskClip;
    auto note = [notes](std::string s) {
        if (notes) notes->push_back(std::move(s));
    };
    if (instance.blendMode && *instance.blendMode != parent.settings.blendMode) {
        note(instance.name + ": the blend mode override needs its own material (blend mode changes "
                             "the shader); the parent's blend mode is used");
    }
    using K = ParameterOverride::Kind;
    for (usize i = 0; i < kMaterialParameterCount; ++i) {
        const auto p = static_cast<MaterialParameter>(i);
        if (!instance.IsOverridden(p)) continue;
        const MaterialInput& v = instance.inputs[i];
        const MaterialInput& base = parent.Input(p);
        const std::string n = Name(p);
        const bool color = IsColorParameter(p);
        const bool constantless = p == MaterialParameter::Height || p == MaterialParameter::Bump;
        if (!constantless) {
            if (color || p == MaterialParameter::Normal) {
                out.Set({.name = n, .kind = K::Vector, .vector = v.constant});
            } else {
                out.Set({.name = n, .kind = K::Scalar, .scalar = v.constant.x});
            }
        }
        out.Set({.name = "Use " + n + " Texture", .kind = K::StaticSwitch, .staticSwitch = !v.texture.empty()});
        if (!v.texture.empty()) out.Set({.name = n + " Texture", .kind = K::Texture, .texture = v.texture});
        if (!constantless && p != MaterialParameter::Normal) {
            if (color) {
                out.Set({.name = n + " Tint", .kind = K::Vector, .vector = v.tint});
            } else {
                out.Set({.name = n + " Scale", .kind = K::Scalar, .scalar = v.tint.x});
            }
        }
        out.Set({.name = n + " Tiling", .kind = K::Vector, .vector = {v.uvTiling.x, v.uvTiling.y, 0, 0}});
        out.Set({.name = n + " Offset", .kind = K::Vector, .vector = {v.uvOffset.x, v.uvOffset.y, 0, 0}});
        if (p == MaterialParameter::Normal) out.Set({.name = "Normal Strength", .kind = K::Scalar, .scalar = v.normalStrength});
        if (p == MaterialParameter::Height) out.Set({.name = "Height Scale", .kind = K::Scalar, .scalar = v.normalStrength});
        if (p == MaterialParameter::Bump) out.Set({.name = "Bump Strength", .kind = K::Scalar, .scalar = v.normalStrength});
        if (p == MaterialParameter::EmissiveColor) {
            out.Set({.name = "Emissive Intensity", .kind = K::Scalar, .scalar = v.emissiveIntensity});
        }
        // Graph structure can't be overridden per instance.
        if (v.channel != base.channel) {
            note(instance.name + ": " + n + " channel override (" + ChannelText(v.channel) +
                 ") can't be a parameter; the parent's channel " + ChannelText(base.channel) + " is used");
        }
        if (v.invert != base.invert) {
            note(instance.name + ": " + n + " invert override can't be a parameter; the parent's setting is used");
        }
        if (v.flipGreen != base.flipGreen) {
            note(instance.name + ": " + n + " flip-green override can't be a parameter; the parent's setting is used");
        }
    }
    return out;
}

}  // namespace Luma::Material
