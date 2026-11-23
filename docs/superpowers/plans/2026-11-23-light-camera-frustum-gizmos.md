# Light & Camera Frustum Gizmos Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Draw a wireframe view-frustum for the selected camera and an influence gizmo (cone/sphere/arrow/capsule) for the selected light in the editor viewport.

**Architecture:** Add two primitives — `Cone()` and `Frustum()` — to the existing immediate-mode `DebugDraw` system, matching its Box/Sphere/Capsule style. In `EditorScreen::BuildSceneView`, where the selected-object overlay is already assembled, read the selected entity's `CameraComponent`/`LightComponent` and emit the matching overlay shapes from the real component data.

**Tech Stack:** C++20, MSVC, Luma `DebugDraw` (RHI), Luma `Math` (column-major `Mat4`, `Inverse`), Catch2.

**Spec:** `docs/superpowers/specs/2026-11-23-light-camera-frustum-gizmos-design.md`

## Global Constraints

- Gizmos are **overlay** (`DebugDraw::Mode::Overlay`) — always on top, world space.
- Shapes come from component data only; no new tunables.
- `Math::Mat4` is **column-major**: element (row `r`, col `c`) = `m[c*4 + r]`.
- Editor-only; no change to shipped game rendering.

## Review Focus

- **Zero-length spot range / zero cone angle** → `Cone` must not divide by zero or emit NaNs; degenerate cone draws nothing or a point. (Task 1)
- **Orthographic camera** → `Frustum` via inverse view-proj must produce a box, not a skewed perspective shape. (Task 2)
- **Singular projection (near==far or fov 0)** → `Inverse` returns identity; `Frustum` must still produce finite verts, not garbage. (Task 2)
- **Selected entity with a Transform but no Camera/Light, or a Light whose type has no shape** → draws nothing extra, no crash. (Task 3)
- **Non-uniform entity scale on a light** → cone/sphere orientation follows the transform's rotation/translation, radius from component range (not visually warped by scale beyond what the transform implies). (Task 3)

---

### Task 1: `Cone()` primitive in `DebugDraw`

**Files:**
- Modify: `Engine/Rendering/RHI/include/Luma/RHI/DebugDraw.h`
- Modify: `Engine/Rendering/RHI/src/DebugDraw.cpp`
- Test: `Tests/Rendering/DebugDrawTests.cpp`

**Interfaces:**
- Produces: `void DebugDraw::Cone(const Math::Vec3& apex, const Math::Vec3& dir, f32 length, f32 halfAngleDeg, const Math::Vec3& color, Mode mode = Mode::DepthTested, u32 segments = 24);`
  Draws a base ring of radius `length * tan(halfAngle)` centered at `apex + normalize(dir)*length`, plus 4 side lines apex→ring at 0/90/180/270°.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("DebugDraw Cone emits a base ring plus four side lines", "[debugdraw]") {
    Luma::DebugDraw d;
    d.Cone({0,0,0}, {0,0,-1}, 2.0f, 30.0f, {1,1,0}, Luma::DebugDraw::Mode::Overlay, 24);
    const auto& v = d.Lines(Luma::DebugDraw::Mode::Overlay);
    // ring: 24 segments * 2 verts, + 4 side lines * 2 verts
    REQUIRE(v.size() == (24 + 4) * 2);
    // Degenerate cone (zero length) must not emit NaNs.
    Luma::DebugDraw z;
    z.Cone({0,0,0}, {0,0,-1}, 0.0f, 0.0f, {1,1,1}, Luma::DebugDraw::Mode::Overlay, 24);
    for (const auto& lv : z.Lines(Luma::DebugDraw::Mode::Overlay))
        REQUIRE(std::isfinite(lv.position.x));
}
```

- [ ] **Step 2: Run the test, verify it fails** (`Cone` undeclared).
  Build `LumaTests`, run `LumaTests.exe "[debugdraw]"`. Expected: compile/fail.

- [ ] **Step 3: Implement `Cone` in `DebugDraw.cpp`** (declare in `.h`). Reuse the file's `Basis()` for the ring plane, `Normalize`/`std::tan(Radians(halfAngleDeg))`. Guard `length <= 0` by still emitting the apex-centered ring of radius 0 (finite). Match the `Sphere`/`Capsule` ring style.

- [ ] **Step 4: Run the test, verify it passes.**

- [ ] **Step 5: Commit** (`feat(debugdraw): add Cone wireframe primitive`).

---

### Task 2: `Frustum()` primitive in `DebugDraw`

**Files:**
- Modify: `Engine/Rendering/RHI/include/Luma/RHI/DebugDraw.h`
- Modify: `Engine/Rendering/RHI/src/DebugDraw.cpp`
- Test: `Tests/Rendering/DebugDrawTests.cpp`

**Interfaces:**
- Consumes: `Math::Inverse`, `Math::Mat4` (column-major).
- Produces: `void DebugDraw::Frustum(const Math::Mat4& invViewProj, const Math::Vec3& color, Mode mode = Mode::DepthTested);`
  Unprojects the 8 clip corners `(x,y,z)` with `x,y ∈ {-1,+1}`, `z ∈ {0,1}` through `invViewProj`, divides by `w`, and draws the near rect (z=0), far rect (z=1), and 4 connecting edges — 12 edges total.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("DebugDraw Frustum of an identity clip volume is the unit NDC cube", "[debugdraw]") {
    Luma::DebugDraw d;
    d.Frustum(Luma::Math::Mat4::Identity(), {1,1,1}, Luma::DebugDraw::Mode::Overlay);
    const auto& v = d.Lines(Luma::DebugDraw::Mode::Overlay);
    REQUIRE(v.size() == 12 * 2);            // 12 edges
    // With identity invViewProj, corners are the clip cube: x,y in [-1,1], z in [0,1].
    for (const auto& lv : v) {
        REQUIRE(std::isfinite(lv.position.x));
        REQUIRE(lv.position.x >= -1.001f); REQUIRE(lv.position.x <= 1.001f);
        REQUIRE(lv.position.z >= -0.001f); REQUIRE(lv.position.z <= 1.001f);
    }
}
```

- [ ] **Step 2: Run the test, verify it fails** (`Frustum` undeclared).

- [ ] **Step 3: Implement `Frustum` in `DebugDraw.cpp`.** Build the 8 corners; for each, compute `W = m[3]*x + m[7]*y + m[11]*z + m[15]` and the transformed x/y/z (column-major), divide by `W` when `|W| > 1e-6` (else leave as-is). Index corners so near/far rects and the 4 side edges are drawn (reuse the one-bit-differs edge idea or an explicit edge table).

- [ ] **Step 4: Run the test, verify it passes.**

- [ ] **Step 5: Commit** (`feat(debugdraw): add Frustum wireframe primitive`).

---

### Task 3: Draw gizmos for the selected camera / light in `EditorScreen`

**Files:**
- Modify: `Editor/LumaEditor/EditorScreen.cpp` (selected-overlay block, ~L1303–1310, inside `BuildSceneView`)

**Interfaces:**
- Consumes: `DebugDraw::Cone`, `DebugDraw::Frustum`, `DebugDraw::Sphere`, `DebugDraw::Capsule`, `DebugDraw::Line` (via `DebugDrawer()`), `CameraComponent`, `LightComponent`, `TransformComponent`.
- Produces: no new public API; adds overlay lines to the frame via `DebugDrawer()` before `DebugDrawer().Lines(Overlay)` is read at L1311.

- [ ] **Step 1: Implement the selected-gizmo emission.** In the existing `if (m_selected valid && has TransformComponent)` block, after the transform-gizmo lines, add: compute `world = tf.Matrix()`, `pos = tf.position`, `fwd = normalize(-world.col2)` (same convention as L1168).
  - If `all_of<CameraComponent>`: `cam`; `aspect` = editor viewport aspect (reuse the aspect already used for `scene` / `m_view`; builder's call per spec); `invVP = world * Inverse(cam.ProjectionMatrix(aspect))`; `DebugDrawer().Frustum(invVP, kCameraWire, Overlay)`.
  - If `all_of<LightComponent>`: switch on `lc.type`:
    - `Spot`: `Cone(pos, fwd, lc.range, lc.outerAngleDeg, lc.color, Overlay)` and `Cone(pos, fwd, lc.range, lc.innerAngleDeg, lc.color, Overlay)`.
    - `Point`: `Sphere(pos, lc.attenuationRadius, lc.color, Overlay)`.
    - `Directional`: a short arrow — `Line(pos, pos+fwd*k, lc.color, Overlay)` plus two short head lines.
    - `Tube`: `Capsule(pos - right*lc.length*0.5, pos + right*lc.length*0.5, lc.attenuationRadius, lc.color, Overlay)` where `right` is the transform's local X.
  - Colors: lights use `lc.color`; camera uses a neutral `kCameraWire` constant (e.g. `{0.8,0.8,0.85}`).
  - Guard: entity may have a Transform but neither component → emit nothing.

- [ ] **Step 2: Build `LumaEditor`.** Run the project build (`Scripts/_env.bat cmake --build build --config Debug --target LumaEditor`). Expected: compiles and links.

- [ ] **Step 3: Verify in the running editor.** Launch `LumaEditor.exe`, select a camera (frustum shows), a spot light (two cones), a point light (sphere), a directional light (arrow). Expected: each gizmo appears only while selected and tracks its transform. (Manual — `EditorScreen` has no unit harness.)

- [ ] **Step 4: Commit** (`feat(editor): frustum/cone gizmos for selected camera and lights`).

---

## Notes

- Tasks 1 and 2 are independent (both touch `DebugDraw`, non-overlapping functions) and could be built in parallel, but are small; Task 3 consumes both.
- No change to `DebugDrawer().Clear()` lifecycle — shapes are per-frame, emitted during `BuildSceneView` and read in the same call.
