# Plugin binary compatibility

Status of this document matches the code on `agent/T-0006-plugin-framework`.

## When it applies

`LUMA_MODULAR=ON` (the default): engine libraries are DLLs (`bin/<Config>/LumaCore.dll`, `LumaScene.dll`, …). Editor plugin modules are `Plugins/<Name>/Binaries/Win64/<Config>/LumaEditor-<Module>.dll`, loaded with `LoadLibraryExW` (`LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`).

`LUMA_MODULAR=OFF`: everything is static. Use that for a Shipping-style monolith.

`LumaEngine` always whole-archives Runtime modules even when the engine itself is modular.

## Rules

- Same compiler, standard library and CRT as this engine build: MSVC, `/MD` (`/MDd` in Debug). CMake uses the DLL CRT (`CMAKE_MSVC_RUNTIME_LIBRARY`).
- Plugin binaries are only valid for the engine build they were compiled against. `Luma::Plugins::kModuleApiVersion` (currently 1) is checked before `LumaCreateModule`. A mismatch makes the module fail to load: rebuild the plugin for this engine version. Bump `kModuleApiVersion` whenever the module ABI or an exported engine API changes.
- Memory allocated in one module may be freed in another (one CRT heap).
- Exceptions do not cross `LumaCreateModule`: the export catches and returns null.
- A DLL is never `FreeLibrary`'d while anything it registered is still tracked. Shutdown order is `ShutdownModule`, release tokens, destroy the module, then `FreeLibrary`.
- Destroy every `Scene` (or unresolve those components) before unloading a module. EnTT storage for a plugin type still runs destructors from that DLL.

## EnTT type identity

`entt::registry` storage is keyed by `type_hash` (MSVC `__FUNCSIG__` / pretty name), not the sequential `type_index`. Component types defined in a plugin DLL are therefore the same identity as in the engine process, provided the plugin is built with this engine's compiler and flags. Do not mix toolchains.

## Exports

Modular engine DLLs set `WINDOWS_EXPORT_ALL_SYMBOLS` so registries and singletons (`Components()`, `Menus()`, the log) exist once per process. `LUMA_CORE_API` / `LUMA_SCENE_API` / `LUMA_PLUGINS_API` exist for explicit exports; types that contain STL members are not annotated (C4251 / C4275 under `/WX`).
