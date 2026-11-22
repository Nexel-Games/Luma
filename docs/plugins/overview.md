# Plugin framework

Status of this document matches the code on `agent/T-0006-plugin-framework`.

## Implemented

Plugins are described by a JSON manifest named `<Id>.lplugin` and discovered from trusted directories. Discovery reads those files and does not construct or call a plugin.

```
<EngineRoot>/Plugins/<Id>/<Id>.lplugin
<ProjectRoot>/Plugins/<Id>/<Id>.lplugin
```

`DiscoverPlugins` takes those `Plugins` directories (not the engine or project root). A missing directory is skipped, so a project with no `Plugins` folder is valid. A manifest that parses is reported even when the running engine version or the host platform is outside the manifest: `engineCompatible` / `platformCompatible` are false and `diagnostics` says why.

Whether a manifest has code linked into this process is a separate fact. `CompiledPluginTable` records factories. Discovery only asks `IsCompiledIn`. `Instantiate` is the call that runs a factory, and discovery never calls it. There is no DLL loader.

Version constraints are documented in `manifest.md`. The running engine version is `Luma::EngineVersion()` (`0.1.0` from the CMake project version).

The host platform string is `Windows`, `Linux`, or `macOS` (`Luma::Plugins::HostPlatform()`).

Calls are synchronous. The table and discovery are not thread-safe; the plugin manager (when it exists) runs them on the main thread.

## Planned

- Dependency resolution, startup, shutdown, and rollback (`PluginManager`).
- Per-project enablement in the `.luma` `"plugins"` object, applied on the next editor launch.
- Project Settings → Plugins.
- `PluginContext` registration into the T-0005 registries, and the two sample plugins.
- `luma_add_plugin` and editor-versus-runtime link separation.
- An `IPluginLoader` seam once an owner-approved ABI exists. No loader is compiled in today.

## Unsupported

Dynamic loading, hot reload, and sandboxing. A native plugin that is linked in runs with the engine process's privileges. Discovery itself does not execute plugin code. See `security.md` when the lifecycle lands; until then the trust boundary is: only manifests are read, and only from the two `Plugins` roots the host passes in.

## Blocked

Cook/package of a shipped game. The engine has no packaging pipeline. Build-level separation is part of a later phase, not this one.
