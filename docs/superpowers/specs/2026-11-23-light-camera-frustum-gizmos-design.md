# Light & Camera Frustum Gizmos — Design

Date: 2026-11-23
Size: small change (short design)

## Intent / why

When you select a camera or a light in the viewport you currently see only the
transform gizmo — nothing shows what the camera *sees* or where the light
*reaches*. This adds viewport wireframe gizmos so the selected object's view
frustum (camera) or area of influence (light) is visible at a glance, the way
Unreal shows a selected camera's frustum and a selected spot light's cones.

## Goals

- Selecting a **camera** draws its view **frustum** (perspective) or view **box**
  (orthographic), using the component's fov / ortho height / near / far.
- Selecting a **light** draws its influence by type:
  - **Spot** → inner + outer **cones** out to the light's range.
  - **Point** → wireframe **sphere** at the attenuation radius.
  - **Directional** → a short **arrow** showing aim direction.
  - **Tube** → a **capsule** along the emitter length at the attenuation radius.
- Gizmos are drawn as **overlay** lines (always on top, not occluded by scene
  geometry) and only for the **currently selected** object; they disappear on
  deselect.

## Non-goals / anti-goals

- No gizmos for non-selected objects (decided: selected-only).
- Not interactive (you can't drag the frustum to change fov/range — that stays
  in the Inspector).
- No new editor billboard icons for lights/cameras (separate task).
- Not drawn in the shipped game, only the editor viewport.

## Constraints

- Reuse the existing immediate-mode `DebugDraw` (lines, world space, overlay vs
  depth-tested). It has Box / Sphere / Capsule but no cone or frustum.
- Gizmo geometry must come from the real component data so it matches what the
  renderer actually uses.

## Approaches considered

1. **Extend `DebugDraw` with `Cone()` + `Frustum()` primitives, call from
   `EditorScreen`** (chosen). Consistent with how Box/Sphere/Capsule already
   work; the new primitives are reusable (PIE systems, other tools); the
   drawing call sits in the existing selected-overlay block.
2. **Build the line lists inline in `EditorScreen`** (like the transform
   gizmo). Rejected: scatters geometry math into the editor, not reusable,
   grows an already large file.
3. **A separate gizmo/visualizer module.** Rejected: over-engineering for two
   shapes that belong next to the existing debug primitives.

## The how (decided)

### New `DebugDraw` primitives (`DebugDraw.h/.cpp`)

- `Cone(apex, dir, length, halfAngleDeg, color, mode, segments=24)`
  Base circle of radius `length * tan(halfAngle)` centered at
  `apex + dir*length`, plus 4 side lines apex→ring (at 0/90/180/270°).
- `Frustum(const Math::Mat4& invViewProj, color, mode)`
  Unprojects the 8 clip-space corners (±1 x/y, 0..1 z) through `invViewProj`
  to world space and draws the near rect, far rect, and 4 connecting edges.
  Using the inverse view-projection handles **perspective and orthographic**
  cameras through one path, reusing `CameraComponent::ProjectionMatrix`.

Both get unit-test coverage in `DebugDrawTests.cpp` (vertex counts, and a
spot-check that `Frustum` corners land where expected for a known matrix).

### Drawing call (`EditorScreen::BuildSceneView`, selected-overlay block ~L1303)

When `m_selected` is valid and has a `TransformComponent`:
- If it has a `CameraComponent`: compute
  `invViewProj = inverse(proj(aspect) * inverse(worldMatrix))` and call
  `Frustum(...)`. **Aspect**: use the current viewport aspect as the preview
  aspect (builder's call — see below).
- If it has a `LightComponent`: switch on type and call the matching primitive
  (`Cone` ×2 for spot inner/outer, `Sphere` for point, `Capsule` for tube, a
  few `Line`s for the directional arrow), positioned/oriented from the entity
  transform, colored by the light's own `color` (camera frustum uses a neutral
  wire color).

This runs alongside the existing transform-gizmo overlay, so both show together.

## Left to the builder

- **Camera preview aspect ratio.** The game camera has no fixed aspect until it
  renders to a target. Default: use the editor viewport's current aspect so the
  frustum matches what you'd see if you looked through it now. (My call; easy to
  swap for a fixed 16:9 later.)
- Exact wire colors / line segment counts (follow existing gizmo palette).
- Whether the directional arrow also draws when the light is the scene sun
  (it should — selection is selection).
