# Plugin enablement

Implemented. Which plugins run is a **per-project** choice stored in the `.luma` file, not under `Config/`. Enabling or disabling a plugin **never unloads a running plugin**; the editor must be restarted before the running set matches the file.

## Project file (`schema` 1)

```json
"plugins": {
  "schema": 1,
  "enabled": [ "Luma.Feature" ],
  "disabled": [ "Luma.Optional" ]
}
```

- A plugin with `"enabledByDefault": true` is on unless its id is in `disabled`.
- A plugin listed in `enabled` is on (unless it is also in `disabled`, which wins).
- A plugin in neither list uses `enabledByDefault` (false if omitted).
- Unknown ids are kept on save and shown as **Not installed**. Extra keys on the `plugins` object are kept.

`PluginManager::ApplyProject` reads this section. `PlanEnable` / `PlanDisable` / `Commit` write it.

## Editor

**Edit → Plugins** (dockable Plugins window). Left: Built-In / Project category tree with counts. Top: search (name, description, author, module names). Tiles: FriendlyName, version, description, Created by, Beta/Experimental, Enabled checkbox, status (Active / Disabled / Unavailable / Failed / Not installed). Enabling or disabling confirms required dependencies, beta/experimental warnings, dependents, and scene components. A bottom banner plus the status bar offer **Restart Now** (relaunches this executable with `--project`, the same path as the project browser).

Project Settings no longer lists enable/disable. Per-plugin settings pages still appear under **Project Settings → Plugins → \<FriendlyName\>** while that plugin is active. Those pages load `Config/Default<Name>.json` then overlay the project's `Config/` file (`docs/plugins/content.md`).

Enable computes `PlanEnable`: required dependencies that are currently off are listed (`alsoEnable`). Optional dependencies are not pulled. Confirm, then `Commit` writes the project and shows a restart banner.

Disable computes `PlanDisable`: enabled plugins that require the target, and components on the open authoring scene that those plugins own. Confirm, then `Commit`. After restart, those components load as **"missing plugin — data kept"** (T-0005 unresolved payloads).

A read-only `.luma` file makes the page read-only. `Commit` fails with a message that names the file.

The runtime host (`LumaEngine`) discovers `<EngineRoot>/Plugins` and starts plugins that are `enabledByDefault`. It does not open a project yet.

**Add** generates a plugin under the project or engine `Plugins/` folder. **Edit...** saves the `.lplugin` for a project plugin without dropping unknown keys.
