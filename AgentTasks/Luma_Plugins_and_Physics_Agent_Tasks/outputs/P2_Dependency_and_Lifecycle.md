# P2 — Dependency resolution and lifecycle

- Phase: P2
- Branch: `agent/T-0006-plugin-framework`
- Date: 2026-11-22
- Status: **COMPLETE**

## Purpose

Resolve plugin dependencies, start compiled-in plugins in a stable order, roll back a failed `Startup`, and shut down in reverse order without leaving registry entries behind.

## Behavior

- **Implemented:** `PluginManager`, `IPlugin`, `PluginContext`, `IPluginLoader` (static table only). Required/optional deps, version ranges, missing/compiled-out, disabled required deps, engine/platform, editor-only on runtime, cycle detection with the full path, topological start (id tie-break), reverse shutdown, token tracking, throw-as-failure, dependents of a failed plugin are not started, independent plugins still start.
- **Planned:** Project Settings UI, sample plugins, extra registries on the context, CMake plugin targets.
- **Unsupported:** DLL loading, hot unload while live, OR version ranges.
- **Blocked:** none for this phase.

## API

```cpp
Luma::Plugins::PluginManager mgr(compiledTable);
mgr.SetRegistries(&components, &sceneSystems);
mgr.Discover(roots);
mgr.ApplyProject(project);          // nullptr = enable all discovered
mgr.RestrictEnabled(ids);           // optional subset
bool ok = mgr.StartAll(Luma::Plugins::PluginHostKind::Editor);
mgr.ShutdownAll();
```

`IPlugin::Startup` must `ctx.Track(...)` every `RegistrationToken`. Tokens are released on failed Startup and after Shutdown. `PluginContext::Components()` / `SceneSystems()` may be null.

## Tests

`Tests/Plugins/LifecycleTests.cpp` covers the T-0006 P2 list: A→B→C order, diamond, optional missing, required missing, version mismatch, A↔B and A→B→C→A cycles, Startup false and throw rollback, dependents skipped, independent plugin continues, start/shutdown/start without duplicate components, shutdown reverse order, compiled-out, editor-only host.

```
build\bin\Development\LumaTests.exe "[plugins]" --reporter compact
All tests passed (332 assertions in 28 test cases)

ctest --test-dir build -C Development --output-on-failure
1/2 Test #1: LumaPreferencesTests .............   Passed    0.41 sec
2/2 Test #2: LumaTests ........................   Passed   71.60 sec
100% tests passed out of 2
```

## Remaining

P3: Project Settings → Plugins and the persisted `"plugins"` object as the product UI.
