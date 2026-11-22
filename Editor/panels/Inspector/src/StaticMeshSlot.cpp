#include "Luma/Editor/Panels/StaticMeshSlot.h"

namespace Luma::Editor::Panels {

void AssignStaticMesh(MeshRendererComponent& mesh, std::string_view meshPath) {
    mesh.mesh = std::string(meshPath);
}

bool UseSelectedAssetForMesh(MeshRendererComponent& mesh, const std::string& selectedPath,
                             const std::function<bool(const std::string&)>& isMesh) {
    if (selectedPath.empty() || !isMesh || !isMesh(selectedPath)) return false;
    AssignStaticMesh(mesh, selectedPath);
    return true;
}

std::string AssetNameOf(std::string_view virtualPath) {
    const usize slash = virtualPath.find_last_of('/');
    return std::string(slash == std::string_view::npos ? virtualPath : virtualPath.substr(slash + 1));
}

}  // namespace Luma::Editor::Panels
