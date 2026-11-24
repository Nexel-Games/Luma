# Architecture step 5 — Modular editor DLLs

- Source: `PLUGIN_ARCHITECTURE.md` delivery step 5
- Branch: `agent/T-0006-plugin-framework`
- Date: 2026-11-23
- Status: **COMPLETE** (delivery step 5 of 7)

`LUMA_MODULAR=ON` (default): engine and editor libraries are SHARED (`bin/<Config>/LumaCore.dll`, …). Editor plugin modules are `Plugins/<Name>/Binaries/Win64/<Config>/LumaEditor-<Module>.dll`, loaded with `DllModuleBackend`. `LumaEngine` still whole-archives Runtime modules. Packaging stays **BLOCKED**.

## Purpose

Give the editor a real module DLL boundary with one process-wide copy of registries, an API version check, and EnTT type identity via `type_hash`. The game stays statically linked.

## Behavior

- **Implemented:** `DllModuleBackend` (`LoadLibraryExW` with `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`). `kModuleApiVersion` (1) checked before `LumaCreateModule`. `luma_add_module` builds static + `LumaEditor-<Module>.dll`. Sample Spinner Runtime/Editor DLLs for the editor; tests and `LumaEngine` keep static Sample Spinner. `WINDOWS_EXPORT_ALL_SYMBOLS` + `/MD`.
- **Planned:** New Plugin wizard and descriptor editor (step 6).
- **Unsupported:** hot reload, sandboxing.
- **Blocked:** packaging of plugin `Content/` and `Config/`.

## Tests

```
cmake --build build --config Development --target LumaTests LumaEditor LumaEngine SampleSpinnerDll SampleSpinnerEditorDll DllProbeDll DllProbeBadApiDll
Linking CXX shared library Plugins\SampleSpinner\Binaries\Win64\Development\LumaEditor-SampleSpinner.dll
Linking CXX shared library Plugins\SampleSpinner\Binaries\Win64\Development\LumaEditor-SampleSpinnerEditor.dll
Linking CXX executable bin\Development\LumaTests.exe
Linking CXX executable bin\Development\LumaEngine.exe
Linking CXX executable bin\Development\LumaEditor.exe

build\bin\Development\LumaTests.exe "[plugins]" --reporter compact
Filters: [plugins]
RNG seed: 2220107693
All tests passed (695 assertions in 73 test cases)

build\bin\Development\LumaTests.exe --reporter compact
All tests passed (30446 assertions in 799 test cases)
```

## Code

| Path | Role |
|---|---|
| `cmake/LumaModular.cmake` | `LUMA_MODULAR`, `LUMA_LIBRARY_TYPE` SHARED |
| `cmake/LumaPlugins.cmake` | `luma_add_plugin` / `luma_add_module` DLL output |
| `Engine/Plugins/src/DllModule.cpp` | Load, ABI check, unload |
| `Editor/LumaEditor/EditorScreen.h` | `ModuleManager(DllModuleBackend::Global())` |
| `Tests/Plugins/DllModuleTests.cpp` | load, bad API, EnTT identity |
| `docs/plugins/binary-compatibility.md` | CRT, API version, EnTT |

## Remaining

Step 6: New Plugin wizard and descriptor editor. Add/Edit in the Plugins window must not fake a wizard. Packaging stays **BLOCKED**.
