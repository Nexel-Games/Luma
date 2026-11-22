#include "Luma/Scene/AssetReferences.h"

#include <string>

#include "Luma/Scene/Components.h"
#include "Luma/Scene/Scene.h"

namespace Luma {
namespace {

// `path` == `from`, or `path` starts with `from` + '/'.
bool Retarget(std::string& path, std::string_view from, std::string_view to) {
    if (path.size() < from.size() || path.compare(0, from.size(), from) != 0) return false;
    if (path.size() != from.size() && path[from.size()] != '/') return false;
    path = std::string(to) + path.substr(from.size());
    return true;
}

}  // namespace

u32 RetargetAssetReferences(Scene& scene, std::string_view from, std::string_view to) {
    if (from.empty() || from == to) return 0;
    u32 changed = 0;
    for (Entity e : scene.Registry().view<MeshRendererComponent>()) {
        auto& mr = scene.Registry().get<MeshRendererComponent>(e);
        if (Retarget(mr.mesh, from, to)) ++changed;
        for (std::string& slot : mr.materials) {
            if (Retarget(slot, from, to)) ++changed;
        }
    }
    return changed;
}

}  // namespace Luma
