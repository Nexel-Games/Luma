# P4 — Extension API and sample plugins

- Phase: P4
- Branch: `agent/T-0006-plugin-framework`
- Date: 2026-11-22
- Status: **COMPLETE**

`PluginContext` exposes the T-0005 registries. `Editor()` is null in `LumaEngine`. Two sample plugins prove components, scene systems, panels, menus, and a persisted settings page.

## Purpose

Give plugins a real registration API and two reference plugins authors can copy.

## Behavior

- **Implemented:** `PluginHostServices` / `PluginEditorServices` on `PluginContext` (`Components`, `SceneSystems`, `AssetTypes`, `AssetFactories`, `Translators`, `Editor()` with menus, panels, settings pages, inspector drawers, images, `openPanel`). `LUMA_REGISTER_PLUGIN` writes `CompiledPluginTable::Global()`. Hosts whole-archive static plugin libs. `Luma.Sample.Runtime`: reflected `SampleSpinnerComponent` (axis, degrees per second) and a Play `Update` system. `Luma.Sample.Editor`: Spinner Stats panel, Window → Plugins → Spinner Stats, Project Settings → Sample Spinner (`Config/SampleSpinner.json`). Editor starts plugins **before** loading the scene so plugin components resolve on open.
- **Planned:** `luma_add_plugin` (P5) as the CMake helper; samples already link editor vs runtime by hand.
- **Unsupported:** DLL loading. `Editor()` is null in the sandbox on purpose.
- **Blocked:** none for this phase.

## Registry gap

`SceneSystemRegistry` had no query API. P4 adds `Size()` and `Contains(id)` so tests can assert the spinner system registered. No other Scene/Inspector internals were changed.

## Tests

`Tests/Plugins/SamplePluginTests.cpp`

```
build\bin\Development\LumaTests.exe "[plugins]" --reporter compact
All tests passed (444 assertions in 37 test cases)

build\bin\Development\LumaTests.exe --reporter compact
All tests passed (30187 assertions in 761 test cases)
```

## Link evidence

Development ninja for `LumaEngine.exe` lists `LumaSampleRuntime.lib` and does **not** list `LumaSampleEditor.lib`. `LumaEditor.exe` links both.

## Docs

`docs/plugins/authoring.md`, `docs/plugins/security.md`, updates to `overview.md`.
