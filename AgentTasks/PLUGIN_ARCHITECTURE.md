# Luma Plugin System — Architecture

**Status:** Approved design (owner + lead, 2026-11-23) · **For:** the plugin agent (T-0006)
**Model:** Unreal Engine's plugin system: descriptors, modules with types and loading phases, a module
manager, modular (DLL) editor builds and monolithic (static) shipping builds, a Plugins browser,
plugin content and config, and a New Plugin wizard. Behaviour is described in our own words. Never copy Unreal code.
**Supersedes:** T-0006 §3–§4, `T-0006-DIRECTION-unreal-model.md` (merged into this document), and
`PLUGIN_READINESS_ASSESSMENT.md` §3 Q1. The T-0006 production standard still applies: no shortcuts, demos,
stubs or half-finished phases.

---

## 1. Goals and non-goals

**Goals**
1. A plugin adds features (components, systems, assets, editor panels, menus, settings, content) **without
   editing engine code**. The engine's own features use the same mechanisms.
2. Plugins are enabled **per project** and take effect after an editor restart.
3. Editor builds load plugin code as **DLLs**, so a prebuilt plugin dropped into `Plugins/` works without
   rebuilding the editor. Shipping builds link everything statically into one executable.
4. A broken plugin never stops the editor from starting, and disabling a plugin never loses data.

**Non-goals (this version):** hot reload / live coding, sandboxing of native code, a plugin marketplace,
scripting-language plugins, mixing compilers or CRTs across plugin binaries.

## 2. Concepts

| Concept | Meaning |
|---|---|
| **Plugin** | A folder with a `.lplugin` descriptor. It contains zero or more modules, optional content, config and resources. The unit of enable/disable. |
| **Module** | A unit of code with a startup/shutdown entry point. The unit of loading. It has a **Type** (where it may load) and a **LoadingPhase** (when). Engine subsystems are modules too. |
| **Descriptor** | `<Name>.lplugin` (JSON): identity, version, compatibility, modules, plugin dependencies, flags. |
| **Plugin Manager** | Discovers descriptors, resolves the enabled set for the project, orders it and mounts content/config. It owns plugin state. |
| **Module Manager** | Creates, starts and shuts down modules phase by phase, through a static or DLL backend. It owns module state. |
| **Registries** | The engine's extension points (already in the engine, T-0005). Modules register through a context that records every registration so it can be released automatically. |
| **Target** | What is being built and run: `Editor` (LumaEditor), `Game` (LumaSandbox / packaged game) or `Program` (tools). Module types are filtered by target. |
| **Link mode** | **Modular** (editor builds, Debug/Development: every module is a DLL) or **Monolithic** (Shipping and game targets: everything static in one exe). |

## 3. Layers

```
 ┌──────────────────────────────────────────────────────────────────────────┐
 │ Editor UI    Plugins window · New Plugin wizard · descriptor editor ·     │
 │              Project Settings → Plugins → <plugin> pages                  │
 ├──────────────────────────────────────────────────────────────────────────┤
 │ PluginManager   discovery · validation · enabled-set resolution ·         │
 │                 dependency order · content mount · config layering ·      │
 │                 project persistence · status/diagnostics                   │
 ├──────────────────────────────────────────────────────────────────────────┤
 │ ModuleManager   phase loading · startup/shutdown order · rollback ·       │
 │                 backends: StaticModuleBackend | DllModuleBackend          │
 ├──────────────────────────────────────────────────────────────────────────┤
 │ ModuleContext   tracked access to the registries (tokens per module)      │
 ├──────────────────────────────────────────────────────────────────────────┤
 │ Engine registries (exist): Components() SceneSystems() EntityTemplates()  │
 │   AssetTypes() AssetFactories() Translators() DebugDrawer()               │
 │   Editor: EditorPanels() Menus() SettingsPages() InspectorDrawers()       │
 │           EditorImages()                                                   │
 └──────────────────────────────────────────────────────────────────────────┘
```

Code location:
- `Engine/Plugins/` (`Luma::Plugins`): descriptor, PluginManager, ModuleManager, backends, ModuleContext. Runtime-safe, no editor dependency.
- `Editor/Plugins/` (`Luma::EditorPlugins`): the Plugins window, wizard and descriptor editor (editor-only module).
- `cmake/LumaPlugins.cmake`: build functions.
- `Engine/Plugins/Templates/`: wizard templates.
- `Plugins/` at the engine root: engine plugins (Built-In).
- `<Project>/Plugins/`: project plugins.

## 4. Plugin folder layout

```
Plugins/<Name>/
  <Name>.lplugin
  Source/<Module>/CMakeLists.txt
  Source/<Module>/Public/...        headers other modules may include
  Source/<Module>/Private/...       implementation
  Content/                          assets, mounted at /<Name>/ (if CanContainContent)
  Config/Default<Name>.json         plugin default settings
  Resources/Icon128.png             shown in the Plugins browser
  Binaries/Win64/                   modular build output: LumaEditor-<Module>.dll (+ .pdb)
```
`<Name>` is the plugin's identity: letters, digits, and dots in the middle. It is unique across the engine and
project folders together, and must equal the folder name and the descriptor's file name.

## 5. Descriptor (`.lplugin`)

```json
{
  "FileVersion": 1,
  "Version": 3,
  "VersionName": "1.2.0",
  "FriendlyName": "Sample Spinner",
  "Description": "Adds a Spinner component that rotates actors during Play.",
  "Category": "Simulation",
  "CreatedBy": "Luma", "CreatedByURL": "", "DocsURL": "", "SupportURL": "",
  "EngineVersion": ">=0.1.0 <0.2.0",
  "EnabledByDefault": false,
  "CanContainContent": true,
  "IsBetaVersion": false,
  "IsExperimentalVersion": false,
  "Installed": false,
  "SupportedTargetPlatforms": ["Windows"],
  "Modules": [
    { "Name": "SampleSpinner",       "Type": "Runtime", "LoadingPhase": "Default" },
    { "Name": "SampleSpinnerEditor", "Type": "Editor",  "LoadingPhase": "PostEngineInit",
      "PlatformAllowList": ["Windows"] }
  ],
  "Plugins": [
    { "Name": "OtherPlugin", "Enabled": true, "Optional": false, "Version": ">=1.0.0" }
  ]
}
```

- **Version** is an integer that increases on every release. **VersionName** is the display semver.
  **EngineVersion** is a semver range checked against `Luma::EngineVersion()`.
- **Module Type** decides the targets a module loads in:

| Type | Editor | Game (Development) | Game (Shipping) |
|---|---|---|---|
| `Runtime` | ✓ | ✓ | ✓ |
| `Developer` | ✓ | ✓ | ✗ |
| `Editor` | ✓ | ✗ | ✗ |
| `UncookedOnly` | ✓ | ✗ | ✗ (tools may load it) |

- **LoadingPhase** is a fixed point in engine startup (see §8):
  `EarliestPossible`, `PostConfigInit`, `PreDefault`, `Default`, `PostDefault`, `PostEngineInit`.
- **Plugins** lists plugin dependencies. `Enabled: true` + `Optional: false` means the dependency must be
  enabled and loaded first. `Optional: true` affects order only if it is present.
- Unknown fields are kept on rewrite (the descriptor editor must not drop them).
- `FileVersion` increments with breaking descriptor changes, and the loader migrates older versions.
- Validation is complete and field-level (file, JSON path, message). A descriptor that fails validation
  makes its plugin **Invalid**; nothing else is affected.

## 6. Project enablement

`.luma`:
```json
"Plugins": [ { "Name": "SampleSpinner", "Enabled": true },
             { "Name": "LegacyThing",   "Enabled": false } ]
```
- Only entries that differ from the plugin's `EnabledByDefault` are written.
- Unknown names are kept and shown as **Not installed**.
- Enabling a plugin through the UI enables its required dependencies (after confirmation) and writes them.
  Dependencies are never enabled silently.
- Changes apply after an editor **restart**. The editor shows a restart banner and offers **Restart Now**
  through the existing self-relaunch path.

## 7. Resolution

Input: all descriptors from the engine and project `Plugins/` folders, the project's entries, the current
target, platform and engine version, and which modules are actually built (a module compiled out of this
binary/folder can't load).

Output: an ordered list of **active plugins**, and a status for every discovered plugin:

| State | Meaning |
|---|---|
| `Disabled` | Not enabled for this project. |
| `Invalid` | Descriptor failed validation (diagnostics attached). |
| `Unavailable` | Enabled, but can't run here: engine version, platform, missing/disabled required dependency, module binary missing, module API mismatch. Each reason is listed. |
| `Active` | Will load. After startup it becomes `Loaded`, or `Failed` with the error. |

Rules:
- Required dependencies must be `Active`. Otherwise the dependent plugin is `Unavailable`, and this
  propagates to plugins that depend on it.
- Cycles are reported with the full path (`A → B → C → A`). Every plugin on the cycle is `Unavailable`.
- Order: topological by plugin dependencies; ties broken by name. Module order: by loading phase, then
  plugin order, then module order in the descriptor.

## 8. Startup and shutdown sequence

```
main()
  Core init (log, memory, job system)
  Load project descriptor (.luma)
  PluginManager.Discover(engine Plugins/, project Plugins/)        -- reads descriptors only, runs no code
  PluginManager.Resolve(project, target, platform)
  PluginManager.MountContentAndConfig(active)                      -- /<Name>/ paths, config layers
  ModuleManager.LoadPhase(EarliestPossible)
  Config system init
  ModuleManager.LoadPhase(PostConfigInit)
  ModuleManager.LoadPhase(PreDefault)
  Engine subsystems init (asset registry, renderer device, scene)
  ModuleManager.LoadPhase(Default)
  ModuleManager.LoadPhase(PostDefault)
  Editor/Game UI init (panels, menus registered so far are built)
  ModuleManager.LoadPhase(PostEngineInit)
  Main loop
  ...
  ModuleManager.ShutdownAll()          -- reverse of load order: ShutdownModule, release registrations, unload DLL
  PluginManager.UnmountContentAndConfig()
  Engine teardown
```
The exact call sites in `Editor/LumaEditor/main.cpp` and `Runtime/Sandbox/main.cpp` are documented in
`docs/plugins/lifecycle.md`. Registries must tolerate registrations arriving at any phase. Editor UI built
before `PostEngineInit` must refresh when panels or menus register later.

## 9. Modules

```cpp
namespace Luma::Plugins {

class IModuleInterface {
public:
    virtual ~IModuleInterface() = default;
    virtual void StartupModule(ModuleContext& ctx) = 0;
    virtual void ShutdownModule(ModuleContext& ctx) {}
    virtual bool SupportsDynamicReloading() const { return false; }  // always false in this version
};

// Everything a module may touch. Every registration made through it is recorded
// (RegistrationToken) and released automatically after ShutdownModule or on rollback.
class ModuleContext {
public:
    std::string_view ModuleName() const;
    const PluginDescriptor* Plugin() const;            // null for engine modules
    std::filesystem::path PluginBaseDir() const;
    ComponentRegistry&    Components();
    SceneSystemRegistry&  SceneSystems();
    EntityTemplateRegistry& EntityTemplates();
    AssetTypeRegistry&    AssetTypes();
    AssetFactoryRegistry& AssetFactories();
    TranslatorRegistry&   Translators();
    EditorServices*       Editor();                    // null outside the editor target
    void Track(RegistrationToken token);               // keeps a registration alive until shutdown
    void Log(LogLevel, std::string_view message);      // prefixed with the module name
};

class ModuleManager {
public:
    void LoadPhase(LoadingPhase phase);
    IModuleInterface* LoadModule(std::string_view name, std::string* error = nullptr);  // on demand (Active plugins only)
    bool IsLoaded(std::string_view name) const;
    template <typename T> T& GetChecked(std::string_view name);
    void ShutdownAll();
    std::vector<ModuleStatus> Statuses() const;
};
}

// In the module's Private/ code, exactly once per module:
LUMA_IMPLEMENT_MODULE(FSampleSpinnerModule, SampleSpinner)
```

- Registration **always** goes through `ModuleContext` or a `RegistrationToken` handed to `ctx.Track`.
  After a module shuts down, every registry is exactly as it was before the module started (tested).
- A module may depend on another module's **Public** headers only when its plugin depends on that plugin
  (or the module is an engine module). This is enforced by CMake: `luma_add_module(... DEPENDS ...)` refuses
  modules outside the dependency closure.
- `StartupModule` failure means it threw or called `ctx.Fail(message)`. That module's registrations are
  released. Its plugin and every plugin depending on it are rolled back (their already-started modules shut
  down in reverse order) and marked `Failed`. Everything else keeps running.
- Engine subsystems become modules over time with the same interface. Plugins can't tell the difference.

## 10. Binaries: modular and monolithic

### 10.1 Monolithic (Shipping; `LumaSandbox`/game targets)
- Every engine library and every Runtime (and, in Development, Developer) module of the enabled plugins is
  linked **statically** into the executable.
- `LUMA_IMPLEMENT_MODULE` expands to a static registrar entry (module name → factory) in a table the
  `StaticModuleBackend` reads. A generated `LumaStaticModules.cpp` per target references each module's
  registrar symbol, so the linker can't drop it.
- The game's enabled plugin set is fixed at build time from the project's `.luma`.

### 10.2 Modular (LumaEditor in Debug/Development; `LUMA_MODULAR=ON`)
- **Engine libraries become DLLs** (`LumaCore.dll`, `LumaScene.dll`, ... in `bin/<Config>/`), each with an
  export macro (`LUMA_CORE_API`, `LUMA_SCENE_API`, ...) on its public API. That makes every registry and
  singleton (`Components()`, `Menus()`, the log, the job system...) exist **once per process**, shared by the
  editor and all plugin DLLs. Without this, each plugin DLL would carry its own copy and register into a
  registry nobody reads.
- Each plugin module builds to `Plugins/<Name>/Binaries/Win64/LumaEditor-<Module>.dll`, linking the engine
  DLLs' import libraries (never the static libs).
- `LUMA_IMPLEMENT_MODULE` exports two C functions:
  - `uint32_t LumaModuleApiVersion()` (must equal the engine's `kModuleApiVersion`)
  - `Luma::Plugins::IModuleInterface* LumaCreateModule()`
- `DllModuleBackend`:
  1. Loads with `LoadLibraryExW` using an absolute path inside a discovered plugin folder, with
     `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`, so a plugin's own dependent DLLs
     next to it resolve.
  2. Checks the API version before calling anything else.
  3. Creates the module.
  4. On shutdown: `ShutdownModule`, release tokens, delete the module, then `FreeLibrary`, in that order.
  5. A DLL is never freed while anything it registered is alive.
- **ABI rules** (documented in `docs/plugins/binary-compatibility.md`):
  - Same compiler, standard library and CRT as the engine build: MSVC, `/MD` (`/MDd` in Debug). The engine
    already uses the DLL CRT.
  - Plugin binaries are only valid for the engine build they were compiled against. `kModuleApiVersion`
    changes whenever the module ABI or any exported engine API changes. A mismatch makes the plugin
    `Unavailable` ("rebuild the plugin for this engine version").
  - Memory allocated in one module may be freed in another (single shared CRT heap).
  - Exceptions don't cross the `LumaCreateModule` boundary: catch inside and fail cleanly.
  - EnTT component types used across DLLs must have stable type ids: configure EnTT so type identity
    doesn't depend on per-binary static counters (named/hashed type info) and test with a component
    defined in a plugin DLL.
- Both backends sit behind the same `ModuleManager` API and pass the same tests.

### 10.3 Build integration (`cmake/LumaPlugins.cmake`)
```cmake
luma_add_plugin(SampleSpinner)                     # reads Plugins/SampleSpinner/SampleSpinner.lplugin
luma_add_module(SampleSpinner
    PLUGIN SampleSpinner TYPE Runtime
    SOURCES Private/SampleSpinnerModule.cpp Private/SpinnerComponent.cpp
    DEPENDS Luma::Scene)
luma_add_module(SampleSpinnerEditor
    PLUGIN SampleSpinner TYPE Editor
    SOURCES Private/SampleSpinnerEditorModule.cpp
    DEPENDS SampleSpinner Luma::EditorFramework)
```
- Configure-time validation: the descriptor parses; module names and types match the descriptor; the
  `DEPENDS` closure is respected; no Editor module is linked into a game target.
- The root `CMakeLists.txt` scans `Plugins/*/` (engine) and, when `LUMA_PROJECT_DIR` is set, the project's
  `Plugins/*/` (project code plugins), and adds them.
- `LUMA_PLUGIN_<Name>=OFF` compiles a plugin out. Its status is "not built for this binary".

## 11. Content and config

- **Content:** while a plugin with `CanContainContent` is active, its `Content/` is mounted as the virtual
  root `/<Name>/` in the asset path resolver. The asset registry scans it. The Content Browser lists it
  under a **Plugins** folder (toggle "Show Plugin Content"). Assets referencing `/<Name>/...` from a
  project whose plugin is disabled report a missing-plugin error and never resolve to something else.
- **Config:** `Config/Default<Name>.json` is the plugin's default layer. The project's `Config/` overrides
  it. Plugin settings pages read and write the project layer through the settings-page registry. They
  appear in **Project Settings → Plugins → <FriendlyName>** only while the plugin is active.
- **Scene data:** components from a disabled/missing plugin are preserved by the scene serializer
  (`UnresolvedComponents`, T-0005). The Inspector shows "<Component> (plugin <Name> not loaded) — data kept".

## 12. Editor UI

**Plugins window** (Edit → Plugins; registered through `Menus()` and `EditorPanels()`; dockable):
- **Left:** category tree with counts. Roots **Built-In** (engine) and **Project**; categories from descriptors.
- **Top:** search (name, description, author, module names) and **Add** (wizard).
- **Tiles:** icon, FriendlyName, VersionName, description, "Created by" (link), Beta/Experimental badges,
  **Enabled** checkbox, status line (Active / Disabled / Unavailable + reason / Failed + error /
  Not installed), **Edit...** for project plugins.
- **Bottom banner** after a change: "Restart required to apply plugin changes" + **Restart Now**.
- Dialogs:
  - enabling a plugin lists its required dependencies (Enable All / Cancel)
  - enabling a Beta/Experimental plugin shows a warning
  - disabling a plugin lists its dependents and the open scene's components it provides
- A read-only `.luma` disables the checkboxes, with the reason shown.

**Descriptor editor:** edits FriendlyName, Description, Category, CreatedBy and URLs, VersionName,
EngineVersion, flags and the icon. Saves with unknown fields preserved.

**New Plugin wizard** (Add): templates **Blank**, **Content Only**, **Editor Toolbar Button**, **Editor
Standalone Window**, **Runtime Component**. It asks for name, author, description, category and location.
It generates the folder, descriptor, CMake files, module sources and icon from `Engine/Plugins/Templates/`,
then tells the user to rebuild (the editor doesn't compile C++). Each template must build and load
unmodified (automated test: generate → build → load → shut down).

## 13. Errors and diagnostics

Every failure has a plugin name, a stage (discover, validate, resolve, load, startup) and a fix hint. It is
shown on the plugin's tile and logged once to the console (`LUMA_USER_ERROR("Plugins", ...)`). No failure
stops the editor from starting. `docs/plugins/troubleshooting.md` lists every message and what to do.

## 14. Threading and safety

- Discovery, resolution, module loading/unloading and all registry changes run on the **main thread**.
  Registries document this.
- Discovery never executes plugin code. Native plugins run with full engine privileges: say so in
  `docs/plugins/security.md`. Only load DLLs from the discovered plugin folders, never from the search
  path or the current directory.
- No unload or reload while the editor runs. Enable/disable is restart-only.

## 15. Packaging

The packaged game gets the Runtime modules (statically linked), the `Content/` and `Config/` of the
enabled plugins, and any third-party runtime files a plugin declares. Editor/Developer/UncookedOnly
modules and plugin `Source/` are never included. There is no packaging pipeline yet. Build-level
separation is required now, and packaging is reported **BLOCKED** until the pipeline exists.

## 16. Tests (minimum)

- **Descriptor:** every field invalid/missing; migration; unknown fields kept.
- **Discovery:** duplicates across engine and project folders; name/folder mismatch; unreadable file; no code
  executed.
- **Resolution:** dependency chain, diamond, optional, missing, version range, cycle, platform, engine range,
  propagation, deterministic order.
- **Lifecycle (both backends):**
  - phase order; startup failure rollback (including dependents)
  - registries identical before start and after shutdown
  - module from a dependency available in `StartupModule`
- **DLL backend:** API version mismatch; missing export; missing dependent DLL; component defined in a
  plugin DLL serialized, edited and saved through the editor-side registries; no `FreeLibrary` with live
  registrations.
- **Project:** `.luma` round trip, unknown entries kept, dependency enable, restart flag.
- **Content/config:** `/<Name>/` mounted only while active; settings page present only while active.
- **Wizard:** each template generates, builds and loads.
- **Sandbox:** game target links no Editor module (target property check).
- **Manual (recorded in the report):**
  1. Open Plugins window → search → enable Sample Spinner (dependency dialog) → Restart Now.
  2. Add the Spinner component → Play → Stop → save → disable the plugin → restart.
  3. The component shows "data kept" → re-enable → data intact.

## 17. Delivery order

1. Descriptor + discovery + resolution (both folders), with tests.
2. ModuleManager + ModuleContext + StaticModuleBackend + phases wired into editor and Sandbox startup;
   Sample Spinner as one plugin with Runtime + Editor modules.
3. Plugins window, dialogs and restart flow. Remove the plugin list from Project Settings; keep the
   per-plugin settings pages.
4. Content mounting + config layering.
5. Modular build: engine libraries as DLLs with export macros (`LUMA_MODULAR`), DllModuleBackend, ABI
   checks, EnTT type identity. The editor runs fully modular with both samples as DLLs.
6. New Plugin wizard + descriptor editor.
7. Build/packaging separation, stabilization (10× open/close, leak check, warnings), docs.

Each step: tests first, full suite green, 0 Vulkan validation errors in a manual editor run, docs updated,
and a phase report with real command output.
