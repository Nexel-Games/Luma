#include <catch2/catch_test_macros.hpp>

#include "Luma/Serialization/Json.h"
#include "Luma/Serialization/Reflection.h"
#include "Luma/Serialization/SerialValue.h"

namespace {

enum class Shape { Circle, Square, Triangle };

struct Widget {
    std::string label = "unnamed";
    int count = 0;
    float weight = 1.0f;
    bool enabled = false;
    Shape shape = Shape::Circle;
};

}  // namespace

// Reflection registration for the test type.
namespace Luma {
template <>
const TypeInfo<Widget>& GetTypeInfo<Widget>() {
    static const TypeInfo<Widget> info = [] {
        TypeBuilder<Widget> b("Widget");
        b.Property("label", &Widget::label).Category("General");
        b.Property("count", &Widget::count).Range(0, 100);
        b.Property("weight", &Widget::weight).Tooltip("kg").DisplayName("Weight (kg)");
        b.Property("enabled", &Widget::enabled);
        b.Property("shape", &Widget::shape).Enum({"Circle", "Square", "Triangle"});
        return b.Build();
    }();
    return info;
}
}  // namespace Luma

using Luma::DeserializeObject;
using Luma::GetTypeInfo;
using Luma::SerializeObject;

TEST_CASE("Reflection exposes registered properties with metadata",
          "[serial][reflect]") {
    const auto& info = GetTypeInfo<Widget>();
    REQUIRE(info.Name() == "Widget");
    REQUIRE(info.Properties().size() == 5);
    REQUIRE(info.Properties()[0].name == "label");
    REQUIRE(info.Properties()[0].meta.category == "General");
    REQUIRE(info.Properties()[1].meta.hasRange);
    REQUIRE(info.Properties()[1].meta.rangeMax == 100);
    REQUIRE(info.Properties()[2].meta.tooltip == "kg");
}

TEST_CASE("SerializeObject writes each field by name", "[serial][reflect]") {
    Widget w;
    w.label = "hp bar";
    w.count = 7;
    w.weight = 2.5f;
    w.enabled = true;
    w.shape = Shape::Triangle;

    Luma::SerialValue s = SerializeObject(w);
    REQUIRE(s.IsObject());
    REQUIRE(s.Find("label")->AsString() == "hp bar");
    REQUIRE(s.Find("count")->AsInt() == 7);
    REQUIRE(s.Find("weight")->AsFloat() == 2.5);
    REQUIRE(s.Find("enabled")->AsBool() == true);
    REQUIRE(s.Find("shape")->AsInt() == 2);  // enum stored by underlying value
}

TEST_CASE("DeserializeObject restores fields and round-trips",
          "[serial][reflect]") {
    Widget original;
    original.label = "loot";
    original.count = 42;
    original.weight = 0.25f;
    original.enabled = true;
    original.shape = Shape::Square;

    const std::string json = Luma::WriteJson(SerializeObject(original));
    auto parsed = Luma::ParseJson(json);
    REQUIRE(parsed.has_value());

    Widget restored;
    DeserializeObject(*parsed, restored);
    REQUIRE(restored.label == "loot");
    REQUIRE(restored.count == 42);
    REQUIRE(restored.weight == 0.25f);
    REQUIRE(restored.enabled == true);
    REQUIRE(restored.shape == Shape::Square);
}

TEST_CASE("DeserializeObject keeps defaults for missing fields",
          "[serial][reflect]") {
    // A partial/old document: only "count" present.
    auto parsed = Luma::ParseJson(R"({"count": 9})");
    REQUIRE(parsed.has_value());

    Widget w;  // defaults: label="unnamed", weight=1, enabled=false
    DeserializeObject(*parsed, w);
    REQUIRE(w.count == 9);
    REQUIRE(w.label == "unnamed");
    REQUIRE(w.weight == 1.0f);
    REQUIRE(w.enabled == false);
}

TEST_CASE("Reflection exposes UI metadata through the type-erased view",
          "[serial][reflect]") {
    const Luma::TypeInfoBase& info = GetTypeInfo<Widget>();
    REQUIRE(info.Name() == "Widget");
    REQUIRE(info.PropertyCount() == 5);
    const std::size_t shape = info.FindProperty("shape");
    REQUIRE(shape == 4);
    REQUIRE(info.PropertyMetaAt(shape).enumNames ==
            std::vector<std::string>{"Circle", "Square", "Triangle"});
    REQUIRE(info.PropertyMetaAt(info.FindProperty("weight")).displayName == "Weight (kg)");
    REQUIRE(info.FindProperty("missing") == info.PropertyCount());
}

TEST_CASE("PrettyPropertyName turns identifiers into labels", "[serial][reflect]") {
    REQUIRE(Luma::PrettyPropertyName("degreesPerSecond") == "Degrees Per Second");
    REQUIRE(Luma::PrettyPropertyName("castShadows") == "Cast Shadows");
    REQUIRE(Luma::PrettyPropertyName("shadow_map_size") == "Shadow Map Size");
    REQUIRE(Luma::PrettyPropertyName("cascade2Split") == "Cascade 2 Split");
    REQUIRE(Luma::PrettyPropertyName("name") == "Name");
    REQUIRE(Luma::PrettyPropertyName("") == "");
}
