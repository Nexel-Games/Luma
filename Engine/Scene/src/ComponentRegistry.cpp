#include "Luma/Scene/ComponentRegistry.h"

#include <algorithm>

#include "Luma/Core/Log.h"
#include "Luma/Scene/ComponentReflection.h"

namespace Luma {

namespace {

// Component ids are serialized as JSON object keys and shown in diagnostics;
// keep them to identifier characters so they are unambiguous everywhere.
bool IsValidComponentId(std::string_view id) {
    if (id.empty()) return false;
    for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '.';
        if (!ok) return false;
    }
    return true;
}

bool Fail(std::string* error, std::string message) {
    LUMA_LOG_ERROR("Components", "{}", message);
    if (error) *error = std::move(message);
    return false;
}

bool Validate(const ComponentDesc& d, std::string* error) {
    if (d.id.empty()) return Fail(error, "component registration rejected: the id is empty");
    if (!IsValidComponentId(d.id)) {
        return Fail(error, "component registration rejected: id '" + d.id +
                               "' may only contain letters, digits, '_' and '.'");
    }
    const std::string who = "component '" + d.id + "'";
    if (d.owner.empty()) {
        return Fail(error, who + " registration rejected: owner is empty (use \"Engine\" or "
                                 "the plugin id)");
    }
    if (!d.add) return Fail(error, who + " registration rejected: missing add callback");
    if (!d.remove) return Fail(error, who + " registration rejected: missing remove callback");
    if (!d.has) return Fail(error, who + " registration rejected: missing has callback");
    if (!d.write) return Fail(error, who + " registration rejected: missing write callback");
    if (!d.read) return Fail(error, who + " registration rejected: missing read callback");
    if (!d.reset) return Fail(error, who + " registration rejected: missing reset callback");
    for (const ComponentVariant& v : d.variants) {
        if (v.displayName.empty() || !v.apply) {
            return Fail(error, who + " registration rejected: every variant needs a display "
                                     "name and an apply callback");
        }
    }
    return true;
}

}  // namespace

ComponentRegistry::ComponentRegistry() : m_alive(std::make_shared<int>(0)) {}

ComponentRegistry::~ComponentRegistry() = default;

RegistrationToken ComponentRegistry::Register(ComponentDesc desc, std::string* error) {
    if (!Validate(desc, error)) return {};
    if (const ComponentDesc* existing = Find(desc.id)) {
        Fail(error, "component '" + desc.id + "' is already registered (by '" +
                        existing->owner + "'); rejected the registration from '" +
                        desc.owner + "'");
        return {};
    }
    if (desc.displayName.empty()) desc.displayName = desc.id;

    const u64 handle = m_nextHandle++;
    Entry entry;
    entry.handle = handle;
    entry.desc = std::make_unique<ComponentDesc>(std::move(desc));
    m_entries.push_back(std::move(entry));
    ++m_revision;
    return RegistrationToken(m_alive, [this, handle] { Unregister(handle); });
}

void ComponentRegistry::Unregister(u64 handle) {
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
                           [handle](const Entry& e) { return e.handle == handle; });
    if (it == m_entries.end()) return;
    m_entries.erase(it);
    ++m_revision;
}

const ComponentDesc* ComponentRegistry::Find(std::string_view id) const {
    for (const Entry& e : m_entries) {
        if (e.desc->id == id) return e.desc.get();
    }
    return nullptr;
}

std::vector<const ComponentDesc*> ComponentRegistry::All() const {
    std::vector<const ComponentDesc*> out;
    out.reserve(m_entries.size());
    for (const Entry& e : m_entries) out.push_back(e.desc.get());
    return out;
}

ComponentRegistry& Components() {
    static ComponentRegistry registry;
    // Constructed after `registry`, so destroyed before it.
    static RegistrationSet builtins = RegisterBuiltinComponents(registry);
    return registry;
}

std::vector<const ComponentDesc*> ComponentsOf(const Scene& scene, Entity entity,
                                               const ComponentRegistry& registry) {
    std::vector<const ComponentDesc*> out;
    if (!scene.IsValid(entity)) return out;
    for (const ComponentDesc* d : registry.All()) {
        if (d->has(scene, entity)) out.push_back(d);
    }
    return out;
}

usize ResolveUnresolvedComponents(Scene& scene, const ComponentRegistry& registry) {
    usize resolved = 0;
    entt::registry& reg = scene.Registry();
    std::vector<Entity> emptied;
    for (auto [e, unresolved] : reg.view<UnresolvedComponents>().each()) {
        auto& payloads = unresolved.payloads;
        for (auto it = payloads.begin(); it != payloads.end();) {
            const ComponentDesc* desc = registry.Find(it->id);
            if (!desc) {
                ++it;
                continue;
            }
            desc->read(scene, e, it->payload);
            it = payloads.erase(it);
            ++resolved;
        }
        if (payloads.empty()) emptied.push_back(e);
    }
    for (Entity e : emptied) reg.remove<UnresolvedComponents>(e);
    return resolved;
}

usize UnresolveComponents(Scene& scene, const ComponentDesc& desc) {
    entt::registry& reg = scene.Registry();
    std::vector<Entity> holders;
    for (Entity e : reg.view<entt::entity>()) {
        if (desc.has(scene, e)) holders.push_back(e);
    }
    for (Entity e : holders) {
        UnresolvedComponent preserved;
        preserved.id = desc.id;
        preserved.owner = desc.owner;
        desc.write(scene, e, preserved.payload);
        desc.remove(scene, e);
        auto& unresolved = reg.get_or_emplace<UnresolvedComponents>(e);
        unresolved.Remove(desc.id);
        unresolved.payloads.push_back(std::move(preserved));
    }
    return holders.size();
}

// --- Built-in components ---------------------------------------------------------

namespace {

ComponentDesc EngineComponent(std::string id, std::string displayName, std::string category,
                              std::string icon, std::string tooltip) {
    ComponentDesc d;
    d.id = std::move(id);
    d.displayName = std::move(displayName);
    d.category = std::move(category);
    d.icon = std::move(icon);
    d.tooltip = std::move(tooltip);
    d.owner = "Engine";
    return d;
}

ComponentVariant LightVariant(std::string name, LightType type, std::string tooltip) {
    ComponentVariant v;
    v.displayName = std::move(name);
    v.tooltip = std::move(tooltip);
    v.apply = [type](Scene& scene, Entity e) {
        if (!scene.IsValid(e) || scene.Registry().all_of<LightComponent>(e)) return;
        scene.Registry().emplace<LightComponent>(e).type = type;
    };
    return v;
}

}  // namespace

RegistrationSet RegisterBuiltinComponents(ComponentRegistry& registry) {
    RegistrationSet set;

    ComponentDesc name = EngineComponent("Name", "Name", "General", "",
                                         "The object's display name in the World Outliner.");
    name.userAddable = false;
    set.Add(registry.Register<NameComponent>(std::move(name)));

    ComponentDesc transform = EngineComponent(
        "Transform", "Transform", "General", "", "Position, rotation and scale in the world.");
    transform.userAddable = false;
    set.Add(registry.Register<TransformComponent>(std::move(transform)));

    set.Add(registry.Register<MeshRendererComponent>(
        EngineComponent("MeshRenderer", "Mesh Renderer", "Rendering", "Cube",
                        "Draws a primitive or mesh asset with a PBR material.")));

    set.Add(registry.Register<CameraComponent>(
        EngineComponent("Camera", "Camera", "Rendering", "Camera",
                        "A view into the scene. The primary camera drives the game view.")));

    ComponentDesc light = EngineComponent("Light", "Light", "Lighting", "Light",
                                          "A light source: directional, point, spot or tube.");
    light.variants = {
        LightVariant("Directional Light", LightType::Directional,
                     "Infinitely distant sun light; drives the sky and casts cascaded "
                     "shadows."),
        LightVariant("Point Light", LightType::Point,
                     "Emits in all directions with physical inverse-square falloff."),
        LightVariant("Spot Light", LightType::Spot,
                     "A cone of light with inner and outer angles."),
        LightVariant("Tube Light", LightType::Tube, "A linear emitter of a given length."),
    };
    // Reset keeps the light's type (the type is its identity).
    light.reset = [](Scene& scene, Entity e) {
        if (!scene.Registry().all_of<LightComponent>(e)) return;
        LightComponent fresh;
        fresh.type = scene.Registry().get<LightComponent>(e).type;
        scene.Registry().replace<LightComponent>(e, fresh);
    };
    set.Add(registry.Register<LightComponent>(std::move(light)));

    ComponentDesc env = EngineComponent(
        "Environment", "Sky & Environment", "Environment", "Sphere",
        "Physically based sky atmosphere and ambient (sky) lighting.");
    env.onePerScene = true;
    set.Add(registry.Register<EnvironmentComponent>(std::move(env)));

    set.Add(registry.Register<PostProcessComponent>(
        EngineComponent("PostProcess", "Post Process Volume", "Volumes", "Eye",
                        "Bloom, exposure, tone mapping and other camera effects.")));

    return set;
}

}  // namespace Luma
