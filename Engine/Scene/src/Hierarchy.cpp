#include "Luma/Scene/Hierarchy.h"

namespace Luma {

Entity ParentOf(const Scene& scene, Entity entity) {
    const auto* link = scene.Registry().try_get<HierarchyComponent>(entity);
    if (link == nullptr || !scene.IsValid(link->parent)) return kNullEntity;
    return link->parent;
}

std::vector<Entity> ChildrenOf(const Scene& scene, Entity parent) {
    std::vector<Entity> children;
    auto view = scene.Registry().view<const NameComponent>();
    for (Entity entity : view) {
        if (ParentOf(scene, entity) == parent) children.push_back(entity);
    }
    return children;
}

std::vector<Entity> DescendantsOf(const Scene& scene, Entity entity) {
    std::vector<Entity> out;
    std::vector<Entity> pending = ChildrenOf(scene, entity);
    while (!pending.empty()) {
        Entity current = pending.back();
        pending.pop_back();
        out.push_back(current);
        auto kids = ChildrenOf(scene, current);
        pending.insert(pending.end(), kids.begin(), kids.end());
    }
    return out;
}

bool IsDescendantOf(const Scene& scene, Entity ancestor, Entity entity) {
    if (!scene.IsValid(ancestor) || !scene.IsValid(entity)) return false;
    for (Entity parent = ParentOf(scene, entity); parent != kNullEntity; parent = ParentOf(scene, parent)) {
        if (parent == ancestor) return true;
    }
    return false;
}

bool SetParent(Scene& scene, Entity child, Entity parent) {
    if (!scene.IsValid(child)) return false;
    if (parent == child) return false;
    if (parent != kNullEntity) {
        if (!scene.IsValid(parent)) return false;
        if (parent == child || IsDescendantOf(scene, child, parent)) return false;
    }
    if (parent == kNullEntity) {
        if (scene.Registry().all_of<HierarchyComponent>(child)) {
            scene.Registry().remove<HierarchyComponent>(child);
        }
        return true;
    }
    scene.Registry().emplace_or_replace<HierarchyComponent>(child, HierarchyComponent{parent});
    return true;
}

void DestroySubtree(Scene& scene, Entity root) {
    if (!scene.IsValid(root)) return;
    const std::vector<Entity> children = ChildrenOf(scene, root);
    for (Entity child : children) DestroySubtree(scene, child);
    scene.DestroyEntity(root);
}

Entity DuplicateSubtree(Scene& scene, Entity source) {
    Entity copy = scene.DuplicateEntity(source);
    if (copy == kNullEntity) return kNullEntity;
    if (scene.Registry().all_of<EditorHiddenComponent>(copy)) {
        scene.Registry().remove<EditorHiddenComponent>(copy);
    }
    const std::vector<Entity> children = ChildrenOf(scene, source);
    for (Entity child : children) {
        Entity cloned = DuplicateSubtree(scene, child);
        if (cloned != kNullEntity) SetParent(scene, cloned, copy);
    }
    return copy;
}

std::vector<Entity> SelectionRoots(const Scene& scene, const std::vector<Entity>& selected) {
    std::vector<Entity> roots;
    for (Entity entity : selected) {
        if (!scene.IsValid(entity)) continue;
        bool nested = false;
        for (Entity other : selected) {
            if (other != entity && scene.IsValid(other) && IsDescendantOf(scene, other, entity)) {
                nested = true;
                break;
            }
        }
        if (!nested) roots.push_back(entity);
    }
    return roots;
}

}  // namespace Luma
