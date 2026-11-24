# P1 — Manifests, versions, and discovery

- Phase: P1
- Branch: `agent/T-0006-plugin-framework` (from `origin/dev` at `f3e326e`)
- Date: 2026-11-22 (verified 2026-11-22 after Vulkan SDK at `D:\VulkanSDK\1.4.341.1`)
- Status: **COMPLETE**

The manifest parser, version ranges, and discovery behave as specified. Plugin tests pass inside `LumaTests`. Development `ctest` is green.

## Purpose

Discover `Plugins/<Id>/<Id>.lplugin` manifests from the engine and project plugin roots without constructing or calling plugin code. Reject bad documents with a field name and a message. Record when a parsed manifest does not match this engine version or this operating system.

## Behavior

- **Implemented:** JSON manifest schema 1, semver ranges (`exact`, `>=`, `>`, `<=`, `<`, `=`, `==`, `^`, `~`, space-separated AND), directory discovery, duplicate-id errors that name every path, folder/file/id mismatch errors, unreadable-file errors, engine and platform diagnostics. `CompiledPluginTable::Instantiate` is the only call that runs a factory. `DiscoverPlugins` does not call it.
- **Planned:** dependency startup, project enablement, Project Settings, sample plugins, CMake `luma_add_plugin`. Documented in `docs/plugins/overview.md`.
- **Unsupported:** `||` ranges, DLL loading, hot reload.
- **Blocked:** none for this phase. Cook/package remains a later-phase (P5) item.

## Code

| Path | Role |
|---|---|
| `Engine/Plugins/include/Luma/Plugins/PluginManifest.h` | `ParseManifest`, `SatisfiesRange` |
| `Engine/Plugins/src/PluginManifest.cpp` | Parser and range evaluator |
| `Engine/Plugins/include/Luma/Plugins/PluginDiscovery.h` | `DiscoverPlugins`, `CompiledPluginTable` |
| `Engine/Plugins/src/PluginDiscovery.cpp` | Directory scan |
| `Engine/Plugins/CMakeLists.txt` | Static library `Luma::Plugins` (Core, Serialization, Project) |
| `CMakeLists.txt` | `add_subdirectory(Engine/Plugins)` after Project |
| `Tests/Plugins/ManifestTests.cpp` | Parser and range cases |
| `Tests/Plugins/DiscoveryTests.cpp` | Both roots, duplicates, mismatches, factory counter |
| `Tests/CMakeLists.txt` | Those sources, link `Luma::Plugins` |
| `docs/plugins/manifest.md` | Field and range reference |
| `docs/plugins/overview.md` | What is implemented versus later phases |

Search roots are the `Plugins` directories themselves. A missing root is skipped. A parsed manifest whose engine range or platform does not match is kept, with `engineCompatible` or `platformCompatible` set false and a diagnostic that names the id, the required range or platform, and this engine.

## API

```cpp
bool Luma::Plugins::SatisfiesRange(const Luma::SemVer& version,
                                   std::string_view range,
                                   std::string* error = nullptr);
std::optional<Luma::Plugins::PluginManifest>
Luma::Plugins::ParseManifest(std::string_view json,
                             const std::filesystem::path& file,
                             std::vector<Luma::Plugins::ManifestError>& errors);
Luma::Plugins::DiscoveryResult
Luma::Plugins::DiscoverPlugins(std::span<const std::filesystem::path> roots,
                               const Luma::Plugins::CompiledPluginTable* compiled = nullptr);
```

`SatisfiesRange` returns false and writes `error` when the range text is illegal. When the range is legal and the version is outside it, it returns false and clears `error`. Ranges are not thread-safe only in the sense that they use no shared state; they are pure. Discovery and `CompiledPluginTable` are main-thread only and are not synchronized.

`Instantiate` increments `InstantiateCount` only after the factory returns. A missing factory returns false and does not increment.

## Build and tests

Vulkan SDK: `D:\VulkanSDK\1.4.341.1` (version 1.4.341), including `Lib/shaderc_combined.lib` and `Bin/glslc.exe`. Configure:

```
cmake --fresh --preset msvc -DVulkan_ROOT=D:/VulkanSDK/1.4.341.1
-- Found Vulkan: D:/VulkanSDK/1.4.341.1/Lib/vulkan-1.lib (found version "1.4.341")
```

Development build of the T-0006 tree succeeded (`Scripts/Build.ps1`, 286 remaining steps after switching from `dev`). Plugin filter:

```
build\bin\Development\LumaTests.exe "[plugins]" --reporter compact
Filters: [plugins]
RNG seed: 986071188
All tests passed (187 assertions in 16 test cases)
```

`ctest --test-dir build -C Development --output-on-failure`:

```
1/2 Test #1: LumaPreferencesTests .............   Passed    0.39 sec
2/2 Test #2: LumaTests ........................   Passed   49.26 sec
100% tests passed out of 2
Total Test time (real) =  49.79 sec
```

Debug: `cmake --build build --config Debug --target LumaPlugins LumaTests LumaPreferencesTests` succeeded. `build\bin\Debug\LumaTests.exe "[plugins]"`: 187 assertions, 16 cases, all passed (RNG seed 1686162253). Shipping was not rebuilt in this pass.

## Docs

`docs/plugins/manifest.md` and `docs/plugins/overview.md` match this code. Later docs in T-0006 §8 (authoring, enablement, compatibility, security, troubleshooting) wait until those features exist.

## Remaining

P2 (dependency resolution and lifecycle) has not started.
