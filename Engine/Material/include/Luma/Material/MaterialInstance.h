#pragma once

#include <array>
#include <optional>
#include <string>

#include "Luma/Material/Material.h"

// The Material Instance asset (.lmatinst) — Unreal UMaterialInstanceConstant:
// a parent (a Material or another Instance) plus per-parameter overrides.
// Parameters without their override flag inherit the parent's value; the
// optional settings mirror Unreal's BasePropertyOverrides.

namespace Luma::Material {

struct MaterialInstance {
    std::string name;
    std::string parent;  // virtual asset path of the parent material/instance
    std::array<bool, kMaterialParameterCount> overridden{};
    MaterialInputs inputs = Material::DefaultInputs();
    std::optional<bool> twoSided;
    std::optional<f32> opacityMaskClip;
    std::optional<BlendMode> blendMode;

    bool IsOverridden(MaterialParameter p) const {
        return overridden[static_cast<usize>(p)];
    }
    // Sets an override value (and marks the parameter overridden).
    void Override(MaterialParameter p, const MaterialInput& value) {
        overridden[static_cast<usize>(p)] = true;
        inputs[static_cast<usize>(p)] = value;
    }
};

}  // namespace Luma::Material
