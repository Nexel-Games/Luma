# P3 — Project persistence and Project Settings → Plugins

- Phase: P3
- Branch: `agent/T-0006-plugin-framework`
- Date: 2026-11-22
- Status: **COMPLETE**

Enablement is stored in the `.luma` `"plugins"` section (schema 1). Project Settings → Plugins plans and commits that section. Running plugins are not unloaded; a restart banner is shown until the running set matches the file.

## Purpose

Persist per-project plugin enablement, keep unknown ids, and expose enable/disable in Project Settings with confirmation, diagnostics, and a read-only project file path.

## Behavior

- **Implemented:** schema 1 `enabled` / `disabled` / `enabledByDefault`; unknown ids as `NotInstalled`; extra keys on `"plugins"` kept; `PlanEnable` pulls required deps only (not optional, not unrelated); `PlanDisable` lists enabled required dependents and open-scene components (including unresolved payloads); `Commit` writes `.luma` and fails on a read-only file; Plugins page (search, category groups, badge, checkbox, details, inline confirm because a second `BeginModal` would replace Project Settings); editor `PluginManager` boot; sandbox starts `enabledByDefault` engine plugins; status-bar restart banner.
- **Planned:** sample plugin rows (P4); `luma_add_plugin` compiled-out messaging (P5).
- **Unsupported:** live unload, DLL loading.
- **Blocked:** package walkthrough `03_PLUGIN_PROJECT_SETTINGS_UI.md` is not in this tree. The tests below are the recorded acceptance for persistence and plans. The Plugins page is wired; with no sample plugins yet the list is empty unless a project or `Plugins/` root has manifests.

## API names vs T-0006

`ApplyProject` still takes `const Project*` so tests can pass `nullptr` (enable every discovered plugin). `ChangePlan` includes `targetId` and `enable` so `Commit` knows what to write. `PluginState::NotInstalled` is the "not installed" badge. `PluginStatus::restartRequired` is derived: desired enablement vs `Started`.

## Tests

`Tests/Plugins/PersistenceTests.cpp`: project round trip; unknown ids kept; `PlanEnable` required-only; `PlanDisable` dependents and components; read-only file error.

```
build\bin\Development\LumaTests.exe "[plugins]" --reporter compact
All tests passed (402 assertions in 33 test cases)

build\bin\Development\LumaTests.exe --reporter compact
All tests passed (30145 assertions in 757 test cases)
```

Development build of `LumaTests`, `LumaEditor`, and `LumaEngine` succeeded after these changes.

## Manual

Package doc `03_PLUGIN_PROJECT_SETTINGS_UI.md` is not present. Recorded instead:

1. Persistence and plans: covered by `PersistenceTests` (output above).
2. UI: Edit → Project Settings → Plugins is registered through `SettingsPageRegistry` (`Plugins.Installed`). Checkbox → inline Confirm/Cancel → `Commit`. Read-only `.luma` shows the reason and does not toggle. Restart copy is drawn in the editor status bar when `PluginManager::RestartRequired()`.
3. Sample plugins are P4; this phase does not invent a fake plugin list.

## Docs

`docs/plugins/enablement.md`, plus updates to `overview.md` and `manifest.md`.
