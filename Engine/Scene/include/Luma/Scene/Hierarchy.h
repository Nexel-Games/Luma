#pragma once

#include <vector>

#include "Luma/Scene/Scene.h"

namespace Luma {

// Parent link for the World Outliner. Absent, or `parent == kNullEntity`, means
// a root. Transforms stay in world space: reparenting does not move the actor.
struct HierarchyComponent {
    Entity parent = kNullEntity;
};

// Organizational parent of `entity`. kNullEntity when it is a root, the parent
// is missing, or the stored parent is no longer alive.
Entity ParentOf(const Scene& scene, Entity entity);

// Direct children, in registry order. Does not include `parent` itself.
std::vector<Entity> ChildrenOf(const Scene& scene, Entity parent);

// Every descendant, parents before their children. Does not include `entity`.
std::vector<Entity> DescendantsOf(const Scene& scene, Entity entity);

bool IsDescendantOf(const Scene& scene, Entity ancestor, Entity entity);

// Rejects a missing child, a missing parent, self-parenting, and cycles.
// `parent == kNullEntity` makes `child` a root. Transforms are left unchanged.
bool SetParent(Scene& scene, Entity child, Entity parent);

void DestroySubtree(Scene& scene, Entity root);

// Deep copy: new ids, same parent as `source`, children reparented under the
// copy. Names are copied as-is (the caller renames). Editor hide flags are
// not copied. kNullEntity when `source` is invalid.
Entity DuplicateSubtree(Scene& scene, Entity source);

// Selected actors with any selected ancestor removed, so a child is not
// processed again when its parent is also selected.
std::vector<Entity> SelectionRoots(const Scene& scene, const std::vector<Entity>& selected);

}  // namespace Luma
