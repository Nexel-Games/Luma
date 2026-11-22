#include "Luma/Scene/Scene.h"

namespace Luma {

Entity Scene::CreateEntity(const std::string& name) {
    Entity e = m_registry.create();
    m_registry.emplace<NameComponent>(e, name);
    m_registry.emplace<TransformComponent>(e);
    return e;
}

void Scene::DestroyEntity(Entity entity) {
    if (m_registry.valid(entity)) m_registry.destroy(entity);
}

Entity Scene::DuplicateEntity(Entity source) {
    if (!m_registry.valid(source)) return kNullEntity;
    Entity copy = m_registry.create();
    for (auto [id, pool] : m_registry.storage()) {
        if (pool.contains(source) && !pool.contains(copy)) {
            pool.push(copy, pool.value(source));
        }
    }
    return copy;
}

std::string Scene::UniqueEntityName(const std::string& base) const {
    auto view = m_registry.view<const NameComponent>();
    auto taken = [&view](const std::string& name) {
        for (Entity e : view) {
            if (view.get<const NameComponent>(e).name == name) return true;
        }
        return false;
    };
    if (!taken(base)) return base;
    int n = 1;
    while (taken(base + " " + std::to_string(n))) ++n;
    return base + " " + std::to_string(n);
}

}  // namespace Luma
