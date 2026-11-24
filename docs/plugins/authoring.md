# Authoring a plugin

This is the path the sample plugin follows. Copy `Plugins/SampleSpinner` rather than inventing a second registration style.

## Layout

```
Plugins/<Name>/
  <Name>.lplugin
  Source/<Module>/Public/...
  Source/<Module>/Private/<Module>Module.cpp   # LUMA_IMPLEMENT_MODULE(Class, "ModuleName")
  Content/                                     # optional; mounted at /<Name>/ when CanContainContent
  Config/Default<Name>.json                    # optional; Project Settings default layer
```

`<EngineRoot>/Plugins` is the engine set. A project may also have `Plugins/` next to its `.luma` file.

## Manifest

Every field is in `manifest.md`. FileVersion 1 descriptors list `Modules` with Type and LoadingPhase. Editor modules never link into `LumaEngine`.

## Code

```cpp
class MyModule final : public Luma::Plugins::IModuleInterface {
    void StartupModule(Luma::Plugins::ModuleContext& ctx) override {
        ComponentDesc desc;
        desc.id = "MyThing";
        desc.displayName = "My Thing";
        desc.category = "Simulation";
        desc.owner = ctx.Plugin()->name;
        std::string error;
        auto token = ctx.Components().Register<MyComponent>(std::move(desc), &error);
        if (!token) {
            ctx.Fail(error);
            return;
        }
        ctx.Track(std::move(token));
    }
};
LUMA_IMPLEMENT_MODULE(MyModule, "MyModule")
```

`StartupModule` must `Track` every `RegistrationToken`. Do not store tokens yourself. `ShutdownModule` may be empty: the manager releases tokens after it. `ctx.Fail` or a throw rolls those tokens back and does not call `ShutdownModule`.

`ModuleContext` (main thread only):

| Accessor | Runtime (`LumaEngine`) | Editor |
|---|---|---|
| `Components()`, `SceneSystems()` | set | set |
| `AssetTypes()`, `AssetFactories()`, `Translators()` | may be set | set |
| `Editor()` | **null** | menus, panels, settings pages, inspector drawers, images, `openPanel` |

Do not call `Editor()` without a null check. An editor module should `ctx.Fail` if it is missing.

Reflected components specialize `GetTypeInfo<T>()` and register with `Register<T>`. The Inspector then draws them. Scene files record `componentOwners` so disabling the plugin keeps the JSON as **"missing plugin — data kept"**.

## CMake (until `luma_add_plugin` in P5)

Use `luma_add_plugin` / `luma_add_module` in `cmake/LumaPlugins.cmake`. That builds a static lib (whole-archived into `LumaEngine` and tests) and, when `LUMA_MODULAR=ON`, `Plugins/<Name>/Binaries/Win64/<Config>/LumaEditor-<Module>.dll` for the editor. `LUMA_IMPLEMENT_MODULE` registers a static factory, or in a module DLL exports `LumaModuleApiVersion` / `LumaCreateModule` / `LumaDestroyModule`. Editor modules are not linked into `LumaEngine`. See `binary-compatibility.md`.

Enable the plugin per project: **Edit → Plugins**, then restart. Plugin settings pages stay under **Project Settings → Plugins**. See `content.md` for `/<Name>/` mounts and config layering.

## New Plugin wizard

**Add** in the Plugins window generates a plugin from `Engine/Plugins/Templates/` (Blank, Content Only, Editor Toolbar Button, Editor Standalone Window, Runtime Component). It writes the folder, `.lplugin`, CMake, sources, and `Resources/Icon128.png`. The editor does not compile C++: rebuild, then restart. Content-only plugins need enable + restart, not a compile.

**Edit...** on a project plugin edits FriendlyName, Description, Category, CreatedBy and URLs, VersionName, EngineVersion, Beta/Experimental, CanContainContent, and optionally replaces the icon PNG. Unknown descriptor keys are kept.

Game targets never link Editor modules (`docs/plugins/packaging.md`). Errors and fixes: `troubleshooting.md`.
