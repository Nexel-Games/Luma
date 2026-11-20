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

}  // namespace Luma
