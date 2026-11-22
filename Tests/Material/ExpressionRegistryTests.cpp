// Material expression catalogue: every expression of the design's §2 exists
// with the right pins, defaults and properties.

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

#include "Luma/MaterialGraph/ExpressionRegistry.h"

using namespace Luma;
using namespace Luma::MaterialGraph;

namespace {
const std::vector<std::string> kCatalogue = {
    // Constants
    "Constant", "Constant2Vector", "Constant3Vector", "Constant4Vector", "StaticBool",
    // Parameters
    "ScalarParameter", "VectorParameter", "TextureSampleParameter2D", "TextureSampleParameterCube",
    "TextureObjectParameter", "StaticSwitchParameter", "StaticBoolParameter", "CollectionParameter",
    // Math
    "Add", "Subtract", "Multiply", "Divide", "Power", "Abs", "Min", "Max", "Clamp", "Saturate",
    "OneMinus", "Lerp", "Dot", "Cross", "Normalize", "Length", "Distance", "SquareRoot", "Exp",
    "Log2", "Sine", "Cosine", "Tangent", "Arcsine", "Arccosine", "Arctangent2", "Floor", "Ceil",
    "Round", "Frac", "Fmod", "Sign", "Step", "SmoothStep", "If", "AppendVector", "ComponentMask",
    "BreakOutFloat2Components", "BreakOutFloat3Components", "BreakOutFloat4Components",
    "MakeFloat2", "MakeFloat3", "MakeFloat4", "Desaturation", "RemapValueRange",
    // Texture
    "TextureSample", "TextureObject", "TextureProperty",
    // Coordinates
    "TextureCoordinate", "Panner", "Rotator", "WorldPosition", "ObjectPosition", "ObjectRadius",
    "ObjectBounds", "ActorPosition", "CameraPositionWS", "CameraVectorWS", "VertexNormalWS",
    "PixelNormalWS", "VertexTangentWS", "ReflectionVectorWS", "ScreenPosition", "ViewSize",
    "PixelDepth", "SceneDepth", "Time", "DeltaTime", "VertexColor",
    // Utility
    "Fresnel", "BumpOffset", "ParallaxOcclusionMapping", "NormalFromHeight",
    "BlendAngleCorrectedNormals", "Transform", "TransformPosition", "DDX", "DDY", "Custom",
    "VertexInterpolator", "NamedRerouteDeclaration", "NamedRerouteUsage", "Reroute",
    // Functions
    "MaterialFunctionCall", "FunctionInput", "FunctionOutput",
    // Post process
    "SceneTexture", "SceneColor",
    // Output
    "MaterialOutput",
};
}  // namespace

TEST_CASE("Every catalogue expression is registered", "[graph][registry]") {
    const auto& reg = ExpressionRegistry::Get();
    for (const std::string& type : kCatalogue) {
        INFO(type);
        const ExpressionDesc* d = reg.Find(type);
        REQUIRE(d);
        CHECK(d->type == type);
        CHECK_FALSE(d->displayName.empty());
        CHECK_FALSE(d->category.empty());
        CHECK_FALSE(d->description.empty());
        const bool sinkNode = type == "MaterialOutput" || type == "FunctionOutput";
        CHECK((sinkNode || !d->outputs.empty()));
    }
    CHECK(reg.All().size() == kCatalogue.size());
}

TEST_CASE("Material Output inputs and unconnected defaults", "[graph][registry]") {
    const ExpressionDesc* d = ExpressionRegistry::Get().Find("MaterialOutput");
    REQUIRE(d);
    const std::vector<std::string> names = {
        "BaseColor", "Metallic", "Specular", "Roughness", "Anisotropy", "EmissiveColor",
        "Opacity", "OpacityMask", "Normal", "Tangent", "WorldPositionOffset", "SubsurfaceColor",
        "CustomData0", "CustomData1", "AmbientOcclusion", "Refraction", "PixelDepthOffset",
        "CustomUV0", "CustomUV1", "CustomUV2", "CustomUV3", "CustomUV4", "CustomUV5",
        "CustomUV6", "CustomUV7"};
    REQUIRE(d->inputs.size() == names.size());
    for (usize i = 0; i < names.size(); ++i) CHECK(d->inputs[i].name == names[i]);
    auto def = [&](const char* n) {
        for (const PinDesc& p : d->inputs) {
            if (p.name == n) return p.defaultValue.AsVec4();
        }
        return Math::Vec4{-1, -1, -1, -1};
    };
    CHECK(def("BaseColor").x == 0.0f);
    CHECK(def("Specular").x == 0.5f);
    CHECK(def("Roughness").x == 0.5f);
    CHECK(def("Opacity").x == 1.0f);
    CHECK(def("OpacityMask").x == 1.0f);
    CHECK(def("Normal").z == 1.0f);
    CHECK(def("AmbientOcclusion").x == 1.0f);
    CHECK(d->inputs[0].type == ValueType::Float3);
    CHECK(d->inputs[17].type == ValueType::Float2);
    CHECK(d->hiddenInPalette);
}

TEST_CASE("Lerp and TextureSample descriptors", "[graph][registry]") {
    const auto& reg = ExpressionRegistry::Get();
    const ExpressionDesc* lerp = reg.Find("Lerp");
    REQUIRE(lerp);
    REQUIRE(lerp->inputs.size() == 3);
    CHECK(lerp->inputs[0].name == "A");
    CHECK(lerp->inputs[1].name == "B");
    CHECK(lerp->inputs[2].name == "Alpha");
    CHECK(lerp->inputs[0].defaultValue.AsFloat() == 0.0);
    CHECK(lerp->inputs[1].defaultValue.AsFloat() == 1.0);
    CHECK(lerp->inputs[2].defaultValue.AsFloat() == 0.5);

    const ExpressionDesc* tex = reg.Find("TextureSample");
    REQUIRE(tex);
    auto prop = [&](const char* key) -> const PropertyDesc* {
        for (const PropertyDesc& p : tex->properties) {
            if (p.key == key) return &p;
        }
        return nullptr;
    };
    REQUIRE(prop("sampler"));
    CHECK(prop("sampler")->enumValues ==
          std::vector<std::string>{"Color", "Linear", "Normal", "Masks", "LinearGrayscale"});
    REQUIRE(prop("address"));
    CHECK(prop("address")->enumValues == std::vector<std::string>{"Wrap", "Clamp", "Mirror"});
    REQUIRE(prop("filter"));
    CHECK(prop("filter")->enumValues == std::vector<std::string>{"Linear", "Nearest", "Anisotropic"});
    REQUIRE(prop("mipMode"));
    CHECK(prop("mipMode")->enumValues ==
          std::vector<std::string>{"Default", "Bias", "Level", "Derivatives"});
    REQUIRE(prop("texture"));
    CHECK(prop("texture")->kind == PropertyDesc::Kind::Asset);
    std::vector<std::string> outs;
    for (const PinDesc& p : tex->outputs) outs.push_back(p.name);
    CHECK(outs == std::vector<std::string>{"RGB", "R", "G", "B", "A", "RGBA"});
}

TEST_CASE("Pixel-only expressions are flagged", "[graph][registry]") {
    const auto& reg = ExpressionRegistry::Get();
    for (const char* t : {"DDX", "DDY", "ScreenPosition", "PixelDepth", "SceneDepth",
                          "SceneTexture", "SceneColor", "PixelNormalWS"}) {
        INFO(t);
        CHECK(reg.Find(t)->pixelOnly);
    }
    for (const char* t : {"Add", "Time", "WorldPosition", "Panner", "VertexNormalWS"}) {
        INFO(t);
        CHECK_FALSE(reg.Find(t)->pixelOnly);
    }
}

TEST_CASE("Catalogue is sorted, unique and searchable", "[graph][registry]") {
    const auto all = ExpressionRegistry::Get().All();
    std::set<std::string> seen;
    for (usize i = 0; i < all.size(); ++i) {
        CHECK(seen.insert(all[i]->type).second);
        if (i > 0) {
            const bool ordered = all[i - 1]->category < all[i]->category ||
                                 (all[i - 1]->category == all[i]->category &&
                                  all[i - 1]->displayName <= all[i]->displayName);
            CHECK(ordered);
        }
    }
    CHECK(ExpressionRegistry::Get().Find("NotAThing") == nullptr);
}

TEST_CASE("Unconnected input values come from node properties first", "[graph][registry]") {
    const ExpressionDesc* add = ExpressionRegistry::Get().Find("Add");
    REQUIRE(add);
    Node n;
    n.type = "Add";
    CHECK(InputDefault(*add, n, 1).AsFloat() == add->inputs[1].defaultValue.AsFloat());
    n.properties[InputPropertyKey("B")] = PropertyValue(4.0);
    CHECK(InputDefault(*add, n, 1).AsFloat() == 4.0);
}
