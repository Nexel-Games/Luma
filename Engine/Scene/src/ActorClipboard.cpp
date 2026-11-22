#include "Luma/Scene/ActorClipboard.h"

#include <unordered_set>

#include "Luma/Scene/Hierarchy.h"

namespace Luma {
namespace {

bool Capture(const Scene& scene, Entity entity, ActorClipNode& out) {
    if (!scene.IsValid(entity)) return false;
    if (scene.Registry().all_of<EnvironmentComponent>(entity)) return false;
    out = {};
    out.name = scene.Registry().get<NameComponent>(entity).name;
    out.transform = scene.Registry().get<TransformComponent>(entity);
    if (const auto* mesh = scene.Registry().try_get<MeshRendererComponent>(entity)) {
        out.hasMesh = true;
        out.mesh = *mesh;
    }
    if (const auto* light = scene.Registry().try_get<LightComponent>(entity)) {
        out.hasLight = true;
        out.light = *light;
    }
    if (const auto* camera = scene.Registry().try_get<CameraComponent>(entity)) {
        out.hasCamera = true;
        out.camera = *camera;
    }
    if (const auto* post = scene.Registry().try_get<PostProcessComponent>(entity)) {
        out.hasPost = true;
        out.post = *post;
    }
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
    Entity entity = scene.CreateEntity(UniqueIgnoring(scene, node.name, ignore));
    scene.Registry().get<TransformComponent>(entity) = node.transform;
    if (node.hasMesh) scene.Registry().emplace<MeshRendererComponent>(entity, node.mesh);
    if (node.hasLight) scene.Registry().emplace<LightComponent>(entity, node.light);
    if (node.hasCamera) scene.Registry().emplace<CameraComponent>(entity, node.camera);
    if (node.hasPost) scene.Registry().emplace<PostProcessComponent>(entity, node.post);
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
    if (!Copy(scene, roots)) return false;
    m_cut = true;
    m_cutSources = SelectionRoots(scene, roots);
    std::vector<Entity> kept;
    for (Entity entity : m_cutSources) {
        if (scene.IsValid(entity) && !scene.Registry().all_of<EnvironmentComponent>(entity)) {
            kept.push_back(entity);
        }
    }
    m_cutSources = std::move(kept);
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
