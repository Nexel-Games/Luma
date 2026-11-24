# Plugin descriptor

Implemented. A `.lplugin` file is JSON, parsed with `Luma::ParseJson` / `ParseDescriptor`. The path is `Plugins/<Name>/<Name>.lplugin`. The folder name, the file name (without extension), and the plugin name must be the same string.

**FileVersion 1** is the current document (`PLUGIN_ARCHITECTURE.md`). Identity is the folder/file name; there is no `Name` field inside the JSON. Unknown top-level keys are **kept** on rewrite (`WriteDescriptor`). Duplicate JSON keys are not visible to the parser: `Luma::SerialValue` keeps the last value.

**schema 1** documents (`id`, `displayName`, `kind`, …) still parse. The loader migrates them in memory (`PluginDescriptor::migratedFromSchema1`). Schema 1 unknown fields remain errors, matching the original parser. `ParseManifest` is that schema 1 parser; discovery now goes through `ParseDescriptor` and then `ToManifest` so the existing `PluginManager` still sees a `PluginManifest`.

`ResolvePlugins` takes valid and invalid descriptors plus the project's plugin entries and returns a status for every name: `Disabled`, `Invalid`, `Unavailable`, or `Active`. Active plugins are listed first in dependency order (ties by name). Required plugin dependencies must be Active; optional ones only affect order. Cycles are reported as `A -> B -> C -> A`. A project entry whose plugin is not installed is `Unavailable` ("not installed").

## FileVersion 1 fields

| Field | Required | Type | Rule |
|---|---|---|---|
| `FileVersion` | yes | integer | Must be `1`. |
| `Version` | yes | integer | Release counter, `>= 1`. |
| `VersionName` | yes | string | Display semantic version. |
| `FriendlyName` | yes | string | Non-empty after trimming. |
| `Description` | no | string | Default empty. |
| `Category` | yes | string | Non-empty. |
| `CreatedBy` | yes | string | Non-empty. |
| `CreatedByURL`, `DocsURL`, `SupportURL` | no | string | Default empty. |
| `EngineVersion` | yes | string | Semver range checked against `Luma::EngineVersion()`. |
| `EnabledByDefault` | no | bool | Default `false`. |
| `CanContainContent`, `IsBetaVersion`, `IsExperimentalVersion`, `Installed` | no | bool | Default `false`. `CanContainContent` mounts `Content/` at `/<Name>/` while the plugin is active. |
| `SupportedTargetPlatforms` | no | string array | If present, at least one of `Windows`, `Linux`, `macOS`. Omitted or empty means every platform. |
| `Modules` | no | object array | Default empty. |
| `Plugins` | no | object array | Plugin dependencies. Default empty. |

A module object: `Name` (required), `Type` (`Runtime`, `Developer`, `Editor`, `UncookedOnly`), `LoadingPhase` (default `Default`), optional `PlatformAllowList`.

A plugin-dependency object: `Name`, `Version` (range), optional `Enabled` (default true), `Optional` (default false). `Enabled: true` and `Optional: false` means the dependency must be Active first.

## Schema 1 fields (migrated)

| Field | Required | Type | Rule |
|---|---|---|---|
| `schema` | yes | integer | Must be `1`. |
| `id` | yes | string | `[A-Za-z0-9.]+`, no leading, trailing, or doubled dot. Not trimmed, so a space is an error. Example: `Luma.Physics.PhysX`. |
| `displayName` | yes | string | Non-empty after trimming. |
| `description` | no | string | Default empty. |
| `category` | yes | string | Non-empty. The settings UI groups `Physics`, `Simulation`, `Tools`, `Editor`, and `Other`. Any other non-empty category is accepted. |
| `author` | yes | string | Non-empty. |
| `url` | no | string | Default empty. Not fetched. |
| `version` | yes | string | Semantic version (`MAJOR.MINOR.PATCH` with optional pre-release). Build metadata is accepted and ignored, matching `Luma::SemVer`. |
| `engineVersionRange` | yes | string | Range below, checked against `Luma::EngineVersion()` at discovery. |
| `kind` | yes | string | `Runtime`, `Editor`, or `RuntimeAndEditor`. |
| `platforms` | yes | string array | At least one of `Windows`, `Linux`, `macOS`. |
| `dependencies` | no | object array | Default empty. |
| `capabilities` | no | string array | Declared extension points (`components`, `panels`, ...). Empty strings are errors. |
| `enabledByDefault` | no | bool | Default `false`. On for a project unless the id is in `.luma` `"plugins"."disabled"`. |

A dependency object:

| Field | Required | Type | Rule |
|---|---|---|---|
| `id` | yes | string | Same id rule as the plugin id. |
| `version` | yes | string | A version range, not necessarily one version. |
| `optional` | no | bool | Default `false`. |

`ParseManifest` records every problem it can see (`ManifestError::field` and `message`) and returns nullopt. It does not call plugin code.

## Version ranges

`Luma::Plugins::SatisfiesRange`. Whitespace separates AND constraints. `||` is rejected.

| Form | Meaning |
|---|---|
| `1.2.3` | Equal to that version. |
| `=1.2.3`, `==1.2.3` | Equal to. |
| `>=1.2.3`, `>1.2.3`, `<=1.2.3`, `<1.2.3` | Numeric comparison. A space after the operator is allowed (`>= 1.2.3`). |
| `^1.2.3` | `>=1.2.3 <2.0.0`. |
| `^0.2.3` | `>=0.2.3 <0.3.0`. |
| `^0.0.3` | `>=0.0.3 <0.0.4`. |
| `~1.2.3` | `>=1.2.3 <1.3.0`. |
| `>=1.2.0 <2.0.0` | Both constraints. |

Every bound must be a full semantic version (`1.2` is invalid).

A pre-release version satisfies a range only when at least one constraint names a pre-release of the same major.minor.patch. `1.2.4-alpha` does not satisfy `>=1.2.3 <2.0.0`. `1.2.3-beta` does satisfy `>=1.2.3-alpha <1.3.0`.

On a malformed range, `SatisfiesRange` returns false and, if an error string is passed, fills it. On a well-formed range the version does not satisfy, it returns false and clears that string.

## Discovery diagnostics

`DiscoverPlugins` in `Luma/Plugins/PluginDiscovery.h`.

| Situation | Result |
|---|---|
| Valid manifest, engine range matches, platform listed | Plugin entry, `engineCompatible` and `platformCompatible` true. |
| Engine range does not include `EngineVersion()` | Plugin entry kept. `engineCompatible` false. Diagnostic names the id, the required range, and this engine version. |
| `platforms` does not include `HostPlatform()` | Plugin entry kept. `platformCompatible` false. Diagnostic names the host and the allowed list. |
| Duplicate id in one or both roots | No entry. Error names the id and every manifest path. |
| Folder name, file name, or `id` disagree | No entry. Error says they must match. |
| Manifest path exists but cannot be read (including a directory where the file should be) | No entry. Error says the path could not be read and why. |
| `Something.lplugin` sitting directly in the root | No entry. Error says the layout is `Plugins/<Id>/<Id>.lplugin`. |
| Root path does not exist | Skipped. Not an error. |
| Compiled factory registered for the id | `compiledIn` true. The factory is not called. |

Errors are also written to the `Plugins` log category so a bad manifest is not silent.

## Example

```json
{
  "schema": 1,
  "id": "Luma.Sample.Runtime",
  "displayName": "Sample Runtime",
  "description": "Reference runtime plugin.",
  "category": "Tools",
  "author": "Luma",
  "version": "1.0.0",
  "engineVersionRange": ">=0.1.0 <0.2.0",
  "kind": "Runtime",
  "platforms": ["Windows", "Linux", "macOS"],
  "dependencies": [],
  "capabilities": ["components"],
  "enabledByDefault": false
}
```

File: `Plugins/Luma.Sample.Runtime/Luma.Sample.Runtime.lplugin`.
