# T-0006 — Luma Plugin Framework (production)

**Status:** Approved (owner signed off 2026-11-22; T-0005 is on `dev`)
**Assigned to:** engine agent (plugin agent)
**Created:** 2026-11-21 by lead
**Blocked by:** nothing (T-0005 registries and the icon work are on `dev`)
**Area / files:** new `Engine/Plugins/**`, new `Plugins/**` (engine plugins), `cmake/LumaPlugins.cmake`, new
`Tests/Plugins/**`, `docs/plugins/**`, the Plugins page under `Editor/panels/ProjectSettings/` (new files),
plus the exact call sites listed in §7. Nothing else.

This task replaces P0–P6 of the colleague's package (`Luma_Plugins_and_Physics_Agent_Tasks/02_*`). Your
P0 audit is accepted. Every question it raised is answered in `PLUGIN_READINESS_ASSESSMENT.md` §3, and
the core blockers it found (scene serializer, Inspector, Project Settings, project file, no registries) are
fixed for you in T-0005 before you start. **You do not need further permission for anything written in
this file.** Ask (in *Questions*) only for something this file does not cover.

---

## 1. Standard: read this first

The owner wants the **full production implementation, in one go**:

- **No shortcuts, no demo code, no placeholders.** No function that returns success without doing
  the work. No "TODO: real implementation later". No hard-coded plugin list. No toggles that don't
  persist. No UI showing state the registry doesn't have.
- **No half-finished phases.** A phase is `COMPLETE` only when its code, tests, docs and manual check are
  all done. Otherwise it is `PARTIAL` or `BLOCKED`, with the reason and evidence.
- **The right way, not the easy route.** If the correct design needs more work, do the work. If you
  believe something in this plan is wrong, say so in *Questions* with your proposed fix. Don't quietly
  do something smaller.
- Every error is reported with a clear message (the plugin id, the file, what to do). Nothing fails
  silently.
- Never claim a test ran or passed if it didn't. Paste real command output in the phase report.

## 2. What the user gets

- Plugins live in folders with a `.lplugin` manifest and are discovered at startup without executing any code.
- **Edit → Project Settings → Plugins** lists every plugin with search, categories, status, version,
  author, description, dependencies and diagnostics, plus an Enabled checkbox per project.
- Enabling a plugin checks engine version, platform and dependencies, shows what else will be enabled,
  saves to the project and asks for an editor restart. After restart the plugin's components, panels,
  menus, settings pages, asset types and scene systems are present.
- Disabling a plugin warns which enabled plugins depend on it and which open-scene components it
  provides. After restart its components show as **"missing plugin — data kept"** and nothing is lost.
- A broken manifest, missing dependency, version mismatch, cycle or failed startup is shown with a precise
  message, and the editor still starts.
- Two sample plugins prove the system end to end. Plugins build as separate CMake targets; editor-only
  plugins never link into `LumaSandbox`.

## 3. Design decisions (already made)

| Topic | Decision |
|---|---|
| Loading | **Static plugins.** Each plugin is a CMake static library that registers a factory with the plugin manager through a generated registration source file. All plugins are compiled into the binary that may use them. Manifests decide what is discovered; the project decides what is enabled. **No DLL loading in this task.** Keep a `IPluginLoader` seam so a dynamic loader can be added after an owner-approved ABI design. |
| Enable/disable | Per project, **restart required**. Never unload a running plugin with live components. |
| Manifest | `Plugins/<Id>/<Id>.lplugin`, JSON (parsed with `Luma::Serialization`). Searched under `<EngineRoot>/Plugins` and `<ProjectRoot>/Plugins`. |
| Project state | `.luma` → `"plugins": { "schema": 1, "enabled": ["Id", ...], "disabled": ["Id", ...] }` (T-0005 makes `.luma` real JSON). Plugins with `"enabledByDefault": true` are on unless listed in `disabled`. |
| Dependencies | Required dependencies are enabled together, with the user's confirmation in the UI. Optional dependencies only affect load order. Nothing unrelated is ever enabled silently. |
| Extension points | Only through T-0005 registries: components, Inspector drawers, editor panels, menus, Project Settings pages, asset types, import translators, scene systems. Scripting and rendering extensions are **deferred** and documented as such. |
| Threading | The plugin manager and all lifecycle calls run on the main thread. Registries are main-thread only (document it). |
| Trust | Discovery reads manifests only. Plugins are native code with full engine privileges: document the trust boundary in `docs/plugins/security.md`. |
| Packaging | Build-level separation (runtime/editor targets). Shipping-package pipeline does not exist: report P5 packaging as **BLOCKED**, everything else in P5 must be done. |

## 4. Interfaces to build

```cpp
// Engine/Plugins/include/Luma/Plugins/PluginManifest.h
namespace Luma::Plugins {
enum class PluginKind : u8 { Runtime, Editor, RuntimeAndEditor };
struct PluginDependency { std::string id; std::string versionRange; bool optional = false; };  // range: ">=1.2.0 <2.0.0"
struct PluginManifest {
    i32 schema = 1;
    std::string id;               // reverse-DNS-ish, [A-Za-z0-9.]+, e.g. "Luma.Physics.PhysX"
    std::string displayName, description, category, author, url;
    SemVer version;
    std::string engineVersionRange;     // checked against EngineVersion()
    PluginKind kind = PluginKind::RuntimeAndEditor;
    std::vector<std::string> platforms; // "Windows", ... ; empty = invalid
    std::vector<PluginDependency> dependencies;
    std::vector<std::string> capabilities;  // declared extension points ("components", "panels", ...)
    bool enabledByDefault = false;
    std::filesystem::path manifestPath;     // filled by discovery
};
struct ManifestError { std::filesystem::path file; std::string field; std::string message; };
std::optional<PluginManifest> ParseManifest(std::string_view json, const std::filesystem::path& file,
                                            std::vector<ManifestError>& errors);
bool SatisfiesRange(const SemVer& v, std::string_view range, std::string* error = nullptr);
}

// Engine/Plugins/include/Luma/Plugins/Plugin.h
class IPlugin {
public:
    virtual ~IPlugin() = default;
    virtual bool Startup(PluginContext& ctx, std::string& error) = 0;  // register everything here
    virtual void Shutdown(PluginContext& ctx) = 0;                     // tokens released after this
};
// What a plugin may touch: the T-0005 registries, logging, its own manifest and config folder.
// Registrations made through ctx are tracked per plugin and released automatically on shutdown/rollback.
// Names as implemented by T-0005: Components() (+ EntityTemplates()), SceneSystems(), AssetTypes(),
// AssetFactories(), TranslatorRegistry::Register; editor side: Menus(), EditorPanels(), SettingsPages(),
// InspectorDrawers(), EditorImages(). Every Register returns a RegistrationToken (Luma/Core/Registration.h).
class PluginContext { /* Components(), SceneSystems(), AssetTypes(), AssetFactories(), Translators(), Editor() (null in runtime) */ };

// Registration of a compiled-in plugin (one per plugin target, via the generated source).
#define LUMA_REGISTER_PLUGIN(Id, Type) /* registers a factory under the manifest id */

// Engine/Plugins/include/Luma/Plugins/PluginManager.h
enum class PluginState : u8 { Discovered, Invalid, Disabled, Unavailable /* platform/engine/missing dep */,
                              Resolved, Started, Failed, Stopped };
struct PluginStatus { PluginManifest manifest; PluginState state; std::vector<std::string> diagnostics;
                      bool compiledIn; bool enabledInProject; bool restartRequired; };
class PluginManager {
public:
    void Discover(std::span<const std::filesystem::path> roots);   // no plugin code runs
    void ApplyProject(const Project& project);                     // enabled set
    bool StartAll(EditorOrRuntime host);                           // topological order; rollback on failure
    void ShutdownAll();                                            // reverse order
    std::vector<PluginStatus> Statuses() const;
    // Enable/disable requests from the UI: compute the plan, then persist on confirm.
    struct ChangePlan { std::vector<std::string> alsoEnable, blockedBy, dependents, affectedComponents; bool restart; };
    ChangePlan PlanEnable(std::string_view id) const;
    ChangePlan PlanDisable(std::string_view id, const Scene* openScene) const;
    bool Commit(Project& project, const ChangePlan& plan, std::string* error);
};
```

Names may be adjusted to fit T-0005 as merged. Write any change into the phase report.

## 5. Phases

Each phase: tests first (watch them fail) → implementation → build all configs → full suite →
manual check → docs → phase report → commit. Branch `agent/T-0006-plugin-framework` from `dev`.

### P1 — Manifests, versions, discovery
- `Engine/Plugins` module (`Luma::Plugins`, static lib, depends on Core, Serialization, Project).
- Manifest parser with field-level errors; semver ranges (`>=`, `<`, `^`, `~`, exact, AND-combined).
- Discovery of both roots; a duplicate id is an error naming both paths; a manifest/folder name mismatch is an error.
- **Tests** (`Tests/Plugins/ManifestTests.cpp`, `DiscoveryTests.cpp`, temp folders): valid manifest; each
  required field missing; wrong types; bad id characters; invalid version; invalid range; incompatible
  engine range; unsupported platform; duplicate ids; unreadable file; folder/id mismatch; discovery never
  instantiates a plugin (factory-call counter stays 0).

### P2 — Dependency resolution and lifecycle
- Resolution: required/optional dependencies, version ranges, platform, engine range, missing/compiled-out
  plugins; cycle detection naming the full cycle; deterministic topological order (ties by id).
- Lifecycle: `Discovered → Resolved → Started → Stopped`. A failed `Startup` (returns false, reports
  an error, or throws) rolls back that plugin's registrations, then **stops its dependents and every
  plugin started after it, in reverse order**. The editor continues with the rest and shows the error.
- Per-plugin registration tracking through `PluginContext`: after shutdown or rollback, every
  registry is exactly as before (tests compare registry contents).
- **Tests:** chain A→B→C order; diamond; optional dependency missing (ok) vs required missing (Unavailable
  with reason); version mismatch; cycle A→B→A and A→B→C→A; startup failure rolls back exactly its own
  registrations; dependents of a failed plugin are not started; start → shutdown → start again leaves no
  duplicates; shutdown order is reverse of start.

### P3 — Project persistence and Project Settings → Plugins
- Read/write `"plugins"` in `.luma` (schema 1); unknown plugin ids in the project are kept and shown
  as "not installed".
- Page registered through T-0005's settings-page registry: search; category groups (Physics, Simulation,
  Tools, Editor, Other); row = name, version, author, description, platforms, dependency summary, status
  badge (Enabled, Disabled, Unavailable, Error, Not installed, Restart required) and an Enabled checkbox;
  details panel with full diagnostics and remediation text.
- Enable → `PlanEnable` → confirmation dialog listing plugins that will also be enabled → persist →
  restart banner. Disable → `PlanDisable` → warning listing dependents and the open scene's affected
  components → persist → restart banner. A read-only project file → page is read-only with the reason.
- Use existing Slate widgets (`MenuBuilder`/`Context::OpenMenu`, checkboxes, search field). If a widget is
  missing, ask: don't build your own widget system.
- **Tests:** project round trip; unknown ids kept; `PlanEnable` pulls required dependencies only;
  `PlanDisable` lists dependents and components; read-only file error. **Manual:** the acceptance walkthrough
  in package `03_PLUGIN_PROJECT_SETTINGS_UI.md`, recorded step by step in the report.

### P4 — Extension API and sample plugins
- `PluginContext` exposes the T-0005 registries; editor-only registries are null in `LumaSandbox`.
- `Plugins/Luma.Sample.Runtime` (runtime): registers a reflected component (`SampleSpinnerComponent`: axis,
  degrees per second) and a scene system that rotates entities during Play. It serializes, shows in Add
  Component and the Inspector, and survives disable as "missing plugin — data kept".
- `Plugins/Luma.Sample.Editor` (editor, depends on Sample.Runtime): registers a panel ("Spinner Stats"),
  a Window-menu entry and a Project Settings page with real persisted settings.
- These are real, working plugins (they double as the reference for plugin authors), not mock-ups.
- **Tests:** loading Sample.Runtime registers exactly its component and system; a scene with the component
  saves/loads; with the plugin disabled the scene loads, the payload is preserved, and saving keeps it;
  re-enabling resolves it; editor plugin missing its runtime dependency → Unavailable.

### P5 — Build, compatibility, packaging
- `cmake/LumaPlugins.cmake`: `luma_add_plugin(NAME Id KIND Runtime|Editor|RuntimeAndEditor SOURCES ...)`
  validates the manifest at configure time, creates the target, generates the registration source and
  adds it to the right host targets (`LumaEditor`; `LumaSandbox` for runtime kinds).
- Option `LUMA_PLUGIN_<ID>=ON|OFF` per plugin; a compiled-out plugin is reported as "not built in this
  binary" (state Unavailable).
- Compatibility policy (semver, engine range, manifest schema evolution) in `docs/plugins/compatibility.md`.
- **Tests/evidence:** Development + Debug builds with each sample on/off; `LumaSandbox` links no editor
  plugin (check the link map or target properties); configure-time error on a broken manifest.
  Packaging: **BLOCKED** (no pipeline). State exactly that, with what's ready for it.

### P6 — Stabilization and handoff
- Repeated open/close of a project with plugins (10×) without leaks (CRT debug heap leak report in Debug);
  enable/disable cycles; startup-failure plugin in place; new warnings = 0.
- Docs complete (see §8). `outputs/PLUGIN_FRAMEWORK_COMPLETE.md` with the status matrix and evidence.

## 6. Acceptance criteria

- [ ] All tests in §5 exist and pass; whole suite green (Development); Debug builds.
- [ ] Manual walkthrough (package doc 03) recorded with real observations.
- [ ] No hard-coded plugin ids anywhere outside the plugins' own folders.
- [ ] After any plugin shutdown or failed startup, all registries match their pre-start contents.
- [ ] Disabling a plugin never loses scene or project data.
- [ ] 0 Vulkan validation errors in a manual editor run; no new compiler warnings.
- [ ] Docs match the code (Implemented / Planned / Unsupported / Blocked sections).

## 7. Files you may edit outside your new folders

| File | Allowed change |
|---|---|
| root `CMakeLists.txt` | `add_subdirectory(Engine/Plugins)`, `include(LumaPlugins)`, plugin folders |
| `Editor/LumaEditor/main.cpp` / `EditorScreen.cpp` | create `PluginManager`, `Discover` + `ApplyProject` + `StartAll` after project load, `ShutdownAll` before teardown; restart banner hook |
| `Runtime/Sandbox/main.cpp` | the same for runtime plugins |
| `Tests/CMakeLists.txt` | add `Tests/Plugins/*` and link `Luma::Plugins` |
| `Editor/panels/ProjectSettings/` | **new files** for the Plugins page; register it through the settings-page registry |

Everything else (Scene, Inspector, Asset, Project internals, Slate, Rendering, Material, MaterialBridge) is
**not yours**: use the registries. If a registry is missing something you need, ask in *Questions*.

## 8. Documentation (same commit as the code)

`docs/plugins/`: `overview.md` (architecture diagram, lifecycle), `manifest.md` (every field, examples, errors),
`authoring.md` (create a plugin step by step using the samples: CMake, registration, components, panels,
systems), `enablement.md` (Project Settings, restart, dependencies), `compatibility.md`, `security.md`,
`troubleshooting.md` (each diagnostic message → cause → fix). Phase reports in
`AgentTasks/Luma_Plugins_and_Physics_Agent_Tasks/outputs/` using package doc 09's template.

## 9. Out of scope

DLL/hot reload, sandboxing, scripting bindings, rendering extensions, physics (T-0007), undo (T-0002), packaging pipeline.

## Questions

_(engine agent)_

## Result

_(engine agent)_

## Review

_(lead)_
