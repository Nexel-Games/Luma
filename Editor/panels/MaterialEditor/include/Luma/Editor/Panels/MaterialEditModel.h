#pragma once

#include <string>

#include "Luma/Material/MaterialLibrary.h"

namespace Luma::Editor::Panels {

// The editing operations behind the Material Editor panel, on one Material
// or Material Instance held by the MaterialLibrary. Every edit goes through
// the library (the asset becomes dirty and its version bumps), so the
// viewport, previews and child instances update live. Saving writes the
// file.
//
// Instances: a parameter is either overridden (its own value) or inherited
// from the parent chain; turning an override on starts from the inherited
// value, turning it off falls back to the parent.
class MaterialEditModel {
public:
    // Instance-overridable settings (Unreal BasePropertyOverrides).
    enum class Setting { BlendMode, TwoSided, OpacityMaskClip };

    MaterialEditModel(std::string assetPath, Material::MaterialLibrary& library);

    const std::string& AssetPath() const { return m_path; }
    bool Valid() const;
    bool IsInstance() const;
    bool IsDirty() const;

    // What the asset renders with (resolved through the parent chain).
    Material::MaterialInput EffectiveInput(Material::MaterialParameter p) const;
    Material::MaterialSettings EffectiveSettings() const;

    // Materials: always true. Instances: the parameter's override flag.
    bool IsOverridden(Material::MaterialParameter p) const;
    // Instances: also turns the override on.
    void SetInput(Material::MaterialParameter p, const Material::MaterialInput& input);
    void SetOverride(Material::MaterialParameter p, bool overridden);

    // Settings. On an instance, setting a value overrides it; ShadingModel is
    // material-only.
    bool IsSettingOverridden(Setting s) const;
    void SetSettingOverride(Setting s, bool overridden);
    void SetBlendMode(Material::BlendMode mode);
    void SetShadingModel(Material::ShadingModel model);
    void SetTwoSided(bool twoSided);
    void SetOpacityMaskClip(f32 clip);

    // Instances only. Refuses missing parents and cycles (error says why).
    std::string Parent() const;
    bool SetParent(const std::string& parent, std::string* error);

    bool Save(std::string* error);

private:
    Material::MaterialLibrary& m_library;
    std::string m_path;
};

}  // namespace Luma::Editor::Panels
