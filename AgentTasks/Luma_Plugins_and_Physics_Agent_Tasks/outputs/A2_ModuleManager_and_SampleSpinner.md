# Architecture step 2 — ModuleManager and Sample Spinner modules

- Source: `PLUGIN_ARCHITECTURE.md` delivery step 2
- Branch: `agent/T-0006-plugin-framework`
- Date: 2026-11-23
- Status: **COMPLETE** (delivery step 2 of 7)

`ModuleManager` loads `IModuleInterface` modules through `ModuleContext` and `StaticModuleBackend`. Editor and Sandbox run the six loading phases. Sample Spinner is one plugin with a Runtime module and an Editor module.

## Purpose

Make modules the unit of loading, with phase order, rollback, and registry restore. Keep PluginManager as discovery, enablement, and IPlugin start.

## Behavior

- **Implemented:** `IModuleInterface`, `ModuleContext` (`Track`, `Fail`, registries, `Editor()` null outside the editor), `StaticModuleBackend` / `LUMA_IMPLEMENT_MODULE`, `ModuleManager` (`LoadPhase`, `LoadAllPhases`, `LoadModule`, `GetChecked`, reverse `ShutdownAll`). Startup failure rolls back that plugin and dependents. Hosts: `EditorScreen` (`PluginTarget::Editor`) and `LumaEngine` (`PluginTarget::Game`). `Plugins/SampleSpinner`: Runtime registers Spinner + Play system; Editor registers Spinner Stats, Window menu, Project Settings page. `LumaEngine` whole-archives only the Runtime module.
- **Planned:** Plugins window (step 3), content/config (step 4), modular DLLs (step 5), wizard (step 6).
- **Unsupported:** `DllModuleBackend`, hot reload.
- **Blocked:** packaging.

## Tests

```
cmake --build build --config Development --target LumaPlugins LumaSampleSpinner LumaSampleSpinnerEditor LumaTests LumaEditor LumaEngine
Linking CXX executable bin\Development\LumaTests.exe
Linking CXX executable bin\Development\LumaEngine.exe
Linking CXX executable bin\Development\LumaEditor.exe

build\bin\Development\LumaTests.exe "[plugins]" --reporter compact
Filters: [plugins]
RNG seed: 145107710
All tests passed (617 assertions in 59 test cases)

build\bin\Development\LumaTests.exe --reporter compact
All tests passed (30360 assertions in 783 test cases)
```

`findstr SampleSpinnerEditor` matches `LumaEditor.exe` and does not match `LumaEngine.exe`. `SampleSpinner.Spinner` is in `LumaEngine.exe`.

## Code

| Path | Role |
|---|---|
| `Engine/Plugins/include/Luma/Plugins/Module.h` | Module API, static backend |
| `Engine/Plugins/include/Luma/Plugins/ModuleManager.h` | Phase loader |
| `Engine/Plugins/src/Module.cpp`, `ModuleManager.cpp` | Implementation |
| `Plugins/SampleSpinner/` | One plugin, two modules |
| `Tests/Plugins/ModuleTests.cpp` | Phase order, rollback, registries, GetChecked, Game skip |
| `docs/plugins/lifecycle.md` | Call sites |

## Remaining

Step 3: Plugins window, dialogs, restart flow. Remove the plugin list from Project Settings; keep per-plugin settings pages.
