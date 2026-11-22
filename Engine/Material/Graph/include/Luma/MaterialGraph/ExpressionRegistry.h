#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Luma/MaterialGraph/Graph.h"

// The catalogue of material expressions (Unreal's UMaterialExpression
// classes): pins, properties, category, description and frequency rules for
// every node type. The compiler dispatches on ExpressionDesc::type; the graph
// editor builds its palette, node bodies and details panels from it.
//
// Unconnected input values: a node may store an override in its properties
// under InputPropertyKey(pinName) (Unreal's "Const A / Const B" fields);
// otherwise the pin's defaultValue is used.

namespace Luma::MaterialGraph {

struct PinDesc {
    std::string name;
    ValueType type = ValueType::Any;
    PropertyValue defaultValue;   // value when unconnected
    std::string tooltip;
    bool optional = true;         // false: compile error when unconnected
};

struct PropertyDesc {
    enum class Kind : u8 { Bool, Int, Float, Vec4, String, Enum, Asset, Code };
    std::string key, label, tooltip;
    Kind kind = Kind::Float;
    PropertyValue defaultValue;
    std::vector<std::string> enumValues;   // Kind::Enum (value stored as the index)
    f64 min = 0.0, max = 0.0;              // numeric range (0/0 = unbounded)
    std::string assetType;                 // Kind::Asset: "Texture", "MaterialFunction", "ParameterCollection"
};

struct ExpressionDesc {
    std::string type, displayName, category, description;
    std::vector<PinDesc> inputs, outputs;
    std::vector<PropertyDesc> properties;
    bool pixelOnly = false;        // can't be evaluated in the vertex shader
    bool hiddenInPalette = false;  // Material Output, Function Input/Output outside functions
    bool dynamicPins = false;      // pins come from properties/assets (Custom, Function Call)
    std::vector<std::string> keywords;
};

class ExpressionRegistry {
public:
    static const ExpressionRegistry& Get();
    const ExpressionDesc* Find(std::string_view type) const;
    // Sorted by category, then display name.
    std::vector<const ExpressionDesc*> All() const;

    void Add(ExpressionDesc desc);  // used by the category registration functions

private:
    ExpressionRegistry();
    std::vector<ExpressionDesc> m_descs;
};

// Property key holding a node's value for an unconnected input pin.
std::string InputPropertyKey(std::string_view pinName);
// The value an unconnected input pin evaluates to on `node`.
PropertyValue InputDefault(const ExpressionDesc& desc, const Node& node, u32 inputPin);
// Property value on `node`, or the descriptor default when unset.
PropertyValue PropertyOrDefault(const ExpressionDesc& desc, const Node& node, std::string_view key);

}  // namespace Luma::MaterialGraph
