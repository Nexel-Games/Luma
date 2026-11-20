#pragma once

#include <string>

#include "Luma/Material/MaterialLibrary.h"
#include "Luma/Scene/Components.h"

// Mesh Renderer material-slot operations (Inspector rows, drag and drop,
// viewport drops).

namespace Luma::Editor::Panels {

// Writes `materialPath` into slot `slot` (growing the slot list with empty =
// Default Material entries as needed).
void AssignMaterialSlot(MeshRendererComponent& mesh, u32 slot, const std::string& materialPath);

// Creates and saves a Material Instance of the slot's material (the Default
// Material for an empty slot) named MI_<parent name> next to its parent
// (/Game/Materials for engine parents), assigns it to the slot and returns its
// path. Empty string + `error` on failure.
std::string CreateInstanceForSlot(MeshRendererComponent& mesh, u32 slot,
                                  Material::MaterialLibrary& library, std::string* error);

}  // namespace Luma::Editor::Panels
