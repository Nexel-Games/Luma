# Plugin packaging

Status of this document matches the code on `agent/T-0006-plugin-framework`.

## Build-level split (implemented)

| Target | Runtime modules | Editor / UncookedOnly modules | Developer modules |
|---|---|---|---|
| `LumaEngine` (game) | Static, whole-archived | **Never linked** | Not linked in this tree |
| `LumaEditor` | `LumaEditor-<Module>.dll` when `LUMA_MODULAR=ON` | Same, Editor phase | — |
| `LumaTests` | Static Runtime + Editor (so Game skip can be tested) | Static | — |

`luma_add_module` records `LUMA_MODULE_TYPE`. Configure fails if a `.lplugin` module has no matching CMake target, if Type disagrees with `luma_add_module`, or if `LumaEngine` would link an Editor/UncookedOnly target. The dump is `build/generated/LumaEngineLinkLibraries.txt`.

`PlanGamePackage` lists what a cook **would** copy: Runtime modules (static), `Content/`, `Config/`. It excludes Editor/Developer/UncookedOnly modules, `Source/`, `Resources/Icon128.png`, and `Binaries/`.

## Cook / ship (BLOCKED)

There is no packaging pipeline. `GamePackagePlan.blocked` is always true. Plugin `Content/` and `Config/` are not copied into a shipped game. Do not treat `PlanGamePackage` as a cook.
