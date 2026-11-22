#include "Luma/MaterialEditors/InstanceParameters.h"

#include <algorithm>
#include <map>

#include "Luma/Material/MaterialLibrary.h"
#include "Luma/MaterialCompiler/Translator.h"

namespace Luma::MaterialEditors {

using Material::GraphMaterialInstance;
using Material::ParameterOverride;
using MaterialCompiler::ParameterInfo;

namespace {
constexpr u32 kMaxChain = 16;
}

Material::ParameterOverride DefaultValue(const ParameterInfo& info) {
    ParameterOverride o;
    o.name = info.name;
    o.kind = static_cast<ParameterOverride::Kind>(info.kind);
    o.scalar = info.defaultValue.x;
    o.vector = info.defaultValue;
    o.texture = info.defaultTexture;
    o.staticSwitch = info.defaultValue.x != 0.0f;
    return o;
}

InstanceParameterModel::InstanceParameterModel(std::string instancePath, Material::MaterialLibrary& library)
    : m_path(std::move(instancePath)), m_library(library) {}

const GraphMaterialInstance* InstanceParameterModel::Instance() const {
    return m_library.GetGraphInstance(m_path);
}

bool InstanceParameterModel::Valid() const { return Instance() != nullptr; }
bool InstanceParameterModel::IsDirty() const { return m_library.IsGraphAssetDirty(m_path); }
bool InstanceParameterModel::Save(std::string* error) { return m_library.SaveGraphAsset(m_path, error); }

std::string InstanceParameterModel::Parent() const {
    const GraphMaterialInstance* inst = Instance();
    return inst ? inst->parent : std::string();
}

void InstanceParameterModel::Store(GraphMaterialInstance instance) {
    m_library.SetGraphInstance(m_path, std::move(instance));
}

bool InstanceParameterModel::SetParent(const std::string& parent, std::string* error) {
    const GraphMaterialInstance* inst = Instance();
    if (!inst) {
        if (error) *error = "the instance could not be loaded";
        return false;
    }
    const bool isDefault = parent == Material::kDefaultMaterialPath;
    if (!isDefault && !m_library.GetGraphInstance(parent) && !m_library.GetGraphMaterial(parent)) {
        if (error) *error = "'" + parent + "' is not a material or material instance";
        return false;
    }
    std::string current = parent;
    for (u32 i = 0; i < kMaxChain && !current.empty(); ++i) {
        if (current == m_path) {
            if (error) *error = "'" + parent + "' derives from this instance (cycle)";
            return false;
        }
        const GraphMaterialInstance* p = m_library.GetGraphInstance(current);
        if (!p) break;
        current = p->parent;
    }
    GraphMaterialInstance copy = *inst;
    copy.parent = parent;
    Store(std::move(copy));
    return true;
}

std::string InstanceParameterModel::RootMaterial() const {
    std::string current = m_path;
    for (u32 i = 0; i < kMaxChain; ++i) {
        if (const GraphMaterialInstance* inst = m_library.GetGraphInstance(current)) {
            current = inst->parent;
            continue;
        }
        if (current == Material::kDefaultMaterialPath || m_library.GetGraphMaterial(current)) return current;
        return {};
    }
    return {};
}

const std::vector<ParameterInfo>& InstanceParameterModel::Parameters() const {
    const std::string root = RootMaterial();
    const u64 version = root.empty() ? 0 : m_library.GraphVersion(root);
    if (root == m_cacheRoot && version == m_cacheVersion) return m_cache;
    m_cacheRoot = root;
    m_cacheVersion = version;
    m_cache.clear();
    if (root.empty()) return m_cache;
    Material::GraphMaterial material;
    if (root == Material::kDefaultMaterialPath) {
        material = Material::GraphMaterial::MakeDefault();
    } else if (const Material::GraphMaterial* m = m_library.GetGraphMaterial(root)) {
        material = *m;
    }
    MaterialCompiler::Translator translator(material, &m_library, {});
    m_cache = translator.Finish().parameters;
    return m_cache;
}

ParameterOverride InstanceParameterModel::Lookup(const ParameterInfo& info, const std::string& from) const {
    std::string current = from;
    for (u32 i = 0; i < kMaxChain; ++i) {
        const GraphMaterialInstance* inst = m_library.GetGraphInstance(current);
        if (!inst) break;
        if (const ParameterOverride* o = inst->Find(info.name)) return *o;
        current = inst->parent;
    }
    return DefaultValue(info);
}

std::vector<ParameterGroup> InstanceParameterModel::Groups() const {
    std::vector<ParameterGroup> groups;
    const GraphMaterialInstance* inst = Instance();
    if (!inst) return groups;
    std::map<std::string, std::vector<InstanceParameter>> byGroup;
    for (const ParameterInfo& info : Parameters()) {
        InstanceParameter p;
        p.info = info;
        p.parentValue = Lookup(info, inst->parent);
        const ParameterOverride* own = inst->Find(info.name);
        p.overridden = own != nullptr;
        p.value = own ? *own : p.parentValue;
        byGroup[info.group.empty() ? "None" : info.group].push_back(std::move(p));
    }
    auto add = [&](const std::string& name) {
        auto it = byGroup.find(name);
        if (it == byGroup.end()) return;
        std::sort(it->second.begin(), it->second.end(), [](const InstanceParameter& a, const InstanceParameter& b) {
            if (a.info.sortPriority != b.info.sortPriority) return a.info.sortPriority < b.info.sortPriority;
            return a.info.name < b.info.name;
        });
        groups.push_back({name, std::move(it->second)});
        byGroup.erase(it);
    };
    add("None");
    while (!byGroup.empty()) add(byGroup.begin()->first);
    return groups;
}

void InstanceParameterModel::SetOverride(std::string_view name, bool overridden) {
    const GraphMaterialInstance* inst = Instance();
    if (!inst) return;
    const bool has = inst->Find(name) != nullptr;
    if (has == overridden) return;
    GraphMaterialInstance copy = *inst;
    if (!overridden) {
        copy.Remove(name);
    } else {
        for (const ParameterInfo& info : Parameters()) {
            if (info.name != name) continue;
            copy.Set(Lookup(info, copy.parent));
            break;
        }
    }
    Store(std::move(copy));
}

void InstanceParameterModel::SetValue(const ParameterOverride& value) {
    const GraphMaterialInstance* inst = Instance();
    if (!inst) return;
    GraphMaterialInstance copy = *inst;
    copy.Set(value);
    Store(std::move(copy));
}

}  // namespace Luma::MaterialEditors
