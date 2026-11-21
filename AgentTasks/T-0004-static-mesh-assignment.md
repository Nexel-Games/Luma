# T-0004 — Static Mesh Assignment (place & assign imported meshes, Unreal-style)

**Status:** Approved
**Assigned to:** engine agent
**Created:** 2026-11-21 by lead (planned with the user: "do it like Unreal Engine")
**Area / files:** `Engine/Scene/**` (MeshRenderer component, reflection, new `AssetReferences`),
`Editor/panels/Inspector/**`, `Editor/panels/Viewport/**`, `Editor/panels/ContentBrowser/**`,
`Editor/panels/PanelContext.h` (new members only), `Editor/LumaEditor/EditorScreen.*`
(mesh placement / references only), `Tests/Scene/**`, `Tests/Editor/**`, `Tests/CMakeLists.txt`,
`GOAL.md`.
**Lead permission for this task only (normally lead-only):** in `Editor/MaterialBridge/**` and
`Editor/LumaEditor/MaterialPreview.*` you may change **only** the mesh code: `SyncMesh`,
`MeshEntry`, `Bounds`, `SectionCount`, the mesh part of `BuildInstances`, and how
`MaterialPreview` sets the preview mesh. Do not touch material syncing/compilation.
**Do not touch:** `Engine/Material/**`, `Engine/Rendering/**`, `Editor/panels/MaterialEditor/**`.

## Goal

Imported meshes (`.lmesh`, made by T-0003) become usable in the level, the way they are in
Unreal Engine:

1. **Drag a mesh from the Content Browser into the viewport.** A preview of the mesh follows
   the cursor, resting on the surface under it; releasing places an actor named after the
   asset and selects it.
2. **Inspector "Static Mesh" row** (Unreal's Details → Static Mesh): thumbnail + searchable
   mesh picker, *Use Selected* / *Browse to* / *Reset* buttons, and drop a mesh tile onto it.
3. **Material slots follow the mesh.** One slot per mesh section, named after the section's
   material. An empty slot draws the mesh's own material (shown greyed); *Reset* on a slot
   returns to that default.
4. **References are saved and survive renames.** Actors store the mesh by virtual path
   (`/Game/...`, the same as material slots). Today the mesh is **not saved at all**
   (`MeshRendererComponent::meshAsset` is not reflected) — that is a bug this task fixes.
   Renaming/moving an asset in the Content Browser updates the open scene's references and
   moves the asset's `.meta` along with it.

## How Unreal does it (summary — you do not have the UE source; this is all you need)

**Placement by drag & drop** (`FLevelEditorViewportClient::DropObjectsAtCoordinates`):
- While an asset is dragged over the viewport, Unreal spawns a **transient preview actor**
  every mouse move (`bCreateDropPreview = true`) and destroys it when the drag leaves or ends.
  The real drop spawns the actor for real (transactional) and selects it.
- Position: Unreal traces the world under the cursor (`TraceWorldForPositionWithDefault`).
  If something is hit, the hit point and surface normal are used; if nothing is hit, a point
  at a fixed distance in front of the camera is used. The point is then **snapped to the
  location grid** if grid snapping is on. The actor is offset so its **bounds rest on the
  surface** (its bottom touches the hit point), not its pivot.
- Dropping onto an existing actor: a mesh can't be "applied" to a component (only materials
  can), so Unreal places a **new** actor on the hit surface of that actor. (Materials dropped
  on an actor are applied — Luma already does that.)
- The new actor's label is the asset name (`SM_Rock`); duplicates get a number.

**Details panel** (`FStaticMeshComponentDetails` + `SPropertyEditorAsset`):
- Row "Static Mesh": `[thumbnail] [ SM_Rock            v ]  [←] [🔍] [↺]`.
  The combo opens an **asset picker**: a search box over a list of every Static Mesh asset
  (thumbnail + name + path), plus a "None" entry. `←` = use the asset selected in the Content
  Browser; `🔍` = browse to the asset in the Content Browser; `↺` = reset to default (None).
  Dropping an asset tile onto the row assigns it (only accepted if it is a mesh).
- "Materials" section: one row per material slot of the mesh (`Element 0…N`), labelled with
  the slot's name. A component's **override materials** default to empty, which means "use
  the mesh's material for this slot". The row shows the mesh's material greyed until
  overridden; the per-row reset arrow clears the override. Changing the mesh keeps the
  override array (extra entries are ignored).

## Global constraints

- Same rules as T-0003: TDD (failing test first, seen failing), production quality, Luma
  style, `LUMA_USER_*` for messages the user should see, no file I/O in draw paths, whole
  suite green, 0 Vulkan validation errors.
- Build: `Scripts\Build.ps1` (or the `_env.bat` + `cmake --build build --config Development`
  line in `AgentTasks/README.md`). Tests: `build\bin\Development\LumaTests.exe`.
- There is **no undo system yet** (T-0002). Do not build one; just mark the scene dirty.

## Review focus (the lead will check these hardest)

1. A scene saved with a mesh actor reloads with the same mesh (and old scenes still load).
2. The drag preview never leaks: leaving the viewport, pressing Escape, or dropping
   elsewhere leaves no extra actor and nothing in the Outliner/scene file.
3. Placement: the mesh's bottom rests on the surface under the cursor (another actor's top,
   or the ground), grid-snapped when snapping is on; a mesh whose pivot is not at its base
   still lands correctly.
4. Material slots: count/names come from the mesh; empty slots render the mesh's material.
5. Renaming a mesh or a folder updates the open scene, moves the `.meta`, and leaves no
   broken references.

---

## Task 1 — Mesh reference by virtual path (saved with the scene)

**Files:** `Engine/Scene/include/Luma/Scene/Components.h`, `Engine/Scene/src/ComponentReflection.cpp`,
`Editor/MaterialBridge/**` (mesh parts), `Editor/LumaEditor/MaterialPreview.*` (mesh part),
`Editor/LumaEditor/EditorScreen.cpp` (`FocusEntity` bounds), `Tests/Scene/**`,
`Tests/Editor/MaterialSceneBridgeTests.cpp`.

- [ ] **Step 1 (test first):** `Tests/Scene/MeshRendererSerializationTests.cpp` (`[scene][mesh]`):
  - A scene with an entity whose `MeshRendererComponent{ mesh = "/Game/Props/SM_Rock",
    materials = {"", "/Game/M_Moss"} }` saved with `SceneSerializer` and loaded back has the
    same `mesh` and `materials`.
  - A scene file written **without** a `mesh` field (copy a current fixture scene JSON) loads
    with `mesh` empty and `primitive` unchanged.
  - `UsesMeshAsset()` is true only when `mesh` is non-empty.
- [ ] **Step 2:** Replace `AssetId meshAsset;` with
  ```cpp
  // Static mesh asset as a virtual path ("/Game/.../SM_Rock"). Empty = draw `primitive`.
  std::string mesh;
  bool UsesMeshAsset() const { return !mesh.empty(); }
  ```
  and reflect it: `b.Property("mesh", &MeshRendererComponent::mesh).Category("Mesh")
  .Tooltip("Static mesh asset (virtual path). Empty draws the primitive shape.");`
- [ ] **Step 3:** Bridge: key meshes by the virtual path instead of `AssetId`.
  `SyncMesh(const std::string& path, SceneRenderer&, const AssetPathResolver&)` resolves the file
  with `paths.ToFile(path, {"lmesh"})`; `Bounds(const std::string& path)`;
  `SectionCount(const MeshRendererComponent&)` unchanged signature; `OnMeshFileChanged(path)`.
  `MeshKey` becomes a hash of the normalized path. A missing file keeps today's behaviour
  (one `LUMA_USER_WARN`, draws the primitive).
  Update `MaterialPreview` (it sets the preview entity's mesh — now a path) and
  `EditorScreen::FocusEntity` (bounds lookup by `mr->mesh`). Update every use of `meshAsset`
  in `Tests/Editor/MaterialSceneBridgeTests.cpp` (the fixture's `WriteMesh` returns the
  virtual path).
- [ ] **Step 4:** Whole suite green. Commit `feat(scene): mesh renderer references meshes by
  virtual path and saves them (T-0004)`.

## Task 2 — The mesh's own materials are the slot defaults

**Files:** `Editor/MaterialBridge/**` (mesh parts + `BuildInstances`), `Editor/panels/PanelContext.h`,
`Editor/LumaEditor/EditorScreen.cpp`, `Tests/Editor/MaterialSceneBridgeTests.cpp`.

The importer writes each section's material as a virtual path into
`LumaSubmesh::materialName` (e.g. `/Game/M_Paint`). That is the mesh's default material
for that slot.

- [ ] **Step 1 (tests first)** in `MaterialSceneBridgeTests.cpp` (`[bridge]`):
  - "Empty slots draw the mesh's section materials": a 2-section mesh whose sections name
    `/Game/M_A` and `/Game/M_B` (both exist in the fixture library), component `materials = {}`
    → instance section 0 uses M_A's handle, section 1 uses M_B's.
  - "An override wins over the mesh material": `materials = {"", "/Game/M_C"}` → section 0 M_A,
    section 1 M_C.
  - "A section material that is not a virtual path (or does not exist) falls back to the
    Default Material" (`materialName = "lambert1"`).
  - `bridge.SectionInfo(path)` returns `{count, names, defaults}` (see Step 2).
- [ ] **Step 2:** `MeshEntry` keeps per-section `defaultMaterial` (the `materialName` if it
  starts with `/`, else empty) and `slotName` (the material's stem without the `M_` prefix
  when it is a path, else `materialName`, else `"Element N"`). Add
  ```cpp
  struct MeshSectionInfo { std::string slotName; std::string defaultMaterial; };
  // Empty when the mesh is not loaded (yet).
  const std::vector<MeshSectionInfo>* Sections(const std::string& meshPath) const;
  ```
  `Sync` also syncs each mesh's default materials (same path as component materials).
  In `BuildInstances`: slot material = override if non-empty, else section default if
  uploaded, else Default Material.
- [ ] **Step 3:** `PanelContext` gains
  `std::function<std::vector<MeshSectionInfo>(Entity)> meshSections;` (empty vector for
  primitives) — EditorScreen wires it to the bridge. Keep `sectionCount` working.
- [ ] **Step 4:** Commit `feat(bridge): mesh section materials are the slot defaults (T-0004)`.

## Task 3 — Inspector: Static Mesh row and mesh-driven material slots

**Files:** `Editor/panels/Inspector/**`, `Editor/panels/PanelContext.h`,
`Editor/LumaEditor/EditorScreen.cpp` (wiring), `Tests/Editor/**`.

Layout (Unreal Details, Luma style; reuse the material slot row's widgets and sizes so the
two rows look identical):

```
Mesh Renderer
  Static Mesh   [thumb]  [ SM_Rock                     v ]
                         [←] [🔍] [↺]
  Shape         [ Cube v ]            <- only while Static Mesh is None
  Materials
  Paint         [thumb]  [ M_Paint (mesh default)       v ]   <- greyed text while not overridden
                         [←] [🔍] [↺]
  Metal         [thumb]  [ M_Chrome                     v ]
```

- [ ] **Step 1 (tests first):** `Tests/Editor/StaticMeshSlotTests.cpp` (`[inspector][mesh]`) for
  pure helpers in a new `Editor/panels/Inspector/include/Luma/Editor/Panels/StaticMeshSlot.h`:
  ```cpp
  // Assign or clear (empty path) the mesh; keeps `materials` (Unreal keeps overrides).
  void AssignStaticMesh(MeshRendererComponent& mesh, std::string_view path);
  // Use Selected: true and assigned when `selectedPath` is a mesh asset (ends in an .lmesh
  // file per `isMesh`), false (unchanged) otherwise.
  bool UseSelectedAssetForMesh(MeshRendererComponent& mesh, const std::string& selectedPath,
                               const std::function<bool(const std::string&)>& isMesh);
  // Filters the picker list: case-insensitive substring on the asset name, sorted by name.
  std::vector<std::string> FilterMeshList(const std::vector<std::string>& paths, std::string_view search);
  ```
  Tests: assign keeps overrides; clearing returns to the primitive; Use Selected rejects a
  material path; filter matches "rock" in `/Game/Props/SM_Rock` and not in its folder name only,
  sorts `SM_A` before `SM_b`.
  Plus a Slate-harness test (see `Tests/Editor/InspectorMaterialTests.cpp` for how the
  Inspector is driven): dropping an `asset/mesh` payload on the Static Mesh row assigns it;
  dropping an `asset/material` payload there does nothing.
- [ ] **Step 2:** `PanelContext` gains `std::function<std::vector<std::string>()> listMeshes;`
  (all `.lmesh` virtual paths, from the registry — no disk scan per frame) and
  `assetThumbnail` must accept `.lmesh` paths (add `"lmesh"` to the extensions in
  `EditorScreen::AssetThumbnail`) and `onBrowseToAsset` must find `.lmesh` files.
- [ ] **Step 3:** Draw the Static Mesh row (`DrawStaticMeshRow`) above Shape; hide Shape while
  a mesh is set. The picker popup is a Slate menu: every popup in the editor is built with
  `Slate::MenuBuilder` and opened with `Context::OpenMenu` (see `Engine/UI/Slate/include/Luma/Slate/Menu.h`
  and the material slot combo in `Inspector.cpp` for a working example; capture lambdas by value): a search field at the top, then "None", then one
  entry per `FilterMeshList` result with its thumbnail. Row accepts drops of `asset/mesh`.
- [ ] **Step 4:** Material slots: rows = max(mesh section count, overrides size); label =
  `MeshSectionInfo::slotName`; when the override is empty show the section default's name
  with `theme.textDim` and its thumbnail; `↺` clears the override (to the mesh default).
  Tooltip on the section header: "One material per mesh section. Empty slots use the mesh's
  own material." Primitives keep today's behaviour (1 slot, Default Material).
- [ ] **Step 5:** Commit `feat(inspector): Static Mesh row, mesh-driven material slots (T-0004)`.

## Task 4 — Drag meshes from the Content Browser; place them in the viewport

**Files:** `Editor/panels/ContentBrowser/src/ContentBrowser.cpp`, `Editor/panels/Viewport/**`,
`Editor/panels/PanelContext.h`, `Editor/LumaEditor/EditorScreen.*`, new
`Editor/panels/Viewport/include/Luma/Editor/Panels/MeshPlacement.h` + `src/MeshPlacement.cpp`,
`Tests/Editor/MeshPlacementTests.cpp`.

- [ ] **Step 1 (tests first)** `MeshPlacementTests.cpp` (`[viewport][placement]`) for the pure
  placement math:
  ```cpp
  struct PlacementHit { Math::Vec3 point; Math::Vec3 normal{0, 1, 0}; };
  struct MeshPlacementInput {
      Math::Vec3 rayOrigin, rayDir;              // world ray under the cursor
      std::optional<PlacementHit> hit;           // surface under the cursor, if any
      Math::Vec3 boundsMin, boundsMax;           // mesh local bounds
      bool snap = false; f32 snapStep = 0.1f;    // gizmo translate snapping
  };
  // World position for the new actor (rotation 0, scale 1).
  Math::Vec3 ComputeMeshPlacement(const MeshPlacementInput& in);
  ```
  Rules (and their tests):
  - Hit on a surface: x/z = hit point x/z; y = `hit.y - boundsMin.y` (the bottom rests on the
    surface even when the pivot is at the mesh's centre — test with bounds y ∈ [-1, 1] → y = hit.y + 1).
  - No hit: intersect the ray with the ground plane y = 0 (same y rule). If the ray does not
    point down (`rayDir.y >= -1e-4`), use `rayOrigin + rayDir * 10` (Unreal's default
    distance in Luma units) with the same y rule.
  - Snapping: x and z rounded to the nearest `snapStep`; y is not snapped (resting wins).
- [ ] **Step 2:** `EditorScreen::PickEntity` gets a sibling returning the hit:
  `std::optional<std::pair<Entity, PlacementHit>> PickSurface(origin, dir, Entity ignore)`
  — the same AABB slab test, returning the entry point and the AABB face normal. Test it in
  an existing EditorScreen-free way: put the slab test in `MeshPlacement.cpp` as
  `std::optional<PlacementHit> RayAabb(origin, dir, lo, hi)` and unit-test it (hit top face
  from above → normal (0,1,0); miss → nullopt; origin inside → exit face).
- [ ] **Step 3:** Content Browser: mesh tiles are drag sources with
  `payload.type = "asset/mesh"`, `data` = virtual path (same block as materials/textures).
- [ ] **Step 4:** Viewport: while `ui.IsDragging("asset/mesh")` and the mouse is over the
  viewport body, call `ctx.onMeshDragPreview(origin, dir, path)` every frame (the payload of the
  active drag — add `const DragPayload* Context::ActiveDrag() const` to Slate if needed, with a
  test in `Tests/Slate/DragDropTests.cpp`); when the drag leaves the viewport, is cancelled
  (Escape) or ends anywhere else, call `ctx.onMeshDragEnd()`. On
  `AcceptDrop(body, "asset/mesh")` call `ctx.onDropMesh(origin, dir, path)`.
- [ ] **Step 5:** EditorScreen:
  - **Preview:** keep `std::optional<MeshPreview>{path, position}`; it is **not** an entity
    (never in the Outliner, never saved, never pickable). Add it as one extra `SceneInstance`
    after `BuildInstances` (selected = true so it gets the outline). Position from
    `ComputeMeshPlacement` with the hit from `PickSurface` and the mesh's bounds from the bridge
    (if the mesh is not loaded yet, sync it first; until bounds exist, draw nothing).
  - **Drop:** create `UniqueEntityName(scene, <asset name>)` with a Transform at the computed
    position and `MeshRendererComponent{ mesh = path }`, select it, mark the scene dirty,
    clear the preview, `LUMA_USER_INFO("Level", "Placed '{}'", name)`. A mesh that fails to load
    still places the actor (it draws its primitive and the bridge warns once).
  - `onMeshDragEnd` clears the preview.
- [ ] **Step 6:** Commit `feat(viewport): drag meshes into the level with a placement preview (T-0004)`.

## Task 5 — Renames and moves keep references (and `.meta`)

**Files:** `Engine/Scene/include/Luma/Scene/AssetReferences.h` + `src/AssetReferences.cpp`,
`Editor/panels/ContentBrowser/src/ContentBrowser.cpp`, `Editor/panels/PanelContext.h`,
`Editor/LumaEditor/EditorScreen.cpp`, `Tests/Scene/AssetReferencesTests.cpp`.

- [ ] **Step 1 (tests first)** `AssetReferencesTests.cpp` (`[scene][references]`):
  ```cpp
  // Rewrites every asset path in the scene's components equal to `from`, or under it when
  // `from` is a folder ("/Game/Props" -> "/Game/Rocks" also maps "/Game/Props/SM_A").
  // Returns how many references changed.
  u32 RetargetAssetReferences(Scene& scene, std::string_view from, std::string_view to);
  ```
  Cases: mesh path renamed; material slot renamed; folder moved (both kinds under it);
  `/Game/Prop` must **not** match `/Game/Props/SM_A` (whole path segments only); unrelated
  paths untouched; returns the count.
- [ ] **Step 2:** Content Browser rename (`ContentBrowserPanel` rename commit, near
  `fs::rename(a.packagePath, newPackage)`): also rename `<file>.meta` if it exists (a failure
  there is a `LUMA_USER_WARN`, the asset rename stands). Then call
  `ctx.onAssetRenamed(oldVirtual, newVirtual)` (new PanelContext callback; folders too).
- [ ] **Step 3:** EditorScreen wires `onAssetRenamed`: `RetargetAssetReferences` on the open
  scene; if it changed anything, mark the scene dirty and
  `LUMA_USER_INFO("Content", "Updated {} reference(s) to '{}'", n, newVirtual)`; tell the bridge
  the old mesh/material paths are stale (`OnMeshFileChanged(old)`), so the renamed asset reloads
  under its new path. Scenes on disk that are not open are **not** rewritten (Unreal leaves a
  redirector; we don't have one yet — note it in GOAL.md as an open item).
- [ ] **Step 4:** Commit `feat(content): renames update scene references and move .meta (T-0004)`.

## Task 6 — Manual verification, docs

- [ ] **Step 1: Manual checks** in a scratch copy of `Tests/Fixtures/MaterialShowcase` (never
  `TestProject`). Write what you saw in *Result*:
  1. Import `Tests/Fixtures/Import/robot.glb`. Drag `SM_Body` into the viewport: the preview
     follows the cursor, rests on the floor, and steps on the grid with snapping on; on another
     actor it rests on that actor's top. Release → actor `SM_Body` selected in the Outliner.
     Drag again and press Escape / leave the viewport → nothing added.
  2. Inspector shows Static Mesh `SM_Body`, Shape hidden, material slot "Paint" greyed
     `M_Paint`. Override it with `M_Metal`, reset → back to `M_Paint`.
  3. Picker: type "arm" → only `SM_Arm`; pick it → the actor shows the arm. None → Cube again.
  4. Save the scene, reopen the project → the actor still shows `SM_Arm`.
  5. Rename `SM_Arm` to `SM_RobotArm` in the Content Browser → the actor still shows it, its
     `.meta` moved, the scene is marked dirty. Rename the folder holding it → same.
  6. `Saved/Logs/Editor.log`: 0 `VUID` / `validation error`.
- [ ] **Step 2:** `GOAL.md` §5.6: tick "Assign an imported static mesh to an actor" and describe
  placement/Inspector/rename fix-up; add open items: redirectors for closed scenes, Static Mesh
  Editor, "Replace selected actors with", multi-asset drops. Commit `docs: static mesh
  assignment (T-0004)`.
- [ ] **Step 3:** Merge `dev` into your branch, build, full suite, set **Completed**, push the
  branch and merge it into `dev` (never `main`).

## Acceptance criteria

- [ ] Every test named above exists, was seen failing first, and passes.
- [ ] Whole suite green; manual checks written up in *Result*; 0 validation errors.
- [ ] No `meshAsset` (AssetId) mesh references left anywhere; old scenes load.

## Out of scope

- Static Mesh Editor (double-click a mesh), collision, LODs, Nanite-like features.
- "Replace Selected Actors with", dropping several assets at once (the Content Browser is
  single-select today), undo/redo (T-0002), redirector files for scenes that are not open.

## Questions

## Result

## Review
