# Plugin content and config

Status of this document matches the code on `agent/T-0006-plugin-framework`.

## Content

A plugin with `CanContainContent: true` can ship assets in `Content/`. While that plugin is **active** (resolved or started for this project), `PluginManager::MountContentAndConfig` mounts the folder as the virtual root `/<Name>/` on `AssetPathResolver` and adds it to `AssetRegistry` as a plugin root.

- `/Game/...` is the project's `Assets/`. `/Engine/...` is engine content. `/<Name>/...` is that plugin only.
- If the plugin is disabled or otherwise not active, `/<Name>/...` is a **missing plugin** (`AssetPathResolver::Status::MissingPlugin`). It never resolves as `/Game/<Name>/...`.
- The Content Browser lists plugin roots under **Plugins**. They stay hidden until **Show Plugin Content** is checked (filter menu).
- Content-only plugins (no modules) can still mount.

## Config

`Config/Default<Name>.json` is the plugin's default layer. Project Settings pages owned by that plugin (`owner` = plugin name) load that document's section first. `<Project>/Config/<configFile>` overlays it. Saves write only the project file.

Pages appear under **Project Settings → Plugins → \<FriendlyName\>** only while the plugin's editor module is loaded (the page is registered in `StartupModule` and released on shutdown).

## Call sites

After `ApplyProject` / `RestrictEnabled`, before module load:

- `Editor/LumaEditor/EditorScreen.cpp`
- `Runtime/Sandbox/main.cpp`

`ShutdownAll` unmounts. Packaging of plugin `Content/` and `Config/` is **BLOCKED** (`docs/plugins/packaging.md`).
