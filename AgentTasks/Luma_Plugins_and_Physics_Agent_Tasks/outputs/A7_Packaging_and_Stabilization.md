# Architecture step 7 — Build/packaging separation and stabilization

- Source: `PLUGIN_ARCHITECTURE.md` delivery step 7
- Branch: `agent/T-0006-plugin-framework`
- Date: 2026-11-23
- Status: **COMPLETE** (delivery step 7 of 7)

Build-level split is in place: `LumaEngine` whole-archives Runtime modules only. Cook/package of plugin `Content/` and `Config/` stays **BLOCKED**.

## Purpose

Keep Editor modules out of the game binary, document the future cook, and prove start/stop does not leak registry state.

## Behavior

- **Implemented:** `LUMA_MODULE_TYPE` on each `luma_add_module`. Configure validates FileVersion 1, module names vs CMake targets, Type vs `luma_add_module`, and refuses Editor/UncookedOnly on `LumaEngine`. Link dump: `build/generated/LumaEngineLinkLibraries.txt`. `PlanGamePackage` lists Runtime / Content / Config as in-game and Source / Editor / Binaries as out; `blocked` is always true. 10× SampleSpinner and DllProbe start/stop. Missing `LumaModuleApiVersion` export is a load error. Docs: `packaging.md`, `troubleshooting.md`.
- **Planned:** a real cook that copies the in-game set.
- **Unsupported:** hot reload, sandboxing, compiling C++ in the editor.
- **Blocked:** packaging pipeline (Content/Config not shipped).

## Tests

```
cmake --build build --config Development --target LumaTests LumaEditor LumaEngine
Linking CXX executable bin\Development\LumaEngine.exe
Linking CXX executable bin\Development\LumaTests.exe
Linking CXX executable bin\Development\LumaEditor.exe

type build\generated\LumaEngineLinkLibraries.txt
SampleSpinner
(no SampleSpinnerEditor)

build\bin\Development\LumaTests.exe "[plugins]" --reporter compact
All tests passed (899 assertions in 86 test cases)

build\bin\Development\LumaTests.exe --reporter compact
All tests passed (30650 assertions in 812 test cases)
```

10× open/close is automated (`[plugins][stability]`). An interactive editor 10× session with Vulkan validation was not run in this step.

## Code

| Path | Role |
|---|---|
| `cmake/LumaPlugins.cmake` | Type properties, descriptor check, game-link assert |
| `Engine/Plugins/src/PluginPackaging.cpp` | `PlanGamePackage` |
| `Tests/Plugins/PackagingTests.cpp` | Plan, sandbox link dump, 10×, missing export |
| `docs/plugins/packaging.md` | Split vs cook |
| `docs/plugins/troubleshooting.md` | Messages and fixes |

## Remaining

T-0006 architecture steps 1–7 are in code on this branch. Packaging of a shipped game is still **BLOCKED**. PhysX (T-0007) waits until T-0006 is accepted. Not merged to main.
