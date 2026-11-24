# Architecture step 3 — Plugins window, dialogs, restart

- Source: `PLUGIN_ARCHITECTURE.md` delivery step 3
- Branch: `agent/T-0006-plugin-framework`
- Date: 2026-11-23
- Status: **COMPLETE** (delivery step 3 of 7)

**Edit → Plugins** opens a dockable Plugins browser. Enable/disable is still restart-required. The Project Settings plugin list is gone; per-plugin settings pages remain.

## Purpose

Replace Project Settings → Plugins with the architecture's Plugins window, including confirmation dialogs and Restart Now through the existing self-relaunch path.

## Behavior

- **Implemented:** Category tree (Built-In / Project), search (name, description, author, modules), tiles (FriendlyName, version, description, Created by, Beta/Experimental, Enabled, status). Enable confirms required dependencies and beta/experimental. Disable lists dependents and scene components. Read-only `.luma` disables checkboxes. Restart banner in the window and the status bar; **Restart Now** relaunches `LumaEditor` with `--project` (same as the project browser). `Add` and `Edit...` explain that the wizard and descriptor editor are not in this step.
- **Planned:** Content/config (step 4), modular DLLs (step 5), New Plugin wizard and descriptor editor (step 6).
- **Unsupported:** DLL loading, hot reload.
- **Blocked:** packaging.

## Tests

```
cmake --build build --config Development --target LumaPlugins LumaEditorPlugins LumaPanelProjectSettings LumaEditor LumaTests
Linking CXX executable bin\Development\LumaTests.exe
Linking CXX executable bin\Development\LumaEditor.exe

build\bin\Development\LumaTests.exe "[plugins]" --reporter compact
Filters: [plugins]
RNG seed: 730898286
All tests passed (639 assertions in 64 test cases)

build\bin\Development\LumaTests.exe --reporter compact
All tests passed (30382 assertions in 788 test cases)
```

## Code

| Path | Role |
|---|---|
| `Editor/Plugins/include/Luma/EditorPlugins/PluginsWindow.h` | Plugins window API |
| `Editor/Plugins/src/PluginsWindow.cpp` | Browser, dialogs, restart banner |
| `Editor/src/EditorScreen.cpp` | `RequestRelaunch` / `WantsRelaunch` |
| `Editor/src/main.cpp` | Detached relaunch with `--project` |
| `Tests/Editor/PluginsWindowTests.cpp` | Search, dialogs, read-only, restart |
| `docs/plugins/enablement.md`, `overview.md`, `lifecycle.md`, `authoring.md` | Window vs wizard |

## Remaining

Step 4: content mounting and config layering.
