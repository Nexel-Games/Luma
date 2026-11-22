#include "Luma/MaterialGraph/ExpressionRegistry.h"

#include <algorithm>

#include "ExpressionBuilder.h"

namespace Luma::MaterialGraph {

ExpressionRegistry::ExpressionRegistry() {
    using namespace Detail;
    RegisterConstants(*this);
    RegisterParameters(*this);
    RegisterMath(*this);
    RegisterTexture(*this);
    RegisterCoordinates(*this);
    RegisterUtility(*this);
    RegisterFunctions(*this);
    RegisterPostProcess(*this);
    RegisterOutput(*this);
    std::sort(m_descs.begin(), m_descs.end(), [](const ExpressionDesc& a, const ExpressionDesc& b) {
        return a.category != b.category ? a.category < b.category : a.displayName < b.displayName;
    });
}

const ExpressionRegistry& ExpressionRegistry::Get() {
    static const ExpressionRegistry registry;
    return registry;
}

void ExpressionRegistry::Add(ExpressionDesc desc) { m_descs.push_back(std::move(desc)); }

const ExpressionDesc* ExpressionRegistry::Find(std::string_view type) const {
    for (const ExpressionDesc& d : m_descs) {
        if (d.type == type) return &d;
    }
    return nullptr;
}

std::vector<const ExpressionDesc*> ExpressionRegistry::All() const {
    std::vector<const ExpressionDesc*> out;
    out.reserve(m_descs.size());
    for (const ExpressionDesc& d : m_descs) out.push_back(&d);
    return out;
}

std::string InputPropertyKey(std::string_view pinName) { return "in." + std::string(pinName); }

PropertyValue InputDefault(const ExpressionDesc& desc, const Node& node, u32 inputPin) {
    if (inputPin >= desc.inputs.size()) return PropertyValue(0.0);
    const PinDesc& pin = desc.inputs[inputPin];
    if (auto it = node.properties.find(InputPropertyKey(pin.name)); it != node.properties.end()) {
        return it->second;
    }
    return pin.defaultValue;
}

PropertyValue PropertyOrDefault(const ExpressionDesc& desc, const Node& node, std::string_view key) {
    if (auto it = node.properties.find(std::string(key)); it != node.properties.end()) return it->second;
    for (const PropertyDesc& p : desc.properties) {
        if (p.key == key) return p.defaultValue;
    }
    return PropertyValue(0.0);
}

}  // namespace Luma::MaterialGraph
