#include "Luma/Scene/ActorClipboard.h"

#include <algorithm>
#include <unordered_set>

#include "Luma/Scene/Hierarchy.h"
#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/SceneSerializer.h"

namespace Luma {
namespace {

bool Capture(const Scene& scene, Entity entity, ActorClipNode& out) {
    if (!scene.IsValid(entity)) return false;
    if (OnePerSceneComponentOf(scene, entity)) return false;
    out = {};
    out.name = scene.Registry().get<NameComponent>(entity).name;
    out.owners = SerialValue::MakeObject();
    out.components = SceneSerializer::WriteComponents(scene, entity, out.owners);
    for (Entity child : ChildrenOf(scene, entity)) {
        ActorClipNode node;
        if (Capture(scene, child, node)) out.children.push_back(std::move(node));
    }
    return true;
}

std::string UniqueIgnoring(const Scene& scene, const std::string& base,
                           const std::unordered_set<Entity>& ignore) {
    auto view = scene.Registry().view<const NameComponent>();
    auto taken = [&](const std::string& name) {
        for (Entity entity : view) {
            if (ignore.count(entity) != 0) continue;
            if (view.get<const NameComponent>(entity).name == name) return true;
        }
        return false;
    };
    if (!taken(base)) return base;
    int n = 1;
    while (taken(base + " " + std::to_string(n))) ++n;
    return base + " " + std::to_string(n);
}

Entity PasteNode(Scene& scene, const ActorClipNode& node, Entity parent,
                 const std::unordered_set<Entity>& ignore) {
    const std::string name = UniqueIgnoring(scene, node.name, ignore);
    Entity entity = scene.CreateEntity(name);
    SceneSerializer::ReadComponents(scene, entity, node.components, &node.owners);
    scene.Registry().get<NameComponent>(entity).name = name;
    SetParent(scene, entity, parent);
    for (const ActorClipNode& child : node.children) PasteNode(scene, child, entity, ignore);
    return entity;
}

}  // namespace

bool ActorClipboard::Copy(const Scene& scene, const std::vector<Entity>& roots) {
    m_roots.clear();
    m_cutSources.clear();
    m_cut = false;
    for (Entity root : SelectionRoots(scene, roots)) {
        ActorClipNode node;
        if (Capture(scene, root, node)) m_roots.push_back(std::move(node));
    }
    return !m_roots.empty();
}

bool ActorClipboard::Cut(const Scene& scene, const std::vector<Entity>& roots) {
    // Moving a partial subtree would destroy any omitted singleton on paste.
    // Only cut roots whose complete subtree can be copied.
    auto movable = SelectionRoots(scene, roots);
    std::erase_if(movable, [&](Entity root) {
        if (OnePerSceneComponentOf(scene, root)) return true;
        for (Entity child : DescendantsOf(scene, root))
            if (OnePerSceneComponentOf(scene, child)) return true;
        return false;
    });
    if (!Copy(scene, movable)) return false;
    m_cut = true;
    m_cutSources = std::move(movable);
    return true;
}

std::vector<Entity> ActorClipboard::Paste(Scene& scene, Entity parent,
                                          const std::vector<Entity>& ignore) const {
    std::unordered_set<Entity> skip(ignore.begin(), ignore.end());
    std::vector<Entity> created;
    if (parent != kNullEntity && !scene.IsValid(parent)) return created;
    for (const ActorClipNode& root : m_roots) {
        created.push_back(PasteNode(scene, root, parent, skip));
    }
    return created;
}

void ActorClipboard::Clear() {
    m_roots.clear();
    m_cutSources.clear();
    m_cut = false;
}

}  // namespace Luma
