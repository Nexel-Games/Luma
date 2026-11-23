# Camera Preview Inset — Design

Date: 2026-11-23
Size: project (short design)

## Intent / why

When a camera is selected, show a picture-in-picture inset in the viewport's
bottom-right that renders what that camera sees — Unreal's selected-camera
preview. Lets you frame a shot without piloting the camera.

## Goals

- While a `CameraComponent` entity is selected, a live inset in the viewport's
  bottom-right shows the scene rendered from that camera's view/projection.
- The inset disappears when no camera is selected.
- The preview is a clean game-like view: the scene's meshes, lights and sky,
  but no editor grid, gizmos or overlay lines.
- The inset shows the camera's name.

## Non-goals / anti-goals

- No click-to-pilot / "lock to camera" (separate feature).
- Not resizable or draggable by the user (fixed corner, fixed fraction).
- No second camera's preview when multiple are selected — only the active
  selection's camera.
- Not rendered in the shipped game.

## Constraints

- The main `VulkanDeferredRenderer` rebuilds all targets on any render-size
  change (`vkDeviceWaitIdle` + realloc in `SetViewportDimensions`), so the
  preview must render at the **same resolution** as the main view — never a
  smaller size on the same renderer each frame.
- Reuse the renderer's existing **snapshot** API (`SetSnapshotTarget` /
  `SnapshotTexture` / `ReleaseSnapshot`) — the same mechanism Material Editor
  previews use to keep a rendered view in a persistent texture.

## Approaches considered

1. **Second RenderScene on the main renderer, into a snapshot** (chosen).
   Render the camera's view at the current viewport resolution first, snapshot
   it, then render the main view (which leaves the live buffer showing the main
   view). Draw the snapshot scaled into the corner. No resize, no second
   renderer, no duplicated residency. Cost: one extra scene render while a
   camera is selected, gated to the frames the main view also renders.
2. **A dedicated small preview renderer** (like `MaterialPreview`). Rejected:
   it would need the whole active scene's mesh/material residency duplicated
   into a second renderer — large memory and sync cost for a thumbnail.
3. **Render the preview at a small size on the main renderer.** Rejected: the
   per-change `SetViewportDimensions` tears down and rebuilds all targets, so
   alternating sizes each frame thrashes the GPU.

## The how (decided)

### Render order (main loop, `main.cpp`, around L611–639)

Per frame, after `BuildDeferredSceneView` and computing `needsRender`:
1. `hasPreview = editor->BuildCameraPreviewView(previewScene, viewW, viewH, &camName)`.
2. If `hasPreview && needsRender`: `SetSnapshotTarget(kCameraPreviewKey)`,
   `RenderScene(previewScene)`, `SetSnapshotTarget(0)`. **Before** the main
   render, so the main render is last and the live viewport texture stays the
   main view; the snapshot holds the camera view.
3. Main render + `SetViewportTexture(GetViewportTextureHandle())` as today.
4. `hasPreview` → `editor->SetCameraPreview(SnapshotTexture(key), camName)`;
   else `editor->SetCameraPreview(0, "")` and, on the active→inactive edge,
   `ReleaseSnapshot(key)` (tracked by a loop-local `previewWasActive`).

`kCameraPreviewKey` is a fixed non-zero constant distinct from any material
preview key.

### `EditorScreen::BuildCameraPreviewView(out, w, h, &name)` → bool

- Return false unless `m_selected` is valid and has a `CameraComponent`.
- Copy the already-built `m_deferredSceneSource` (the frame's `SceneView`) into
  a member `m_cameraPreviewSceneSource`, then **disable grid and clear overlay
  lines** on the copy (shallow copy keeps the instance/light pointers, which
  point at EditorScreen members valid for the frame).
- Fill `out` from the camera entity: `world = tf.Matrix()`,
  `view = Inverse(world)`, `proj = cam.ProjectionMatrix((f32)w/h)`,
  `viewProjection = proj*view`, position/direction, width/height, fov, near/far,
  deltaTime; `out.sceneData = &m_cameraPreviewSceneSource`,
  `out.lightingParams = &m_cameraPreviewSceneSource.lighting`.
- `name` = the entity's `NameComponent` (or "Camera").

### Viewport inset (`ViewportPanel`)

- `SetCameraPreview(TextureHandle tex, std::string name)` stores `m_cameraPreview`
  + `m_cameraPreviewName`.
- In `Draw`, after the main image and toolbars, if `m_cameraPreview != 0`:
  draw an inset in the bottom-right — width = `round(body.w * 0.22)`, height =
  `width * body.h / body.w` (mirrors the viewport aspect, so the snapshot shows
  undistorted), 8px margin, a hairline border and a drop shadow, the image, and
  a small name label on a dim strip along its top.

## Left to the builder

- Exact inset fraction (0.22) and margin — tune to look right.
- Whether the preview also renders in non-realtime mode on camera-selection
  change: it rides `needsRender`, which already flips on any scene/selection
  change, so selecting a camera triggers one render. Good enough; no extra gate.
- Snapshot memory: the snapshot is at viewport resolution (can be large). Only
  one exists, freed on deselect; acceptable.
