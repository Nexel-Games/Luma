#include "Luma/Scene/EntityTemplates.h"

#include <algorithm>

#include "Luma/Core/Log.h"

namespace Luma {

EntityTemplateRegistry::EntityTemplateRegistry() : m_alive(std::make_shared<int>(0)) {}

RegistrationToken EntityTemplateRegistry::Register(EntityTemplate t, std::string* error) {
    auto fail = [error](std::string message) {
        LUMA_LOG_ERROR("Scene", "{}", message);
        if (error) *error = std::move(message);
        return RegistrationToken{};
    };
    if (t.id.empty()) return fail("entity template rejected: the id is empty");
    const std::string who = "entity template '" + t.id + "'";
    if (t.owner.empty()) return fail(who + " rejected: owner is empty");
    if (t.displayName.empty()) return fail(who + " rejected: displayName is empty");
    if (!t.create) return fail(who + " rejected: missing create callback");
    if (t.autoCreate && t.singletonComponent.empty()) {
        return fail(who + " rejected: autoCreate needs a singletonComponent (otherwise it "
                          "would be created on every scene load)");
    }
    if (const EntityTemplate* existing = Find(t.id)) {
        return fail(who + " is already registered (by '" + existing->owner + "')");
    }
    const u64 handle = m_nextHandle++;
    m_entries.push_back({handle, std::make_unique<EntityTemplate>(std::move(t))});
    ++m_revision;
    return RegistrationToken(m_alive, [this, handle] {
        auto it = std::find_if(m_entries.begin(), m_entries.end(),
                               [handle](const Entry& e) { return e.handle == handle; });
        if (it == m_entries.end()) return;
        m_entries.erase(it);
        ++m_revision;
    });
}

const EntityTemplate* EntityTemplateRegistry::Find(std::string_view id) const {
    for (const Entry& e : m_entries) {
        if (e.value->id == id) return e.value.get();
    }
    return nullptr;
}

std::vector<const EntityTemplate*> EntityTemplateRegistry::All() const {
    std::vector<const EntityTemplate*> out;
    out.reserve(m_entries.size());
    for (const Entry& e : m_entries) out.push_back(e.value.get());
    return out;
}

EntityTemplateRegistry& EntityTemplates() {
    static EntityTemplateRegistry registry;
    static RegistrationSet builtins = RegisterBuiltinEntityTemplates(registry);
    return registry;
}

namespace {

// The entity holding `componentId` (first found), or kNullEntity.
Entity FindHolder(const Scene& scene, std::string_view componentId) {
    const ComponentDesc* desc = Components().Find(componentId);
    if (!desc) return kNullEntity;
    for (Entity e : scene.Registry().view<entt::entity>()) {
        if (desc->has(scene, e)) return e;
    }
    return kNullEntity;
}

}  // namespace

Entity Instantiate(Scene& scene, const EntityTemplate& t, const EntityTemplateContext& context) {
    if (!t.singletonComponent.empty()) {
        if (Entity existing = FindHolder(scene, t.singletonComponent); existing != kNullEntity) {
            return existing;
        }
    }
    return t.create(scene, context);
}

usize EnsureAutoCreatedEntities(Scene& scene, const EntityTemplateRegistry& registry) {
    usize created = 0;
    for (const EntityTemplate* t : registry.All()) {
        if (!t->autoCreate) continue;
        if (FindHolder(scene, t->singletonComponent) != kNullEntity) continue;
        if (t->create(scene, {1}) != kNullEntity) ++created;
    }
    return created;
}

std::string UniqueEntityName(const Scene& scene, std::string_view base) {
    auto view = scene.Registry().view<const NameComponent>();
    auto taken = [&view](const std::string& name) {
        for (Entity e : view) {
            if (view.get<const NameComponent>(e).name == name) return true;
        }
        return false;
    };
    std::string name(base);
    if (!taken(name)) return name;
    int n = 1;
    while (taken(name + " " + std::to_string(n))) ++n;
    return name + " " + std::to_string(n);
}

// --- Built-in templates ----------------------------------------------------------

namespace {

EntityTemplate EngineTemplate(std::string id, std::string displayName, std::string category,
                              std::string categoryIcon, std::string icon, std::string tooltip) {
    EntityTemplate t;
    t.id = std::move(id);
    t.displayName = std::move(displayName);
    t.category = std::move(category);
    t.categoryIcon = std::move(categoryIcon);
    t.icon = std::move(icon);
    t.tooltip = std::move(tooltip);
    t.owner = "Engine";
    return t;
}

EntityTemplate PrimitiveTemplate(std::string id, MeshPrimitive primitive, std::string icon,
                                 std::string tooltip) {
    EntityTemplate t = EngineTemplate(id, id, "Geometry", "CategoryGeometry", std::move(icon),
                                      std::move(tooltip));
    const std::string base = t.displayName;
    t.create = [base, primitive](Scene& scene, const EntityTemplateContext& ctx) {
        Entity e = scene.CreateEntity(UniqueEntityName(scene, base));
        // Empty material slots: the Default Material.
        scene.Registry().emplace<MeshRendererComponent>(e).primitive = primitive;
        scene.Registry().get<TransformComponent>(e).position =
            Math::Vec3(static_cast<f32>(ctx.index - 1) * 2.0f, 1.0f, 0.0f);
        return e;
    };
    return t;
}

EntityTemplate LightTemplate(std::string id, std::string displayName, LightType type,
                             std::string icon, std::string tooltip) {
    EntityTemplate t = EngineTemplate(std::move(id), displayName, "Lights", "CategoryLight",
                                      std::move(icon), std::move(tooltip));
    t.create = [displayName, type](Scene& scene, const EntityTemplateContext& ctx) {
        Entity e = scene.CreateEntity(UniqueEntityName(scene, displayName));
        scene.Registry().emplace<LightComponent>(e).type = type;
        auto& tf = scene.Registry().get<TransformComponent>(e);
        // Lights sit above the origin. Directional defaults to a three-quarter
        // sun angle (pitch down, yawed) so the ground and meshes both catch light
        // instead of a horizon-grazing identity rotation.
        tf.position = Math::Vec3(static_cast<f32>(ctx.index - 1) * 3.0f, 4.0f, 2.0f);
        if (type == LightType::Directional) tf.rotationEuler = Math::Vec3(-50.0f, 35.0f, 0.0f);
        return e;
    };
    return t;
}

}  // namespace

RegistrationSet RegisterBuiltinEntityTemplates(EntityTemplateRegistry& registry) {
    RegistrationSet set;

    EntityTemplate empty = EngineTemplate("Empty", "Empty Actor", "Basic", "Cube", "Dot",
                                          "An entity with only a name and a transform.");
    empty.create = [](Scene& scene, const EntityTemplateContext&) {
        return scene.CreateEntity(UniqueEntityName(scene, "Empty Actor"));
    };
    set.Add(registry.Register(std::move(empty)));

    set.Add(registry.Register(PrimitiveTemplate("Cube", MeshPrimitive::Cube, "ActorCube",
                                                "A unit cube mesh.")));
    set.Add(registry.Register(PrimitiveTemplate("Plane", MeshPrimitive::Plane, "ActorPlane",
                                                "A flat 1x1 plane mesh.")));
    set.Add(registry.Register(PrimitiveTemplate("Sphere", MeshPrimitive::Sphere, "ActorSphere",
                                                "A UV sphere mesh.")));
    set.Add(registry.Register(PrimitiveTemplate("Cylinder", MeshPrimitive::Cylinder,
                                                "ActorCylinder", "A capped cylinder mesh.")));

    set.Add(registry.Register(LightTemplate("DirectionalLight", "Directional Light",
                                            LightType::Directional, "ActorDirLight",
                                            "The sun: drives the sky and casts cascaded "
                                            "shadows.")));
    set.Add(registry.Register(LightTemplate("PointLight", "Point Light", LightType::Point,
                                            "ActorPointLight",
                                            "Omnidirectional light with physical falloff.")));
    set.Add(registry.Register(LightTemplate("SpotLight", "Spot Light", LightType::Spot,
                                            "ActorSpotLight", "A cone of light.")));
    set.Add(registry.Register(LightTemplate("TubeLight", "Tube Light", LightType::Tube,
                                            "ActorPointLight", "A linear light emitter.")));

    EntityTemplate env = EngineTemplate(
        "Environment", "Sky & Environment", "Environment", "CategoryEnvironment",
        "CategoryEnvironment",
        "Sky atmosphere and ambient lighting (one per scene; selects the existing one if "
        "present).");
    env.singletonComponent = "Environment";
    env.autoCreate = true;  // every scene has a sky
    env.create = [](Scene& scene, const EntityTemplateContext&) {
        Entity e = scene.CreateEntity(UniqueEntityName(scene, "Environment"));
        scene.Registry().emplace<EnvironmentComponent>(e);
        return e;
    };
    set.Add(registry.Register(std::move(env)));

    EntityTemplate pp = EngineTemplate("PostProcessVolume", "Post Process Volume", "Volumes",
                                       "Eye", "Eye",
                                       "Bloom, exposure, tone mapping and other camera effects.");
    pp.create = [](Scene& scene, const EntityTemplateContext&) {
        Entity e = scene.CreateEntity(UniqueEntityName(scene, "Post Process Volume"));
        scene.Registry().emplace<PostProcessComponent>(e);
        return e;
    };
    set.Add(registry.Register(std::move(pp)));

    return set;
}

}  // namespace Luma
