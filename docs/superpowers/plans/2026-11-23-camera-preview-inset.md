# Camera Preview Inset Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** A picture-in-picture inset in the viewport's bottom-right showing the selected camera's view, active only while a camera is selected.

**Architecture:** Render the camera's view at the main viewport resolution into the renderer's snapshot texture (before the main render), then draw that snapshot scaled into the corner. Reuses the existing `VulkanDeferredRenderer` snapshot API; no second renderer.

**Tech Stack:** C++20, Luma `VulkanDeferredRenderer` (snapshot API), `Renderer2::DeferredSceneView`, `SceneView`, Slate.

**Spec:** `docs/superpowers/specs/2026-11-23-camera-preview-inset-design.md`

## Global Constraints

- Preview renders at the SAME resolution as the main view (resize tears down all targets).
- Preview render happens BEFORE the main render so the live viewport texture stays the main view.
- Editor/renderer glue has no unit harness here — tasks verify by build + manual, like the existing viewport gizmo wiring.

## Review Focus

- **No camera selected / selection cleared** → builder returns false, inset hidden, snapshot released once. (Task 1/2)
- **Selected entity is a camera with orthographic projection** → `cam.ProjectionMatrix` ortho path used; preview not distorted. (Task 1)
- **Camera selected but scene static (non-realtime)** → preview still renders on the selection-change frame (rides `needsRender`); snapshot persists after. (Task 2)
- **Viewport very small / zero-size** → inset math produces finite, on-screen rect, no overflow. (Task 3)

---

### Task 1: `BuildCameraPreviewView` + preview plumbing in `EditorScreen`

**Files:**
- Modify: `Editor/LumaEditor/EditorScreen.h` (new members + method decls)
- Modify: `Editor/LumaEditor/EditorScreen.cpp` (implement near `BuildDeferredSceneView`)

**Interfaces:**
- Produces:
  - `static constexpr u64 kCameraPreviewKey = 0xCA3E12A1;`
  - `bool EditorScreen::BuildCameraPreviewView(Renderer2::DeferredSceneView& out, u32 width, u32 height, std::string* name);`
    Returns false unless `m_selected` has a `CameraComponent`. Copies the frame's `m_deferredSceneSource` into `m_cameraPreviewSceneSource`, disables grid + clears overlay lines on the copy, fills `out` from the camera entity's transform/projection, points `out.sceneData`/`lightingParams` at the copy, sets `*name`.
  - `void EditorScreen::SetCameraPreview(TextureHandle tex, std::string name) { m_viewportPanel.SetCameraPreview(tex, std::move(name)); }`
- Consumes: `CameraComponent::ProjectionMatrix`, `Math::Inverse`, `NameComponent`.

- [ ] **Step 1: Implement the members and methods.** Add `SceneView m_cameraPreviewSceneSource;`. `BuildCameraPreviewView` must be called after `BuildDeferredSceneView` (relies on `m_deferredSceneSource`). View = `Inverse(tf.Matrix())`; aspect = `(f32)width/height`.
- [ ] **Step 2: Build `LumaEditor`.** Expected: compiles (ViewportPanel::SetCameraPreview added in Task 3 — build after Task 3, or stub first). Order: do Task 3 before building.
- [ ] **Step 3: Commit** (`feat(editor): camera preview scene-view builder`).

---

### Task 2: Render the preview snapshot in the main loop

**Files:**
- Modify: `Editor/LumaEditor/main.cpp` (viewport render block, ~L611–639)

**Interfaces:**
- Consumes: `EditorScreen::BuildCameraPreviewView`, `EditorScreen::SetCameraPreview`, `EditorScreen::kCameraPreviewKey`, renderer `SetSnapshotTarget`/`SnapshotTexture`/`ReleaseSnapshot`.

- [ ] **Step 1: Add the preview render.** After `needsRender` is computed and BEFORE `RenderScene(deferredScene)`: build the preview view; if `hasPreview && needsRender`, `SetSnapshotTarget(kCameraPreviewKey)`, `RenderScene(previewScene)`, `SetSnapshotTarget(0)`. After the main render + `SetViewportTexture`, call `SetCameraPreview(SnapshotTexture(key), name)` when `hasPreview`, else `SetCameraPreview(0, "")` and `ReleaseSnapshot` once on the active→inactive edge (loop-local `bool previewWasActive`).
- [ ] **Step 2: Build `LumaEditor`.** Expected: compiles + links.
- [ ] **Step 3: Commit** (`feat(editor): render selected-camera preview into a snapshot`).

---

### Task 3: Draw the inset in `ViewportPanel`

**Files:**
- Modify: `Editor/panels/Viewport/include/Luma/Editor/Panels/Viewport.h`
- Modify: `Editor/panels/Viewport/src/Viewport.cpp` (in `Draw`, after main image + toolbars, ~L559)

**Interfaces:**
- Produces: `void ViewportPanel::SetCameraPreview(Luma::TextureHandle tex, std::string name);` + members `m_cameraPreview`, `m_cameraPreviewName`.

- [ ] **Step 1: Implement SetCameraPreview + inset draw.** When `m_cameraPreview != 0`: `w = round(body.w*0.22)`, `h = w*body.h/body.w`, 8px margin bottom-right; drop shadow + hairline border; `ui.Image(m_cameraPreview, insetRect)`; a dim top strip with the name label. Guard tiny/zero `body`.
- [ ] **Step 2: Build `LumaEditor`** (links Tasks 1–3). Expected: compiles + links.
- [ ] **Step 3: Verify in the running editor.** Select a camera → inset appears bottom-right showing its view, with name; deselect → inset gone; move an object → preview updates. (Manual.)
- [ ] **Step 4: Commit** (`feat(editor): draw camera preview inset in the viewport`).

## Notes

- Build order: implement Tasks 1 and 3 (the API both ends need), then build; Task 2 wires them in the loop. Commit per task; a single build after Task 3 covers Task 1's compile.
