# Plugin and module lifecycle

Status of this document matches the code on `agent/T-0006-plugin-framework`.

## Sequence

```
main()
  Core init
  Load project (.luma)                          -- editor only
  PluginManager.Discover(engine Plugins/, project Plugins/)
  PluginManager.ApplyProject / RestrictEnabled
  PluginManager.MountContentAndConfig            -- /<Name>/, Default<Name>.json
  PluginManager.StartAll                        -- IPlugin instances only
  ModuleManager.SetActivePlugins(LoadableDescriptors())
  ModuleManager.LoadPhase(EarliestPossible)
  ModuleManager.LoadPhase(PostConfigInit)
  ModuleManager.LoadPhase(PreDefault)
  ModuleManager.LoadPhase(Default)              -- SampleSpinner Runtime
  ModuleManager.LoadPhase(PostDefault)
  Editor/Game UI already constructed in this engine
  ModuleManager.LoadPhase(PostEngineInit)       -- SampleSpinner Editor
  PluginManager.ApplyModuleStatuses
  Main loop
  ModuleManager.ShutdownAll()                   -- reverse of load order
  PluginManager.ShutdownAll()                   -- unmounts content/config
```

Call sites:

- `Editor/LumaEditor/EditorScreen.cpp` constructor (after project load, before `LoadScene`) and destructor.
- `Runtime/Sandbox/main.cpp` after VFS mount, before the window; shutdown after the main loop.

`LoadAllPhases()` runs the six phases in that order. The Sandbox uses `PluginTarget::Game`, so Editor/UncookedOnly modules are skipped. The editor uses `PluginTarget::Editor`.

When `LUMA_MODULAR=ON`, `LumaEditor` constructs `ModuleManager` with `DllModuleBackend` and does not whole-archive plugin modules. `LumaEngine` still whole-archives Runtime modules through `StaticModuleBackend`. See `binary-compatibility.md`.

## Rules

- Discovery never constructs a module.
- `StartupModule` failure (`ctx.Fail` or a throw) releases that module's tokens, shuts down already-started modules of that plugin and of plugins that require it, and marks them Failed. Independent plugins keep running.
- After `ShutdownModule`, every registry is as it was before the module started.
- `LUMA_IMPLEMENT_MODULE` records a factory in `StaticModuleBackend`. Static libs are whole-archived into the host.
