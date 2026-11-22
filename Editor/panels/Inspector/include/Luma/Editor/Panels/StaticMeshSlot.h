#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "Luma/Scene/Components.h"

// Mesh Renderer "Static Mesh" operations (Inspector row, drag and drop,
// viewport placement). Unreal keeps a component's material overrides when its
// mesh changes; so do these.

namespace Luma::Editor::Panels {

// Assigns `meshPath` (a virtual path), or clears it (empty) so the component
// draws its primitive again. Material overrides are kept.
void AssignStaticMesh(MeshRendererComponent& mesh, std::string_view meshPath);

// "Use Selected Asset": assigns `selectedPath` (the Content Browser
// selection) when `isMesh` says it is a static mesh; false and unchanged
// otherwise.
bool UseSelectedAssetForMesh(MeshRendererComponent& mesh, const std::string& selectedPath,
                             const std::function<bool(const std::string&)>& isMesh);

// "SM_Rock" for "/Game/Props/SM_Rock".
std::string AssetNameOf(std::string_view virtualPath);

}  // namespace Luma::Editor::Panels
