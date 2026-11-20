#include "Luma/Editor/Panels/MaterialSlots.h"

#include "Luma/Core/Log.h"

namespace Luma::Editor::Panels {

void AssignMaterialSlot(MeshRendererComponent& mesh, u32 slot, const std::string& materialPath) {
    if (mesh.materials.size() <= slot) mesh.materials.resize(static_cast<usize>(slot) + 1);
    mesh.materials[slot] = materialPath;
}

bool UseSelectedAssetForSlot(MeshRendererComponent& mesh, u32 slot, const std::string& selectedPath,
                             Material::MaterialLibrary& library) {
    if (selectedPath.empty() || !library.Exists(selectedPath)) return false;
    AssignMaterialSlot(mesh, slot, selectedPath);
    return true;
}

std::string CreateInstanceForSlot(MeshRendererComponent& mesh, u32 slot,
                                  Material::MaterialLibrary& library, std::string* error) {
    std::string parent = slot < mesh.materials.size() ? mesh.materials[slot] : std::string{};
    if (parent.empty()) parent = std::string(Material::kDefaultMaterialPath);
    if (!library.Exists(parent)) {
        if (error) *error = "'" + parent + "' is not a material";
        return {};
    }
    // Next to a project parent; engine parents get theirs under /Game.
    const usize slash = parent.find_last_of('/');
    const std::string folder = parent.rfind("/Game/", 0) == 0 ? parent.substr(0, slash)
                                                                : std::string("/Game/Materials");
    std::string base = parent.substr(slash + 1);
    if (base.rfind("M_", 0) == 0) base = base.substr(2);
    if (base.rfind("MI_", 0) == 0) base = base.substr(3);

    Material::MaterialInstance instance;
    instance.parent = parent;
    const std::string path = library.CreateInMemoryInstance(folder + "/MI_" + base, instance);
    instance.name = path.substr(path.find_last_of('/') + 1);
    library.SetInstance(path, instance);
    if (!library.Save(path, error)) {
        library.Revert(path);
        return {};
    }
    AssignMaterialSlot(mesh, slot, path);
    LUMA_USER_INFO("Materials", "Created '{}' (instance of '{}')", path, parent);
    return path;
}

}  // namespace Luma::Editor::Panels
