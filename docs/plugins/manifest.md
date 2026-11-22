# Plugin manifest

Implemented. A manifest is JSON, parsed with `Luma::ParseJson`. The file path is `Plugins/<Id>/<Id>.lplugin`. The folder name, the file name, and the `id` field must be the same string.

Unknown fields are errors. Duplicate JSON keys are not visible to the parser: `Luma::SerialValue` keeps the last value, which is existing JSON behavior and is not changed here.

## Fields

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
| `enabledByDefault` | no | bool | Default `false`. Not applied until project enablement exists. |

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
