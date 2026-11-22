#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Registration.h"
#include "Luma/Core/Types.h"
#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/Scene.h"

// Entity templates: the "Place Actors" / Create GameObject entries (Empty Actor,
// Cube, Point Light, Sky & Environment, ...). Each template builds a configured
// entity. The World Outliner's create menu and the editor's scene setup are driven
// entirely by this registry; built-ins register exactly like plugins.
//
// Main-thread only.

namespace Luma {

struct EntityTemplateContext {
    // 1-based running count of entities the caller has created, used to stagger
    // spawn positions so new actors don't overlap.
    i32 index = 1;
};

struct EntityTemplate {
    std::string id;           // stable id ("PointLight")
    std::string displayName;  // menu label ("Point Light")
    std::string category;     // menu submenu ("Lights")
    std::string categoryIcon; // icon of the category submenu (first template wins)
    std::string icon;         // icon name of the entry
    std::string tooltip;
    std::string owner;        // "Engine" or the plugin id
    // When set (a component id), the scene may hold only one entity with that
    // component: instantiating selects the existing holder instead.
    std::string singletonComponent;
    // Instantiated in every scene that has no singletonComponent holder (scene
    // setup). Requires singletonComponent.
    bool autoCreate = false;
    std::function<Entity(Scene&, const EntityTemplateContext&)> create;
};

class EntityTemplateRegistry {
public:
    EntityTemplateRegistry();
    EntityTemplateRegistry(const EntityTemplateRegistry&) = delete;
    EntityTemplateRegistry& operator=(const EntityTemplateRegistry&) = delete;

    RegistrationToken Register(EntityTemplate entityTemplate, std::string* error = nullptr);
    const EntityTemplate* Find(std::string_view id) const;
    std::vector<const EntityTemplate*> All() const;  // registration order
    u64 Revision() const { return m_revision; }

private:
    struct Entry {
        u64 handle = 0;
        std::unique_ptr<EntityTemplate> value;
    };
    std::vector<Entry> m_entries;
    std::shared_ptr<void> m_alive;
    u64 m_nextHandle = 1;
    u64 m_revision = 0;
};

// The process-wide registry; built-ins registered on first access.
EntityTemplateRegistry& EntityTemplates();
RegistrationSet RegisterBuiltinEntityTemplates(EntityTemplateRegistry& registry);

// Creates the template's entity (or returns the existing singleton holder).
Entity Instantiate(Scene& scene, const EntityTemplate& entityTemplate,
                   const EntityTemplateContext& context);

// Instantiates every autoCreate template whose singleton is missing. Returns the
// number of entities created.
usize EnsureAutoCreatedEntities(Scene& scene,
                                const EntityTemplateRegistry& registry = EntityTemplates());

// `base` when no entity is named that; otherwise "base 1", "base 2", ... (the
// lowest free number).
std::string UniqueEntityName(const Scene& scene, std::string_view base);

}  // namespace Luma
