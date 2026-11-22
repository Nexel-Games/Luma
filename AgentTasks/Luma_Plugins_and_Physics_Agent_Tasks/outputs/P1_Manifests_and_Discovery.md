# P1 — Manifests, versions, and discovery

- Phase: P1
- Branch: `agent/T-0006-plugin-framework` (from `origin/dev` at `f3e326e`)
- Date: 2026-11-22
- Status: **PARTIAL**

The manifest parser, version ranges, and discovery behave as specified and their tests pass. The full engine test suite was not run. Configuring `build/` fails because the Vulkan SDK library `shaderc_combined` is not on this machine.

## Purpose

Discover `Plugins/<Id>/<Id>.lplugin` manifests from the engine and project plugin roots without constructing or calling plugin code. Reject bad documents with a field name and a message. Record when a parsed manifest does not match this engine version or this operating system.

## Behavior

- **Implemented:** JSON manifest schema 1, semver ranges (`exact`, `>=`, `>`, `<=`, `<`, `=`, `==`, `^`, `~`, space-separated AND), directory discovery, duplicate-id errors that name every path, folder/file/id mismatch errors, unreadable-file errors, engine and platform diagnostics. `CompiledPluginTable::Instantiate` is the only call that runs a factory. `DiscoverPlugins` does not call it.
- **Planned:** dependency startup, project enablement, Project Settings, sample plugins, CMake `luma_add_plugin`. Documented in `docs/plugins/overview.md`.
- **Unsupported:** `||` ranges, DLL loading, hot reload.
- **Blocked:** `ctest` of `LumaTests` on this machine (see Build).

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

`cmake --build build --config Development --target LumaPlugins` re-ran CMake and failed:

```
CMake Error at Engine/Rendering/ShaderCompiler/CMakeLists.txt:14 (find_library):
  Could not find LUMA_SHADERC_LIB using the following names: shaderc_combined
```

`D:\VulkanSDK\1.4.341.1` is not on this machine. CMake then reported that the cache had to be cleared. `build/CMakeCache.txt` is gone, so `cmake --build build` cannot regenerate until the SDK is available. Object files and libraries already in `build/` were left in place. This phase did not edit the shader compiler.

The plugin tests were compiled with the Development warning set (`/W4 /WX /std:c++20 /permissive- /utf-8 /Zc:preprocessor /MD /DLUMA_CONFIG_DEVELOPMENT=1`) and linked to the existing Development Catch2 libraries:

```
build\plugin-p1\PluginP1Tests.exe --reporter compact
RNG seed: 1212193683
All tests passed (187 assertions in 16 test cases)
```

`PluginManifest.cpp` and `PluginDiscovery.cpp` also compiled clean (`/WX`) as Debug (`/MDd /DLUMA_CONFIG_DEBUG=1`) and Shipping (`/MD /O2 /DNDEBUG /DLUMA_CONFIG_SHIPPING=1`). Release was not a separate invocation. `LumaTests` via `ctest` was **not** run.

## Docs

`docs/plugins/manifest.md` and `docs/plugins/overview.md` match this code. Later docs in T-0006 §8 (authoring, enablement, compatibility, security, troubleshooting) wait until those features exist.

## Remaining

P2 (dependency resolution and lifecycle) has not started. Full-suite evidence is blocked on a Vulkan SDK that contains `shaderc_combined`, then a fresh configure of `build/`.
