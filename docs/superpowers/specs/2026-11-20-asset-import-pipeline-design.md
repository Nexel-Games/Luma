# Asset Import Pipeline & Import Window — Design

**Date:** 2026-11-20
**Status:** awaiting approval
**Built by:** engine agent (task T-0003), planned by lead + user
**Approach:** Unreal Interchange-style (translators → import scene → pipelines → factories)

## Intent

Importing content into a Luma project today copies raw files into `Assets/` — no
conversion, no options, no dialog. The user wants Unreal-style importing: pick files
(or drag them in), an **Import window** shows what each file contains with checkboxes
to choose what to bring in and per-type settings, and after import the assets appear
in the Content Browser **with real thumbnails** (3D render for models, the image for
textures, a waveform for sounds).

Formats, first round:
- **3D:** glTF (`.gltf` + `.bin`), GLB, FBX, OBJ (+ `.mtl`).
- **Audio:** WAV, MP3.
- **Images:** PNG, JPEG, TGA, BMP, PSD (flattened), HDR (Radiance), EXR (OpenEXR).

Unreal 5.8 Interchange (`Engine/Plugins/Interchange`) is the reference — translators,
generic pipelines, factory nodes, `SInterchangePipelineConfigurationDialog` — studied,
never copied.

## Goals

1. A reusable **import framework** (`Engine/Asset/Import`): format translators produce a
   neutral **ImportScene**; configurable **pipelines** turn it into a list of assets to
   create; **factories** write engine assets; metadata records source, settings and
   hashes for reimport.
2. The **Import window** in the editor (Slate), Unreal-style: content tree with
   checkboxes, settings panel per asset type, presets, conflict handling, Import /
   Import All / Cancel.
3. Entry points: Content Browser **Import** button, Content Browser context menu
   *Import to <folder>*, **drag & drop from Windows Explorer** onto the Content Browser,
   and **Reimport** on an imported asset.
4. **Static meshes** from all four 3D formats → `.lmesh`, with their materials (→ `.lmat`
   through the Material library API) and textures (embedded or referenced → imported
   images), correct axes/units, normals, tangents, sections, pivot.
5. **Textures** from all image formats, keeping colour-space detection
   (`TextureColorSpace`) and adding per-texture settings.
6. **Sounds** from WAV/MP3 → Sound assets with analysed info (duration, channels,
   sample rate) and per-sound settings.
7. **Thumbnails:** models (GPU render through the existing preview capture), textures
   (all new formats, HDR/EXR tone-mapped), sounds (waveform image).
8. **Audio preview:** a minimal `Luma::Audio` module (miniaudio device) so a Sound can be
   played/stopped from the Content Browser (play button on its tile, Unreal-style).
9. Background import with progress, cancel, and user-facing console messages.

## Non-goals (later tasks)

- Skeletal meshes, skins, animations, morph targets, cameras/lights, LODs, collision.
- "Import into level" (spawning actors per file node).
- The full audio engine (audio components, 3D attenuation, mixing, streaming playback
  in game).
- Texture block compression (BCn) and DDS/KTX.
- Material graph import (imported materials use today's `.lmat`; the lead's upcoming
  v4 migration converts them automatically).

## Anti-goals

- Copying files and calling it import; settings that do nothing; a dialog that only
  shows a file name.
- Silent failures: every skipped item / error reaches the console (`LUMA_USER_*`) and
  the import summary.
- Touching the lead's areas (material compiler, renderer, material bridge/preview
  internals). The importer **uses** public APIs (`Material::MaterialLibrary`,
  `MaterialPreview::CaptureMesh` via the existing thumbnail callback) without changing
  them.

## Approaches (decided)

- **A. Interchange-style pipeline** — *chosen*. Neutral import scene, pipelines,
  factories; one translator per format family; the dialog is driven by the pipelines'
  settings. Reuses the working pieces (Assimp mesh code, `.lmesh`, metadata GUIDs,
  colour-space detection) and replaces the rest.
- B. Per-type importers + per-type option windows (UE4 style) — rejected: no "what's
  in this file" tree; inherits weak scaffolding.
- C. Copy raw files, convert lazily — rejected: no window, no settings.

## Architecture

| Unit | Where | Responsibility |
|---|---|---|
| ImportScene | `Engine/Asset/Import/` | Neutral nodes: `MeshNode` (geometry payload handle, sections, material refs, transform), `MaterialNode` (PBR params + texture refs), `TextureNode` (source path or embedded bytes, hinted usage), `SoundNode` (source path, decoded info). Every node has a stable id, display name, source file, enabled flag. |
| Translators | `Engine/Asset/Import/Translators/` | `ModelTranslator` (Assimp: gltf/glb/fbx/obj), `ImageTranslator` (stb_image: png/jpg/tga/bmp/psd/hdr; tinyexr: exr), `AudioTranslator` (miniaudio decoders: wav/mp3). `CanTranslate(path)`, `Translate(path) → ImportScene`, payloads loaded lazily. |
| Pipelines | `Engine/Asset/Import/Pipelines/` | `MeshPipeline`, `MaterialPipeline`, `TexturePipeline`, `AudioPipeline`, `CommonPipeline` (target folder, naming, conflicts). Each owns a settings struct with reflection-style metadata (label, tooltip, range, category, visible-if) so the dialog builds its UI generically, and turns enabled nodes into **asset requests**. Settings (de)serialize to JSON for presets and metadata. |
| Factories | `Engine/Asset/Import/Factories/` | Create files: `.lmesh` (existing writer), `.lmat` (Material library API), images (copy original, or convert PSD/embedded → PNG; HDR/EXR kept), sounds (copy original). Each writes a `.meta` sidecar (GUID, source path + hash, importer version, settings). |
| ImportSession | `Engine/Asset/Import/` | Runs translate → pipeline → factories for a set of files on a worker thread; progress, cancellation, results, errors; main-thread completion callback; registry rescan + thumbnail invalidation. Replaces the `AssetImportManager` singleton (removed, with its unused code paths). |
| Audio | `Engine/Audio/` (new module) | miniaudio device lifetime, `PlaySound(path)` / `Stop()`, decode info, waveform peaks for thumbnails. No game-facing components yet. |
| Import window | `Editor/panels/ImportDialog/` (new panel module) | The modal Slate window (§ UI). |
| Content Browser | `Editor/panels/ContentBrowser/` | Import button / context menu / OS drop → session; Reimport; Sound tile play button; thumbnails for new types. |
| Thumbnails | `Engine/Asset` thumbnail renderers | New `SoundThumbnailRenderer` (waveform), texture renderer handles new formats; mesh thumbnails use the existing GPU capture callback. |

## Import window (UI)

Modal, resizable, Unreal Interchange layout, built with Luma Slate widgets:

```
+-- Import Content ----------------------------------------------------+
| Destination: /Game/Imports/Robot        [Preset: Default        v]  |
+---------------------------+------------------------------------------+
| Source                    | Settings                                 |
| [x] robot.glb             |  Common                                  |
|   [x] Meshes (3)          |    Name prefix / suffix, Offset T/R/S    |
|     [x] Body              |  Meshes                                  |
|     [x] Arm_L             |    Combine meshes, Scale, Axis, Normals  |
|   [x] Materials (2)       |    (import / compute, smoothing angle),  |
|   [x] Textures (4)        |    Tangents, Flip UVs, Pivot             |
| [x] engine_hum.wav        |  Materials: Create / Use existing / None |
| [x] T_Rock_D.png          |  Textures: sRGB (auto/on/off), Mips,     |
|                           |    Max size, Filter, Address mode        |
|                           |  Audio: Loop, Volume, Pitch              |
+---------------------------+------------------------------------------+
| ! 2 assets already exist: (o) Overwrite ( ) Skip ( ) Keep both        |
| [Reset to Defaults]                 [Import] [Import All] [Cancel]   |
+----------------------------------------------------------------------+
```

- The **Source** tree lists every file and every node in it, with type icons and counts;
  unchecking a group unchecks its children; a mesh's materials/textures stay checked
  when the mesh is (shared dependencies are imported once).
- Selecting a tree item scrolls the settings to its section; settings irrelevant to the
  selected files are hidden.
- **Presets:** *Default*, plus user-saved presets stored in the project
  (`Config/ImportPresets.json`); "last used" settings are remembered per type.
- **Conflicts:** destination assets that already exist are listed; choose Overwrite /
  Skip / Keep both (suffix `_1`). Reimport pre-selects Overwrite and the original
  settings.
- **Multiple files:** one window covers all files picked together; **Import All** applies
  the current settings to every file of the same type in the batch.
- Errors from translation (unreadable file, unsupported content) show inline in the
  tree in red with the reason; those items are unchecked and can't be enabled.

## Behaviour details

- **Units & axes:** the translator converts every source to Luma's coordinate system
  and metres (glTF metres/Y-up; FBX unit scale + axis metadata; OBJ assumed Y-up metres,
  overridable in settings). The builder verifies Luma's convention from the math/scene
  code and documents it.
- **Meshes:** triangulated; normals imported or computed (smoothing angle);
  MikkTSpace-equivalent tangents; one section per source material; bounds; pivot =
  source origin or bounds centre/bottom (setting); combine-meshes merges with
  transforms baked.
- **Materials:** source PBR (glTF metallic-roughness, FBX/OBJ legacy params) mapped to
  Luma `.lmat` inputs (base colour, metallic, roughness, normal, emissive, AO, opacity
  mask); textures referenced by virtual path; "Use existing" matches by name in the
  destination folder.
- **Textures:** embedded textures (GLB/FBX) extracted; referenced textures searched next
  to the source file and in common sub-folders; missing ones reported. Colour space:
  auto (existing `TextureColorSpace` rules + usage hint from the material slot), or
  forced.
- **Sounds:** decoded once to validate and measure; original file kept; info stored in
  metadata; preview plays from the file.
- **Metadata:** `<asset>.meta` JSON: GUID, asset type, source file (project-relative
  when inside the project, absolute otherwise), source hash + mtime, importer + version,
  settings. **Reimport** re-runs with stored settings; a changed source is detected and
  offered for reimport (Content Browser badge + context menu), never silently applied.
- **Naming:** default prefixes `SM_` (mesh), `M_` (material), `T_` (texture), `S_`
  (sound); unsafe characters sanitised; deterministic.
- **Threading:** translation and factory work on a worker; GPU work (thumbnails) on the
  main thread via existing callbacks; the editor stays responsive; progress bar in the
  Content Browser with Cancel.

## Testing

- Fixtures (small, committed under `Tests/Fixtures/Import/`): a two-mesh/two-material
  GLB with embedded textures, the same scene as glTF + `.bin` + external PNGs, an FBX
  (cm units, Z-up), an OBJ + MTL, 1 s mono WAV, 1 s stereo MP3, one image per format.
- Unit: each translator's ImportScene (node counts, names, material params, texture
  refs, units/axes converted, sound info); pipeline settings → asset requests (enable
  flags, combine, naming, conflicts); factories write the expected files + metadata;
  reimport uses stored settings; changed-source detection; presets round-trip.
- Dialog: driven with the Slate test harness — tree check/uncheck propagation, settings
  edits reach the pipeline, conflict choice, Import produces assets.
- Thumbnails: sound waveform image non-empty and deterministic; texture thumbnails for
  every format; mesh thumbnail via the editor capture (manual check).
- Manual: import each fixture into a scratch project through the window and by drag &
  drop; models render in the viewport with their materials; thumbnails correct; sounds
  play from the tile; 0 Vulkan validation errors.

## Builder self-check

- *Where do imported textures live at runtime?* As standard image files in the project
  (`.png` etc., with `.meta`), which the existing material/texture loading already reads
  by virtual path. HDR/EXR are importable and get thumbnails; using them as material
  textures is the lead's renderer follow-up (noted in the task).
- *Who renders mesh thumbnails?* The editor's existing GPU callback
  (`ThumbnailManager::SetRenderCallback` → `MaterialPreview::CaptureMesh`); the agent
  only makes sure imported `.lmesh` + materials are registered before thumbnails are
  requested.
- *Is the old `AssetImportManager` / `ImportDialog` / `BatchImportManager` kept?*
  Replaced by the new framework; code with no remaining users is deleted, and tests
  move to the new APIs.

## Calls I made (for review)

- Images stay as standard image files + `.meta` (Godot-like) instead of a binary
  `.ltex`, because the renderer already loads images by virtual path.
- Sounds keep the original WAV/MP3 + `.meta`; no conversion yet.
- A minimal Audio module is added only for preview playback and waveform thumbnails.
- Default name prefixes `SM_/M_/T_/S_`; conflicts default to *Keep both*.
- Presets stored per project in `Config/ImportPresets.json`.

## Left to the builder

Exact settings field list per pipeline beyond those named, tree widget implementation
details, worker count, waveform image style (within the theme), progress-bar placement.
