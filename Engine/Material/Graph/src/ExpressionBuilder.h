#pragma once

#include <initializer_list>

#include "Luma/MaterialGraph/ExpressionRegistry.h"

// Small fluent builder used by the per-category registration files.

namespace Luma::MaterialGraph::Detail {

using VT = ValueType;
using PK = PropertyDesc::Kind;

inline PropertyValue F(f64 v) { return PropertyValue(v); }
inline PropertyValue V(f32 x, f32 y, f32 z, f32 w) { return PropertyValue(Math::Vec4{x, y, z, w}); }
inline PropertyValue S(const char* s) { return PropertyValue(std::string(s)); }
inline PropertyValue B(bool b) { return PropertyValue(b); }
inline PropertyValue I(i64 i) { return PropertyValue(i); }

class Expr {
public:
    Expr(const char* type, const char* display, const char* category, const char* description) {
        m_d.type = type;
        m_d.displayName = display;
        m_d.category = category;
        m_d.description = description;
    }
    Expr& In(const char* name, VT type, PropertyValue def = F(0.0), const char* tip = "",
             bool optional = true) {
        m_d.inputs.push_back({name, type, std::move(def), tip, optional});
        return *this;
    }
    Expr& Out(const char* name, VT type, const char* tip = "") {
        m_d.outputs.push_back({name, type, F(0.0), tip, true});
        return *this;
    }
    Expr& Prop(const char* key, const char* label, PK kind, PropertyValue def, const char* tip = "",
               f64 min = 0.0, f64 max = 0.0) {
        PropertyDesc p;
        p.key = key;
        p.label = label;
        p.kind = kind;
        p.defaultValue = std::move(def);
        p.tooltip = tip;
        p.min = min;
        p.max = max;
        m_d.properties.push_back(std::move(p));
        return *this;
    }
    Expr& Enum(const char* key, const char* label, std::initializer_list<const char*> values,
               i64 def = 0, const char* tip = "") {
        PropertyDesc p;
        p.key = key;
        p.label = label;
        p.kind = PK::Enum;
        p.defaultValue = I(def);
        p.tooltip = tip;
        for (const char* v : values) p.enumValues.emplace_back(v);
        m_d.properties.push_back(std::move(p));
        return *this;
    }
    Expr& Asset(const char* key, const char* label, const char* assetType, const char* tip = "") {
        PropertyDesc p;
        p.key = key;
        p.label = label;
        p.kind = PK::Asset;
        p.defaultValue = S("");
        p.tooltip = tip;
        p.assetType = assetType;
        m_d.properties.push_back(std::move(p));
        return *this;
    }
    // Common parameter metadata (name, group, sort priority, description).
    Expr& ParamMeta(const char* defaultName) {
        Prop("name", "Parameter Name", PK::String, S(defaultName), "Name used by Material Instances.");
        Prop("group", "Group", PK::String, S("None"), "Groups parameters in the instance editor.");
        Prop("sortPriority", "Sort Priority", PK::Int, I(32), "Lower sorts first within the group.");
        Prop("description", "Description", PK::String, S(""), "Tooltip shown in the instance editor.");
        return *this;
    }
    Expr& PixelOnly() { m_d.pixelOnly = true; return *this; }
    Expr& Hidden() { m_d.hiddenInPalette = true; return *this; }
    Expr& Dynamic() { m_d.dynamicPins = true; return *this; }
    Expr& Keywords(std::initializer_list<const char*> k) {
        for (const char* s : k) m_d.keywords.emplace_back(s);
        return *this;
    }
    ExpressionDesc Build() { return std::move(m_d); }

private:
    ExpressionDesc m_d;
};

// Shared sampler properties of every texture-sampling expression.
inline Expr& SamplerProps(Expr& e) {
    e.Enum("sampler", "Sampler Type", {"Color", "Linear", "Normal", "Masks", "LinearGrayscale"}, 0,
           "How the texture is decoded: Color = sRGB, Normal = tangent-space normal map.");
    e.Enum("address", "Address", {"Wrap", "Clamp", "Mirror"}, 0);
    e.Enum("filter", "Filter", {"Linear", "Nearest", "Anisotropic"}, 2);
    e.Enum("mipMode", "MIP Value Mode", {"Default", "Bias", "Level", "Derivatives"}, 0);
    return e;
}

void RegisterConstants(ExpressionRegistry&);
void RegisterParameters(ExpressionRegistry&);
void RegisterMath(ExpressionRegistry&);
void RegisterTexture(ExpressionRegistry&);
void RegisterCoordinates(ExpressionRegistry&);
void RegisterUtility(ExpressionRegistry&);
void RegisterFunctions(ExpressionRegistry&);
void RegisterPostProcess(ExpressionRegistry&);
void RegisterOutput(ExpressionRegistry&);

}  // namespace Luma::MaterialGraph::Detail
