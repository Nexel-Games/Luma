# Architecture step 1 — Descriptor, discovery, resolution

- Source: `PLUGIN_ARCHITECTURE.md` (approved 2026-11-23), copied to `AgentTasks/PLUGIN_ARCHITECTURE.md`
- Branch: `agent/T-0006-plugin-framework`
- Date: 2026-11-23
- Status: **COMPLETE** (delivery step 1 of 7)

Discovery reads `.lplugin` files and never runs plugin code. `ResolvePlugins` assigns Disabled / Invalid / Unavailable / Active without loading modules. Existing `PluginManager` still starts plugins through `PluginManifest` (`ToManifest`).

## Purpose

Replace schema-1-only parsing as the discovery source of truth with FileVersion 1 descriptors, keep schema 1 via migration, and resolve the enabled set independently of lifecycle.

## Behavior

- **Implemented:** `ParseDescriptor` / `WriteDescriptor` (FileVersion 1; unknown top-level keys kept). Schema 1 documents migrate (`migratedFromSchema1`). `DiscoverPlugins` uses `ParseDescriptor` then `ToManifest`. `ResolvePlugins` (project `Plugins` array or legacy `plugins` object, engine range, platform, required deps, cycles, missing binaries, not-installed entries, deterministic topo order).
- **Planned:** ModuleManager, Plugins window, content/config, modular DLLs, wizard (steps 2–6).
- **Unsupported:** DLL loading, hot reload.
- **Blocked:** packaging (architecture §15).

## Tests

```
cmd /c "Scripts\_env.bat cmake --build build --config Development --target LumaPlugins LumaTests"
Linking CXX executable bin\Development\LumaTests.exe

build\bin\Development\LumaTests.exe "[plugins]" --reporter compact
Filters: [plugins]
RNG seed: 4193718434
All tests passed (588 assertions in 54 test cases)

build\bin\Development\LumaTests.exe --reporter compact
All tests passed (30331 assertions in 778 test cases)
```

## Code

| Path | Role |
|---|---|
| `Engine/Plugins/include/Luma/Plugins/PluginDescriptor.h` | FileVersion 1 types, `ParseDescriptor`, `ResolvePlugins` |
| `Engine/Plugins/src/PluginDescriptor.cpp` | Parser, rewrite, migration, resolution |
| `Engine/Plugins/src/PluginDiscovery.cpp` | Uses `ParseDescriptor` |
| `Tests/Plugins/DescriptorTests.cpp` | Fields, migration, unknown keys kept |
| `Tests/Plugins/ResolutionTests.cpp` | Chain, diamond, optional, missing, range, cycle, platform, engine, propagation, order |
| `docs/plugins/manifest.md` | FileVersion 1 + schema 1 |

## Remaining

Step 2: ModuleManager + ModuleContext + StaticModuleBackend + phases in editor and Sandbox; Sample Spinner as one plugin with Runtime + Editor modules.
