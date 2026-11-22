#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Luma/Material/GraphAssets.h"
#include "Luma/MaterialCompiler/Parameters.h"

namespace Luma::Material {
class MaterialLibrary;
}

// The headless model behind the Material Instance Editor (Unreal's
// UMaterialEditorInstanceConstant): the parameters the root material
// declares, grouped like UE's Details panel, with each one's override flag,
// effective value and the value it falls back to. Every edit goes through
// the MaterialLibrary (the instance becomes dirty; the bridge turns it into
// a parameter-only update).

namespace Luma::MaterialEditors {

struct InstanceParameter {
    MaterialCompiler::ParameterInfo info;
    bool overridden = false;                 // this instance overrides it
    Material::ParameterOverride value;       // what this instance renders with
    Material::ParameterOverride parentValue; // what Reset / unchecking falls back to
};

struct ParameterGroup {
    std::string name;  // "None" for ungrouped parameters
    std::vector<InstanceParameter> parameters;
};

class InstanceParameterModel {
public:
    InstanceParameterModel(std::string instancePath, Material::MaterialLibrary& library);

    const std::string& Path() const { return m_path; }
    bool Valid() const;
    bool IsDirty() const;
    bool Save(std::string* error);

    std::string Parent() const;
    // Refuses unknown assets and parent cycles (error says why).
    bool SetParent(const std::string& parent, std::string* error);
    // The graph material at the end of the parent chain ("" if broken).
    std::string RootMaterial() const;

    // Groups: "None" first, then alphabetical; parameters by sort priority,
    // then name.
    std::vector<ParameterGroup> Groups() const;

    // On: copies the inherited value; off: removes the override.
    void SetOverride(std::string_view name, bool overridden);
    void SetValue(const Material::ParameterOverride& value);  // also overrides
    void Reset(std::string_view name) { SetOverride(name, false); }

private:
    const Material::GraphMaterialInstance* Instance() const;
    void Store(Material::GraphMaterialInstance instance);
    // Value of `info` through the chain starting at `from` (an instance or
    // the root), else the declared default.
    Material::ParameterOverride Lookup(const MaterialCompiler::ParameterInfo& info,
                                       const std::string& from) const;
    const std::vector<MaterialCompiler::ParameterInfo>& Parameters() const;

    std::string m_path;
    Material::MaterialLibrary& m_library;
    // Root parameters cached per (root path, root version).
    mutable std::string m_cacheRoot;
    mutable u64 m_cacheVersion = ~0ull;
    mutable std::vector<MaterialCompiler::ParameterInfo> m_cache;
};

// ParameterInfo default as an override value.
Material::ParameterOverride DefaultValue(const MaterialCompiler::ParameterInfo& info);

}  // namespace Luma::MaterialEditors
