# Architecture step 6 — New Plugin wizard and descriptor editor

- Source: `PLUGIN_ARCHITECTURE.md` delivery step 6
- Branch: `agent/T-0006-plugin-framework`
- Date: 2026-11-23
- Status: **COMPLETE** (delivery step 6 of 7)

**Add** generates a plugin from `Engine/Plugins/Templates/`. **Edit...** saves a project plugin's `.lplugin` without dropping unknown fields. The editor does not compile C++. Packaging stays **BLOCKED**.

## Purpose

Stop faking Add/Edit. The wizard writes a real folder; the descriptor editor writes a real FileVersion 1 document.

## Behavior

- **Implemented:** Templates Blank, Content Only, Editor Toolbar Button, Editor Standalone Window, Runtime Component. Name, author, description, category, location (project or engine `Plugins/`). Generates descriptor, CMake, sources, `Resources/Icon128.png`. Descriptor editor: FriendlyName, Description, Category, CreatedBy and URLs, VersionName, EngineVersion, Beta/Experimental, CanContainContent, optional icon PNG. Unknown keys kept. Each template generates, builds (CMake fixtures), loads, and shuts down.
- **Planned:** Packaging separation and stabilization (step 7).
- **Unsupported:** compiling C++ inside the editor, hot reload.
- **Blocked:** packaging of plugin `Content/` and `Config/`.

## Tests

```
cmake --build build --config Development --target LumaTests LumaEditor LumaEngine
Linking CXX shared library WizardFixtures\WizardBlank\Binaries\Win64\Development\LumaEditor-WizardBlank.dll
Linking CXX executable bin\Development\LumaTests.exe
Linking CXX executable bin\Development\LumaEditor.exe

build\bin\Development\LumaTests.exe "[wizard]" --reporter compact
All tests passed (93 assertions in 8 test cases)

build\bin\Development\LumaTests.exe "[plugins]" --reporter compact
All tests passed (788 assertions in 81 test cases)

build\bin\Development\LumaTests.exe --reporter compact
All tests passed (30539 assertions in 807 test cases)
```

## Code

| Path | Role |
|---|---|
| `Engine/Plugins/src/PluginWizard.cpp` | Generate from templates |
| `Engine/Plugins/Templates/` | Five templates |
| `Editor/Plugins/src/PluginsWindow.cpp` | Wizard and descriptor editor modals |
| `Tests/Plugins/WizardTests.cpp` | Generate, save, load, shut down |
| `docs/plugins/authoring.md` | Wizard / Edit |

## Remaining

Step 7: build/packaging separation, stabilization, docs. Packaging stays **BLOCKED**.
