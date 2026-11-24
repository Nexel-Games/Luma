# Architecture step 4 — Content mounting and config layering

- Source: `PLUGIN_ARCHITECTURE.md` delivery step 4
- Branch: `agent/T-0006-plugin-framework`
- Date: 2026-11-23
- Status: **COMPLETE** (delivery step 4 of 7)

Active plugins with `CanContainContent` mount `Content/` at `/<Name>/`. `Config/Default<Name>.json` is the default settings layer; the project's `Config/` overlays it and is the only file that is saved.

## Purpose

Make plugin assets addressable without colliding with `/Game` or `/Engine`, fail as a missing plugin when that plugin is off, and layer plugin defaults under project settings.

## Behavior

- **Implemented:** `AssetPathResolver::MountPlugin` (`/<Name>/`). Known but unmounted names are `MissingPlugin` and never resolve as `/Game/<Name>/`. `AssetRegistry` plugin roots display as `Plugins/<Name>/...`. Content Browser filter menu: **Show Plugin Content** (off by default) groups those roots under **Plugins**. `PluginManager::MountContentAndConfig` after resolve; content-only plugins (no modules) can still mount. Settings pages load plugin defaults then the project file; save writes the project file only. Sample Spinner ships `Content/Materials/M_Spinner.lmat` and `Config/DefaultSampleSpinner.json`.
- **Planned:** Modular DLLs (step 5), New Plugin wizard and descriptor editor (step 6).
- **Unsupported:** DLL loading, hot reload.
- **Blocked:** packaging of plugin `Content/` and `Config/`.

## Tests

```
cmake --build build --config Development --target LumaTests LumaEditor LumaEngine
Linking CXX executable bin\Development\LumaTests.exe
Linking CXX executable bin\Development\LumaEngine.exe
Linking CXX executable bin\Development\LumaEditor.exe

build\bin\Development\LumaTests.exe "[plugins]" --reporter compact
Filters: [plugins]
RNG seed: 2400709946
All tests passed (675 assertions in 69 test cases)

build\bin\Development\LumaTests.exe --reporter compact
All tests passed (30426 assertions in 795 test cases)
```

## Code

| Path | Role |
|---|---|
| `Engine/Asset/include/Luma/Asset/AssetPath.h` | Plugin mounts, `Classify`, missing-plugin |
| `Engine/Asset/src/AssetRegistry.cpp` | Plugin roots, `RemoveRoot`, display paths |
| `Engine/Plugins/src/PluginManager.cpp` | `MountContentAndConfig` / defaults |
| `Editor/panels/ProjectSettings/src/ProjectSettingsStore.cpp` | Default + project overlay |
| `Editor/panels/ContentBrowser` / `FileSystemTree` | Show Plugin Content |
| `Tests/Plugins/ContentMountTests.cpp` | Mount only while active |
| `docs/plugins/content.md` | Behaviour |

## Remaining

Step 5: modular build (`LUMA_MODULAR`), `DllModuleBackend`, ABI checks. Packaging stays **BLOCKED**.
