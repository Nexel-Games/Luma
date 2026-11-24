#pragma once

#include <string>

#include "Luma/Core/Types.h"
#include "Luma/Material/MaterialLibrary.h"

namespace Luma::Editor {
class InspectorRows;
}

// The parameters of a Mesh Renderer slot's material, edited right in the
// Inspector (no Material Editor round trip):
//   - a Material Instance: edits its overrides (every object using that
//     instance follows, as in the Instance Editor);
//   - a project Material: edits its parameter defaults, which every object
//     using it shares (the rows say so; the slot's Create Instance button
//     gives this object its own copy);
//   - engine content (/Engine/...): shown read-only.
// Parameter edits never recompile shaders, except a Static Switch, which
// compiles that permutation once.

namespace Luma::Editor::Panels {

enum class SlotParameterMode : u8 { None, Instance, SharedMaterial, ReadOnly };

// How a slot holding `materialPath` edits its parameters.
SlotParameterMode SlotParameterModeFor(const std::string& materialPath, Material::MaterialLibrary& library);

// Draws the collapsible "Parameters" rows for slot `slot` showing
// `materialPath` (the slot's material, or the mesh's / Default Material when
// the slot is empty).
void DrawSlotParameters(InspectorRows& rows, const std::string& materialPath, u32 slot);

// Just the parameter rows of `materialPath` (grouped, editable per its
// SlotParameterMode); `keyPrefix` keeps widget ids unique per call site.
void DrawMaterialParameters(InspectorRows& rows, const std::string& materialPath, const std::string& keyPrefix);

}  // namespace Luma::Editor::Panels
