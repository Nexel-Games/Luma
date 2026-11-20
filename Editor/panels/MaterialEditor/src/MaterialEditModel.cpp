#include "Luma/Editor/Panels/MaterialEditModel.h"

namespace Luma::Editor::Panels {

using Material::MaterialInput;
using Material::MaterialInstance;
using Material::MaterialParameter;

namespace {
usize Index(MaterialParameter p) { return static_cast<usize>(p); }
}  // namespace

MaterialEditModel::MaterialEditModel(std::string assetPath, Material::MaterialLibrary& library)
    : m_library(library), m_path(std::move(assetPath)) {}

bool MaterialEditModel::Valid() const {
    return m_library.GetMaterial(m_path) || m_library.GetInstance(m_path);
}

bool MaterialEditModel::IsInstance() const { return m_library.IsInstance(m_path); }

bool MaterialEditModel::IsDirty() const { return m_library.IsDirty(m_path); }

MaterialInput MaterialEditModel::EffectiveInput(MaterialParameter p) const {
    return m_library.Resolve(m_path).inputs[Index(p)];
}

Material::MaterialSettings MaterialEditModel::EffectiveSettings() const {
    return m_library.Resolve(m_path).settings;
}

bool MaterialEditModel::IsOverridden(MaterialParameter p) const {
    const MaterialInstance* inst = m_library.GetInstance(m_path);
    return !inst || inst->IsOverridden(p);
}

void MaterialEditModel::SetInput(MaterialParameter p, const MaterialInput& input) {
    if (const MaterialInstance* inst = m_library.GetInstance(m_path)) {
        MaterialInstance edited = *inst;
        edited.Override(p, input);
        m_library.SetInstance(m_path, std::move(edited));
    } else if (const Material::Material* mat = m_library.GetMaterial(m_path)) {
        Material::Material edited = *mat;
        edited.inputs[Index(p)] = input;
        m_library.SetMaterial(m_path, std::move(edited));
    }
}

void MaterialEditModel::SetOverride(MaterialParameter p, bool overridden) {
    const MaterialInstance* inst = m_library.GetInstance(m_path);
    if (!inst || inst->IsOverridden(p) == overridden) return;
    MaterialInstance edited = *inst;
    // Either way the stored value becomes the inherited one: re-checking starts
    // from it, unchecking makes the instance follow its parent again.
    edited.inputs[Index(p)] = m_library.Resolve(inst->parent).inputs[Index(p)];
    edited.overridden[Index(p)] = overridden;
    m_library.SetInstance(m_path, std::move(edited));
}

bool MaterialEditModel::IsSettingOverridden(Setting s) const {
    const MaterialInstance* inst = m_library.GetInstance(m_path);
    if (!inst) return true;
    switch (s) {
        case Setting::BlendMode: return inst->blendMode.has_value();
        case Setting::TwoSided: return inst->twoSided.has_value();
        case Setting::OpacityMaskClip: return inst->opacityMaskClip.has_value();
    }
    return false;
}

void MaterialEditModel::SetSettingOverride(Setting s, bool overridden) {
    const MaterialInstance* inst = m_library.GetInstance(m_path);
    if (!inst || IsSettingOverridden(s) == overridden) return;
    MaterialInstance edited = *inst;
    const Material::MaterialSettings inherited = m_library.Resolve(inst->parent).settings;
    switch (s) {
        case Setting::BlendMode:
            edited.blendMode = overridden ? std::optional(inherited.blendMode) : std::nullopt;
            break;
        case Setting::TwoSided:
            edited.twoSided = overridden ? std::optional(inherited.twoSided) : std::nullopt;
            break;
        case Setting::OpacityMaskClip:
            edited.opacityMaskClip =
                overridden ? std::optional(inherited.opacityMaskClip) : std::nullopt;
            break;
    }
    m_library.SetInstance(m_path, std::move(edited));
}

void MaterialEditModel::SetBlendMode(Material::BlendMode mode) {
    if (const MaterialInstance* inst = m_library.GetInstance(m_path)) {
        MaterialInstance edited = *inst;
        edited.blendMode = mode;
        m_library.SetInstance(m_path, std::move(edited));
    } else if (const Material::Material* mat = m_library.GetMaterial(m_path)) {
        Material::Material edited = *mat;
        edited.settings.blendMode = mode;
        m_library.SetMaterial(m_path, std::move(edited));
    }
}

void MaterialEditModel::SetShadingModel(Material::ShadingModel model) {
    if (const Material::Material* mat = m_library.GetMaterial(m_path)) {
        Material::Material edited = *mat;
        edited.settings.shadingModel = model;
        m_library.SetMaterial(m_path, std::move(edited));
    }
}

void MaterialEditModel::SetTwoSided(bool twoSided) {
    if (const MaterialInstance* inst = m_library.GetInstance(m_path)) {
        MaterialInstance edited = *inst;
        edited.twoSided = twoSided;
        m_library.SetInstance(m_path, std::move(edited));
    } else if (const Material::Material* mat = m_library.GetMaterial(m_path)) {
        Material::Material edited = *mat;
        edited.settings.twoSided = twoSided;
        m_library.SetMaterial(m_path, std::move(edited));
    }
}

void MaterialEditModel::SetOpacityMaskClip(f32 clip) {
    if (const MaterialInstance* inst = m_library.GetInstance(m_path)) {
        MaterialInstance edited = *inst;
        edited.opacityMaskClip = clip;
        m_library.SetInstance(m_path, std::move(edited));
    } else if (const Material::Material* mat = m_library.GetMaterial(m_path)) {
        Material::Material edited = *mat;
        edited.settings.opacityMaskClip = clip;
        m_library.SetMaterial(m_path, std::move(edited));
    }
}

std::string MaterialEditModel::Parent() const {
    const MaterialInstance* inst = m_library.GetInstance(m_path);
    return inst ? inst->parent : std::string{};
}

bool MaterialEditModel::SetParent(const std::string& parent, std::string* error) {
    const MaterialInstance* inst = m_library.GetInstance(m_path);
    if (!inst) {
        if (error) *error = "only Material Instances have a parent";
        return false;
    }
    if (!m_library.CanSetParent(m_path, parent)) {
        if (error) {
            *error = !m_library.Exists(parent)
                         ? "'" + parent + "' is not a material"
                         : "'" + parent + "' depends on '" + m_path + "' (would create a cycle)";
        }
        return false;
    }
    MaterialInstance edited = *inst;
    edited.parent = parent;
    m_library.SetInstance(m_path, std::move(edited));
    return true;
}

bool MaterialEditModel::Save(std::string* error) { return m_library.Save(m_path, error); }

}  // namespace Luma::Editor::Panels
