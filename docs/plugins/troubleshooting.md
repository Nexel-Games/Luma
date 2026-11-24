# Plugin errors

Every failure names the plugin, the stage, and a fix. The Plugins window tile and the console (`LUMA_USER_ERROR("Plugins", ...)`) show the same text. The editor still starts.

| Message (pattern) | Stage | What to do |
|---|---|---|
| `FileVersion is required` / `unsupported FileVersion` | discover | Use FileVersion 1 in `<Name>.lplugin`. |
| folder / file / identity mismatch | discover | Folder, `Name.lplugin`, and identity must be the same string. |
| `not built in this binary` | resolve | Rebuild with `LUMA_PLUGIN_<Name>=ON`, or disable the plugin. |
| `requires engine … but this engine is` | resolve | Change `EngineVersion` or use a matching engine. |
| `does not support` (platform) | resolve | Add this OS to `SupportedTargetPlatforms`. |
| `plugin dependency cycle: A -> B -> A` | resolve | Break the cycle in `Plugins` on the descriptors. |
| required dependency missing / disabled | resolve | Install, enable, and satisfy the version range, or make it optional. |
| `failed to construct` / `failed to start` | startup | Read the rest of the line; fix `Startup` / `StartupModule`. |
| `module "X" is not built for this binary` | load | Rebuild that module, or disable the plugin. |
| `has no LumaEditor-X.dll under a discovered plugin Binaries/Win64 folder` | load | Rebuild with `LUMA_MODULAR=ON`. DLLs live next to the `.lplugin`, not on PATH. |
| `could not load … Put dependent DLLs next to the module` | load | Copy native deps into that `Binaries/Win64/<Config>/` folder. |
| `missing export LumaModuleApiVersion` / `LumaCreateModule` | load | Use `LUMA_IMPLEMENT_MODULE` and rebuild. |
| `module API N does not match engine M. Rebuild the plugin` | load | Rebuild the plugin for this engine (`kModuleApiVersion`). |
| `GetChecked("X") is not a loaded module` | runtime | Load the providing plugin first. |
| project file is read-only | enable | Make the `.luma` writable. |
| `already exists` (wizard) | authoring | Pick another name or location. |

Enable/disable never unloads a running plugin. Use **Restart Now**.
