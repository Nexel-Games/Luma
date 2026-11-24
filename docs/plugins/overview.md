# Plugin framework

Status of this document matches the code on `agent/T-0006-plugin-framework`.

## Implemented

Plugins are described by a JSON `.lplugin` descriptor (`FileVersion` 1, or a migrated schema 1 document) named `<Name>.lplugin` and discovered from trusted directories. Discovery reads those files and does not construct or call a plugin. `ResolvePlugins` then assigns Disabled / Invalid / Unavailable / Active without loading code.

```
<EngineRoot>/Plugins/<Id>/<Id>.lplugin
<ProjectRoot>/Plugins/<Id>/<Id>.lplugin
```

`DiscoverPlugins` takes those `Plugins` directories (not the engine or project root). A missing directory is skipped, so a project with no `Plugins` folder is valid. A manifest that parses is reported even when the running engine version or the host platform is outside the manifest: `engineCompatible` / `platformCompatible` are false and `diagnostics` says why.

Whether a manifest has code linked into this process is a separate fact. `CompiledPluginTable` records factories. Discovery only asks `IsCompiledIn`. `Instantiate` is the call that runs a factory, and discovery never calls it. `DllModuleBackend` loads `LumaEditor-<Module>.dll` from a discovered plugin's `Binaries/Win64/<Config>/` folder when `LUMA_MODULAR=ON`.

Version constraints are documented in `manifest.md`. The running engine version is `Luma::EngineVersion()` (`0.1.0` from the CMake project version).

The host platform string is `Windows`, `Linux`, or `macOS` (`Luma::Plugins::HostPlatform()`).

Calls are synchronous. The table, discovery, and `PluginManager` are main-thread only.

## Lifecycle (implemented)

`PluginManager` discovers, resolves, starts, and stops plugins.

States: `Discovered` → `Resolved` → `Started` → `Stopped`. Failures are `Failed`. Plugins that cannot run are `Unavailable` (engine range, platform, missing/compiled-out/disabled required dependency, version mismatch, cycle, editor-only on a runtime host). A discovered plugin that is not in the enabled set is `Disabled`.

Required dependencies must be installed, compiled in, enabled, and in range. Optional dependencies only affect load order when they themselves will start. Cycles are reported with the full path (`A -> B -> A`). Start order is a topological sort, ties broken by id. Shutdown is the reverse of the last successful start list.

`Startup` registers through `PluginContext::Track`. A false return or a throw rolls back that plugin's tokens, marks dependents `Unavailable`, and continues with independent plugins. `Shutdown` runs `IPlugin::Shutdown` then releases tokens.

`CompiledPluginTable` implements `IPluginLoader` for `IPlugin` instances. Native module code is either statically registered or loaded by `DllModuleBackend` from a discovered plugin folder.

`ApplyProject(nullptr)` enables every discovered plugin (used by tests). A project file uses `"plugins": { "schema": 1, "enabled": [...], "disabled": [...] }`. Unknown ids stay in the file and appear as `NotInstalled`. `PlanEnable` / `PlanDisable` / `Commit` persist enablement without unloading a running plugin. **Edit → Plugins** is the browser; **Restart Now** relaunches the editor. Per-plugin settings pages stay in Project Settings (`docs/plugins/enablement.md`).

`ModuleContext` exposes T-0005 registries: components, scene systems, asset types, factories, translators, and `Editor()` (menus, panels, settings pages, inspector drawers, images). `Editor()` is null in `LumaEngine`. Sample: `Plugins/SampleSpinner` (Runtime module: Spinner component + Play rotation; Editor module: Spinner Stats panel, Window command, Project Settings page; `Content/` mounted at `/SampleSpinner/` while active). See `authoring.md`, `lifecycle.md`, and `content.md`.

**Add** runs the New Plugin wizard. **Edit...** on a project plugin opens the descriptor editor (unknown fields kept).

`LumaEngine` whole-archives Runtime modules only. Configure refuses to link Editor/UncookedOnly modules into the game. `PlanGamePackage` describes a future cook and is always **blocked**. See `packaging.md` and `troubleshooting.md`.

## Unsupported

Hot reload and sandboxing. Editor plugin modules load as DLLs (`DllModuleBackend`); the game still whole-archives Runtime modules. Native plugin code runs with the engine process's privileges. Discovery itself does not execute plugin code. See `security.md`.

## Blocked

Cook/package of a shipped game. The engine has no packaging pipeline. Build-level split (no Editor module in `LumaEngine`) is in place; copying plugin `Content/` and `Config/` into a package is not.
