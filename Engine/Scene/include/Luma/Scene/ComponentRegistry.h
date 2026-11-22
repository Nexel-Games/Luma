#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Registration.h"
#include "Luma/Core/Types.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Serialization/Reflection.h"
#include "Luma/Serialization/SerialValue.h"

// The runtime list of component types the engine knows about.
//
// Everything that needs "all component types" (scene serialization, the
// Inspector's Add Component menu and drawers, the World Outliner, plugins) asks
// this registry. Built-in components register through exactly the same API a
// plugin uses (see RegisterBuiltinComponents), so nothing is special-cased.
//
// Main-thread only.

namespace Luma {

// A preset that adds the component and configures it (e.g. "Point Light" adds a
// Light with type = Point). When a component has variants, the Add Component menu
// lists the variants instead of the plain component.
struct ComponentVariant {
    std::string displayName;
    std::string tooltip;
    std::string icon;  // Slate icon name; empty = the component's icon
    // Called on an entity that does not have the component yet; must add it.
    std::function<void(Scene&, Entity)> apply;
};

struct ComponentDesc {
    std::string id;            // serialized key ("Light", "RigidBody"); stable forever
    std::string displayName;   // "Point Light", "Rigid Body"
    std::string category;      // Add Component menu section ("Rendering", "Physics")
    std::string tooltip;
    std::string icon;          // Slate icon name
    std::string owner;         // "Engine" or the plugin id

    bool onePerEntity = true;  // an entity holds at most one (always true for ECS storage)
    bool onePerScene = false;  // at most one entity in the scene may hold it
    bool userAddable = true;   // offered in Add Component / removable by the user

    std::function<void(Scene&, Entity)> add;     // add with defaults if missing
    std::function<void(Scene&, Entity)> remove;  // remove if present
    std::function<bool(const Scene&, Entity)> has;
    std::function<void(const Scene&, Entity, SerialValue&)> write;  // component -> payload
    std::function<void(Scene&, Entity, const SerialValue&)> read;   // payload -> component
    std::function<void(Scene&, Entity)> reset;   // restore defaults ("Reset to Defaults")

    // Drives the default (reflection-based) Inspector drawer. May be null for a
    // component that only has a custom drawer.
    const TypeInfoBase* reflection = nullptr;

    std::vector<ComponentVariant> variants;
};

class ComponentRegistry {
public:
    ComponentRegistry();
    ComponentRegistry(const ComponentRegistry&) = delete;
    ComponentRegistry& operator=(const ComponentRegistry&) = delete;
    ~ComponentRegistry();

    // Registers a component type. Fails (inactive token, `error` set, logged) on an
    // invalid descriptor or a duplicate id. Destroying the token unregisters it.
    RegistrationToken Register(ComponentDesc desc, std::string* error = nullptr);

    // Registers a reflected ECS component type T: fills add/has/remove/write/read
    // and `reflection` from GetTypeInfo<T>(). Fields already set in `base` win.
    template <typename T>
    RegistrationToken Register(ComponentDesc base, std::string* error = nullptr);

    const ComponentDesc* Find(std::string_view id) const;
    std::vector<const ComponentDesc*> All() const;  // registration order
    usize Size() const { return m_entries.size(); }

    // Changes on every successful register/unregister (lets UI caches refresh).
    u64 Revision() const { return m_revision; }

private:
    struct Entry {
        u64 handle = 0;
        std::unique_ptr<ComponentDesc> desc;
    };
    void Unregister(u64 handle);

    std::vector<Entry> m_entries;
    std::shared_ptr<void> m_alive;
    u64 m_nextHandle = 1;
    u64 m_revision = 0;
};

// The process-wide component registry. The built-in components are registered on
// first access (through RegisterBuiltinComponents, like any other client).
ComponentRegistry& Components();

// Registers the 7 built-in components (Name, Transform, MeshRenderer, Camera,
// Light, Environment, PostProcess) into `registry`, in that order. The returned
// set owns the registrations.
RegistrationSet RegisterBuiltinComponents(ComponentRegistry& registry);

// Registered components present on `entity`, in registration order.
std::vector<const ComponentDesc*> ComponentsOf(const Scene& scene, Entity entity,
                                               const ComponentRegistry& registry = Components());

// --- Components whose type is not registered ------------------------------------

// One preserved component: the payload of a component whose type is not registered
// (its plugin is missing or disabled). Kept exactly as loaded and written back on
// save, so opening a scene without a plugin never destroys the plugin's data.
struct UnresolvedComponent {
    std::string id;      // the component key in the scene file
    std::string owner;   // the plugin that provides it (from the file), may be empty
    SerialValue payload; // the component's JSON value, untouched
};

// ECS component attached to entities that carry preserved payloads. `keyOrder`
// is the order of the entity's component keys in the file it came from; saving
// follows it so the written file matches the loaded one byte for byte.
struct UnresolvedComponents {
    std::vector<UnresolvedComponent> payloads;
    std::vector<std::string> keyOrder;

    const UnresolvedComponent* Find(std::string_view id) const {
        for (const UnresolvedComponent& p : payloads) {
            if (p.id == id) return &p;
        }
        return nullptr;
    }
    bool Remove(std::string_view id) {
        for (auto it = payloads.begin(); it != payloads.end(); ++it) {
            if (it->id == id) {
                payloads.erase(it);
                return true;
            }
        }
        return false;
    }
};

// Turns preserved payloads whose type is now registered into live components
// (e.g. after a plugin started). Returns the number of components resolved.
usize ResolveUnresolvedComponents(Scene& scene, const ComponentRegistry& registry = Components());

// Converts every live `desc` component in the scene into a preserved payload and
// removes the live component (used before a component type is unregistered while a
// scene still holds it). Returns the number of components demoted.
usize UnresolveComponents(Scene& scene, const ComponentDesc& desc);

// --- Register<T> ---------------------------------------------------------------

template <typename T>
RegistrationToken ComponentRegistry::Register(ComponentDesc base, std::string* error) {
    if (!base.add) {
        base.add = [](Scene& scene, Entity e) {
            if (!scene.Registry().all_of<T>(e)) scene.Registry().emplace<T>(e);
        };
    }
    if (!base.remove) {
        base.remove = [](Scene& scene, Entity e) { scene.Registry().remove<T>(e); };
    }
    if (!base.has) {
        base.has = [](const Scene& scene, Entity e) {
            return scene.IsValid(e) && scene.Registry().all_of<T>(e);
        };
    }
    if (!base.write) {
        base.write = [](const Scene& scene, Entity e, SerialValue& out) {
            out = SerializeObject(scene.Registry().get<T>(e));
        };
    }
    if (!base.read) {
        base.read = [](Scene& scene, Entity e, const SerialValue& in) {
            T component{};
            DeserializeObject(in, component);
            scene.Registry().emplace_or_replace<T>(e, std::move(component));
        };
    }
    if (!base.reset) {
        base.reset = [](Scene& scene, Entity e) {
            if (scene.Registry().all_of<T>(e)) scene.Registry().replace<T>(e, T{});
        };
    }
    if (!base.reflection) base.reflection = &GetTypeInfo<T>();
    return Register(std::move(base), error);
}

}  // namespace Luma
