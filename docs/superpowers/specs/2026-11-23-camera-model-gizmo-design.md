# Editor Camera Model Gizmo — Design

Date: 2026-11-23
Size: project (short design)

## Intent / why

Cameras are invisible in the viewport today (only a frustum when selected). Like
Unreal's `SM_CineCam` proxy mesh, draw a real camera **model** at every camera's
transform so cameras are visible and recognizable in the editor. Shipped as a
real mesh asset, instanced editor-only.

## Goals

- A camera mesh asset (`.lmesh`) ships with the editor content.
- Every entity with a `CameraComponent` shows that mesh at its transform in the
  editor viewport, oriented so the lens points down the camera's forward (local
  -Z), always visible (not only when selected).
- Editor-only: the model does not appear in the shipped game, and is hidden
  during Play-in-Editor (like UE's `bCameraMeshHiddenInGame`).
- The selected camera's model gets the normal selection outline.
- Clicking the model selects the camera (works today via the proximity-sphere
  pick; optionally tightened to the mesh bounds).

## Non-goals / anti-goals

- Not a light model/billboard — that's a follow-up (same mechanism).
- Not an imported photoreal model and not an art-pipeline deliverable; a clean
  low-poly camera is enough to read as "a camera".
- The model is not a scene entity/serialized object — it's a per-frame editor
  visualization, owned by the editor, not saved in the scene.
- No new camera authoring UI.

## Constraints

- Luma's built-in primitives are only Cube/Plane/Sphere/Cylinder — no camera
  shape — so the model must be authored as a real mesh.
- `.lmesh` is a documented binary format with `LumaMeshIO::WriteMesh` /
  `ReadMesh` over `LumaMeshData` (verts + indices + submeshes).
- Editor view instances live in `EditorScreen::m_instances`, built by
  `MaterialSceneBridge::BuildInstances`; the renderer takes `SceneInstance`
  (model matrix, `MeshHandle`, section materials, `selected`).
- Picking is ray-based (`EditorScreen::PickEntity`): cameras already pick via a
  0.35-radius sphere at their origin.

## Approaches considered (geometry source)

The chosen path is "ship a real asset"; the open sub-decision is where the
geometry comes from:

1. **Procedurally author a low-poly camera and bake it to `SM_Camera.lmesh`**
   (chosen). A small committed generator builds the geometry (body box, lens
   barrel, viewfinder, top plate) into `LumaMeshData` and writes the `.lmesh`
   once; the baked asset is committed to editor content. No art-tool dependency,
   fully reproducible, lives in-repo.
2. **Model it in Blender (Blender MCP) and export.** Nicer geometry, but depends
   on a live Blender session and an export/convert step; heavier and not
   reproducible from the repo alone. Possible later upgrade — the consumer side
   is identical (it just loads a different `.lmesh`).
3. **Find/import an existing camera model.** Rejected: an external art asset with
   unknown licensing, not reproducible in-repo.

## The how (decided)

### 1. Author + ship the asset

- A generator (a committed tool/test, e.g. `Tools/GenerateCameraMesh` or a
  gated test) builds `LumaMeshData` for a recognizable low-poly camera —
  oriented lens toward -Z, roughly unit-scale (~0.4 m body) — and
  `LumaMeshIO::WriteMesh`es it to `Content/Editor/Meshes/SM_Camera.lmesh`.
- The baked `.lmesh` is committed. The generator stays in-repo so the asset can
  be regenerated/tweaked.

### 2. Load it in the editor

- At editor init (where other editor meshes/icons load), `ReadMesh` the
  `.lmesh`, convert `LumaMeshData` → `MeshData`, `UploadMesh` to the renderer →
  `m_cameraModelMesh` (`MeshHandle`). Use the editor's existing default material
  for its section.

### 3. Instance it per camera (editor-only)

- In the scene-view build, after `BuildInstances`, for each `CameraComponent`
  entity not `EditorHiddenComponent`, append a `SceneInstance`: `model` =
  `tf.Matrix()`, `mesh` = `m_cameraModelMesh`, section material = editor default,
  `selected` = `(e == m_selected)`.
- Skip while playing/simulating (PIE) so the model is hidden in game.

### 4. Picking (optional tighten)

- Keep the existing sphere pick (already selects cameras). Optionally, in
  `PickEntity`, test camera entities against the camera mesh's local bounds
  instead of the 0.35 sphere so the clickable area matches the visible model.

## Left to the builder

- Exact camera silhouette/proportions and poly budget (keep it low, readable).
- Which "editor default material" to use for the model (reuse whatever
  unassigned mesh instances already get).
- Whether to also tighten picking to mesh bounds (nice-to-have; include if cheap).
- Mesh scale/offset so the model sits sensibly around the camera origin with the
  lens pointing forward.
