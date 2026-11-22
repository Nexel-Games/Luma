# T-0005 — Core extensibility: registries instead of hard-coded lists

**Status:** Done (steps 1–11). Branch `agent/T-0005-core-extensibility`; `dev` (with the material renderer) merged in.
**Assigned to:** lead (B13 rendering) + owner
**Created:** 2026-11-21 by lead
**Area / files:** `Engine/Scene`, `Engine/Project`, `Engine/Asset`, `Engine/Core` (version), `Editor/LumaEditor`,
`Editor/panels/{Inspector,WorldOutliner,ProjectSettings,ContentBrowser}`, new `Editor/Framework`, RHI debug draw (lead)

## Goal

Every core list that a plugin must extend becomes a **runtime registry**, and every built-in
registers through it. Nothing is special-cased afterwards. When this lands, T-0006 can build the plugin
framework without touching core internals. Findings and rationale: `PLUGIN_READINESS_ASSESSMENT.md` (B1–B14).

## Standard

Production implementation only: no stubs, no fake saves, no demo paths, no temporary hard-coded
fallbacks. Built-ins go through the same API plugins will use. Every registry supports
**unregistration** (a plugin shutting down must leave no dangling entry), and registering a duplicate id
is an error.

## Interfaces this task produces (T-0006 consumes these exact names)

```cpp
// Engine/Core/include/Luma/Core/Version.h (generated from CMake project(VERSION))
namespace Luma {
struct SemVer { u32 major = 0, minor = 0, patch = 0; std::string pre;
    static std::optional<SemVer> Parse(std::string_view); auto operator<=>(const SemVer&) const; };
SemVer EngineVersion();
}

// Engine/Core/include/Luma/Core/Registration.h — RAII handle every registry returns.
class RegistrationToken { public: void Reset(); /* unregisters; move-only */ };

// Engine/Scene/include/Luma/Scene/ComponentRegistry.h
struct ComponentDesc {
    std::string id;            // serialized key ("Light", "RigidBody"); stable forever
    std::string displayName, category, tooltip, icon;  // icon = Slate icon name
    std::string owner;         // "Engine" or plugin id
    bool onePerEntity = true, onePerScene = false, userAddable = true;
    std::function<void(Scene&, Entity)> add, remove;
    std::function<bool(const Scene&, Entity)> has;
    std::function<void(const Scene&, Entity, SerialValue&)> write;
    std::function<void(Scene&, Entity, const SerialValue&)> read;
    const TypeInfoBase* reflection = nullptr;  // drives the default Inspector drawer
};
class ComponentRegistry {
public:
    RegistrationToken Register(ComponentDesc);
    template <typename T> RegistrationToken Register(ComponentDesc base);  // fills add/has/remove/write/read from TypeInfo<T>
    const ComponentDesc* Find(std::string_view id) const;
    std::vector<const ComponentDesc*> All() const;  // registration order
};
ComponentRegistry& Components();  // process-wide; built-ins registered by RegisterBuiltinComponents()

// ComponentDesc also has: reset, variants, outlinerIcon, outlinerPriority.
// EntityTemplates() — "Place Actors" / auto-created entities (e.g. the sky) by id.

// Preserved payloads of components whose type is not registered (missing/disabled plugin).
struct UnresolvedComponent { std::string id, owner; SerialValue payload; };
struct UnresolvedComponents { std::vector<UnresolvedComponent> payloads; std::vector<std::string> keyOrder; };
// SceneSerializer::WriteComponents / ReadComponents: the shared component form for scene files
// and the actor clipboard (copy/paste keeps plugin and missing-plugin components).

// Engine/Scene/include/Luma/Scene/SceneSystems.h — runtime update hooks (physics, audio, ...)
class ISceneSystem {
public:
    virtual ~ISceneSystem() = default;
    virtual std::string_view Id() const = 0;
    virtual void OnPlayBegin(Scene&) {}
    virtual void FixedUpdate(Scene&, f64 dt) {}  // fixed step, fixed order
    virtual void Update(Scene&, f64 dt) {}
    virtual void OnPlayEnd(Scene&) {}
};
class SceneSystemRegistry { public: RegistrationToken Register(std::unique_ptr<ISceneSystem>, i32 order); /* ... */ };
class SceneRunner { /* fixed timestep (default 1/60, max 8 substeps; Project > Simulation page), pause, single step */ };

// Engine/Asset/include/Luma/Asset/AssetTypeRegistry.h
struct AssetTypeDesc { std::string id, displayName, icon, owner; std::vector<std::string> extensions;
    AssetType builtin; std::string nativeExtension, importSection, importFolder, dragType;
    std::function<void(const path&, const AssetOpenContext&)> open;
    std::function<std::unique_ptr<ThumbnailRenderer>()> createThumbnailRenderer; /* + thumbnail hook */ };
AssetTypeRegistry& AssetTypes();  // Find / FindByExtension / FindBuiltin / All; built-ins by RegisterBuiltinAssetTypes

// Engine/AssetImport: TranslatorRegistry::Register(std::unique_ptr<ITranslator>, owner, error) (duplicate
// extensions rejected); AssetFactories() (AssetFactoryRegistry) — import factories by asset type id,
// built-ins via RegisterBuiltinAssetFactories; AssetRequest::typeId for plugin types.

// Editor/Framework (Luma::EditorFramework): Menus() (MenuRegistry, incl. Execute), EditorPanels() + EditorPanelHost,
// SettingsPages(), InspectorDrawers() + InspectorRows, EditorImages() / IconByName.
// Project Settings persist to Config/*.json via ProjectSettingsStore; panel open/closed state (not the dock
// tree) persists in <Project>/Saved/Config/EditorLayout.json.
//   Menus by id ("File", "Edit", "Window", "Tools", plugin menus) with sections and entries.
//   Panels: id, title, icon, draw(Slate::Context&, Rect, PanelContext&), default dock slot, Window-menu entry.
//   Project Settings pages: category, page, draw, load/save via the project's Config/ (real persistence).
//   Inspector drawers: custom drawer by component id; default = reflection-driven rows.

// RHI (lead): Engine/Rendering/RHI/include/Luma/RHI/DebugDraw.h
class DebugDraw { public: enum class Mode { DepthTested, Overlay };
    void Line(...); void Box(...); void OrientedBox(...); void Sphere(...); void Capsule(...);
    const std::vector<LineVertex>& Lines(Mode) const; void Clear(); };
DebugDraw& DebugDrawer();  // process-wide; the editor feeds SceneView::lines / overlayLines and clears per frame
```

## Plan (each step test-first, suite green, separate commit)

1. **Version + RegistrationToken.** `Version.h` generated by CMake; `SemVer` parse/compare tests (pre-release ordering, invalid strings).
2. **ComponentRegistry + built-ins.** All 7 built-ins registered (`Name`, `Transform`, `MeshRenderer`, `Camera`, `Light`, `Environment`, `PostProcess`) with their current keys. Tests: registration order, duplicate id error, unregister removes, `Register<T>` round-trips fields.
3. **Serializer on the registry + unresolved preservation.** Save/load iterate the registry. Unknown keys go into `UnresolvedComponents` and are written back **byte-identical** (key order kept). Scene version 3, with migration from v2 (no data change). Tests: every existing serializer test passes unchanged; an unknown component survives load → save → load; registering the type later resolves it.
4. **Inspector on the registry.** Add Component menu built from `Components().All()` (category sections, one-per rules, tooltips). Default reflection drawer for any registered component; built-ins keep their custom drawers registered via `InspectorDrawers`. "Missing plugin" row for `UnresolvedComponents` ("RigidBody (plugin Luma.Physics.PhysX not loaded) — data kept"), read-only and removable only with a confirmation.
5. **Outliner and scene setup on the registry.** Icons and singleton rules come from `ComponentDesc`; the `emplace<...>` special cases in `EditorScreen.cpp` move to registered "Create" entries.
6. **Editor menu, panel and settings-page registries** (`Editor/Framework`). The main menu bar, Window-menu panel toggles and dock tabs are all registered; layout persistence keys by panel id.
7. **Project file as real JSON** (`Luma::Serialization`). `fileVersion` 2; reads today's JSON and INI forms; unknown keys preserved on save. Tests: old files load, unknown keys survive, round trip.
8. **Project Settings made real.** Opened from Edit → Project Settings. Pages come from the settings registry, persisted under `Config/` with real load/save and error reporting. The fake Rendering pages are removed (nothing reads them).
9. **Asset type registry.** Built-in types registered; the `switch` statements in `AssetType.cpp`, `AssetMetadata.cpp`, `ContentBrowser.cpp`, `ImportSession.cpp` and `ImportPlan.cpp` read from it. `TranslatorRegistry` accepts registered translators.
10. **Scene systems + Play-in-Editor.** `SceneSystemRegistry` and `SceneRunner`. Play copies the authoring scene, runs systems on the copy, and Stop restores (authoring scene untouched; test with a counting system). Pause and single-step work. The toolbar buttons become live.
11. **DebugDraw (lead).** RHI interface + Vulkan implementation on the overlay line pass; editor viewport and PIE draw it.

## Acceptance criteria

- [x] No component, menu, panel, settings page or asset type is listed in code outside its own registration call (grep check in review). Runtime per-asset Material Editor tabs are the documented exception (runtime panels).
- [x] Unknown components survive save/load byte-identical; built-in scene files load unchanged.
- [x] Project Settings opens, saves to disk, reloads; `.luma` keeps unknown keys.
- [x] Play → systems tick on a copy → Stop restores the authored scene exactly.
- [x] Whole suite green; 0 Vulkan validation errors in a manual editor run.
- [x] Step 11 DebugDraw: `Luma/RHI/DebugDraw.h` (Line / Box / OrientedBox / Sphere / Capsule; depth-tested or overlay; `DebugDrawer()` cleared per frame by the editor). Depth-tested lines render through a depth-tested variant of the Vulkan overlay line pass (GPU test: hidden behind geometry, visible in front, overlay always on top).

## Out of scope

Plugin manifests, discovery, enablement and lifecycle (T-0006); physics (T-0007); undo (T-0002); scripting.
