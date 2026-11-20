# Luma PBR Material System — Phase 1 Design

Date: 2026-11-20 · Status: draft for approval (builder-checked)

## 1. Intent

Luma needs a real material system modeled on Unreal Engine, styled the
Luma way. An earlier node-graph system was "not implemented properly and
building the system was causing bugs", so it was removed (it is still in
the last commit; the working tree keeps only a flat `.lmat` asset + its
serializer). This time the system is built **foundation first**:

- **Phase 1 (this document):** PBR materials that work end to end — assets,
  Material Instances, textures bound through materials, a fixed,
  well-tested "Default Lit" shader template, editor tooling — with no
  shortcuts. The output must be correct and complete, not a demo.
- **Phase 2 (separate design later):** an Unreal-style node graph that
  compiles into the *same* shader template (UE inserts translated graph
  code into `MaterialTemplate.ush`; Luma will replace one evaluation
  function). Phase 1 is shaped so Phase 2 slots in without rework.

Decisions taken with the user:
- Approach **B** (foundation first, graph second). Materials are **PBR**
  (metallic/roughness).
- **Materials only** (no per-object color override). Mesh Renderer has a
  material slot per mesh section; empty = engine Default Material.
  Existing scenes migrate automatically (inline colors become Material
  Instances) so nothing changes visually.
- **Build every shader and system needed — no shortcuts.** Asset *import*
  UX improvements come later.
- **One render path, deferred:** the live `VulkanDeferredRenderer` is the
  engine's main (and only) scene renderer. The unused paths — the
  never-initialised `Renderer2::DeferredShadingRenderer` stack and the old
  forward `VulkanSceneView` (`scene.vert/.frag`, `RenderSceneView`) — are
  deleted rather than ported. When the game runtime draws the world it uses
  the same deferred renderer.
- **Unreal-style selection:** the brighten-on-select tint goes away; the
  viewport draws an orange **selection outline**. New shapes spawn with the
  Default Material (grey, like Unreal).

## 2. Goals

1. Material assets (`.lmat`) and Material Instance assets (`.lmatinst`)
   following Unreal's `UMaterial` / `UMaterialInstanceConstant` model.
2. Standard PBR inputs, each either a constant or a texture (with tint,
   channel select, UV tiling/offset): Base Color, Metallic, Specular,
   Roughness, Normal, Emissive Color (+ intensity), Ambient Occlusion,
   Opacity Mask.
3. Material settings: Blend Mode (Opaque, Masked), Shading Model (Default
   Lit, Unlit), Two Sided, Opacity Mask Clip Value.
4. Renderer renders them correctly in every pass: G-buffer, lighting,
   shadows (masked + two-sided), emissive, thumbnails, preview.
5. Meshes carry UVs and tangents into the shaders (primitives + imported
   `.lmesh`), so textures and normal maps work.
6. Editor: Material Editor panel with live preview; Mesh Renderer material
   slots in the Inspector; Content Browser create / create-from-textures /
   create-instance; drag-and-drop assignment; thumbnails.
7. Scene/asset migration from the current format with no visual change.
8. Tests for every model, serialization, resolution, geometry and
   naming rule.

## 3. Non-goals (Phase 1)

- Node graph, Material Functions, custom expressions (Phase 2).
- Translucent / additive blend modes (needs a forward translucency pass).
- Shading models beyond Default Lit + Unlit (subsurface, clear coat, …).
- World Position Offset, Pixel Depth Offset, tessellation.
- Material Instance Dynamic / runtime scripting of parameters.
- Texture compression (BC), virtual texturing, streaming.
- Asset import UX (import dialogs, re-import settings) — later.

## 4. Anti-goals (failure even if it "works")

- Any input that appears in the UI but is not honoured by every relevant
  render pass (e.g. masked material casting a solid shadow).
- Half-built tooling: placeholder panels, dead buttons, TODO paths.
- Color-space mistakes (normal/roughness maps sampled as sRGB, base color
  as linear).
- Visual change to existing scenes after migration.
- Unverified rendering code: every shader path is exercised by a test or
  a documented screenshot check.

## 5. Approaches considered

| | Approach | Trade-off | Verdict |
|---|---|---|---|
| A | Graph first (full UE model, runtime shader compile per material) | Most Unreal-like, but the riskiest part first — what failed last time; needs a runtime shader compiler in the editor | Rejected |
| **B** | **Foundation first: fixed Default Lit template + assets/instances/textures/tooling; graph later compiles into the same template** | Usable early, each layer proven before the next | **Chosen** |
| C | Parameters only, never a graph (Unity-style) | Simplest, but not following Unreal; outgrown later | Rejected |

## 6. Architecture

Modules stay separate (project rule: every feature its own module, Vulkan
backend stays generic — feature data reaches it through the RHI).

### 6.1 `Engine/Material` — asset model (renderer-agnostic)

- `MaterialParameter` enum (append-only, persisted by name not int):
  BaseColor, Metallic, Specular, Roughness, Normal, EmissiveColor,
  AmbientOcclusion, OpacityMask.
- `MaterialInput` (one per parameter):
  - `constant` (vec4; scalars use `.x`, colors `.rgb`),
  - `texture` (AssetId, optional),
  - `channel` (R/G/B/A/RGB — lets one packed ORM/ARM texture feed AO,
    Roughness, Metallic),
  - `tint`/`scale` (multiplied with the texture sample),
  - `uvTiling` (vec2), `uvOffset` (vec2),
  - Normal only: `strength`, `flipGreen` (DirectX-style maps).
  - Emissive only: `intensity` (HDR multiplier).
- `MaterialSettings`: blendMode {Opaque, Masked}, shadingModel {DefaultLit,
  Unlit}, twoSided, opacityMaskClip (default 0.3333, UE default).
- `Material` = settings + one `MaterialInput` per parameter.
- `MaterialInstance` (UE `UMaterialInstanceConstant`): `parent` (AssetId of
  a Material or another Instance) + per-parameter **override** flags and
  values (UE `bOverride` model) + optional overrides of
  twoSided/opacityMaskClip/blendMode (UE `BasePropertyOverrides`).
- **Asset references are stored as Unreal-style project paths**, not raw
  AssetIds (which are hashes of absolute paths and break when a project is
  moved): `/Game/...` = the project's `Assets/` folder, `/Engine/...` =
  engine content. Used by material→texture, instance→parent and scene
  slot→material references; resolved to AssetIds through the registry at
  load time. (Builder call recorded by Claude.)
- `ResolvedMaterial` = what the renderer consumes: the instance chain
  flattened onto its root Material. `MaterialResolver` walks parents with
  cycle detection and a depth limit; missing parent → Default Material +
  user-facing warning.
- `MaterialLibrary` (editor/runtime service): loads `.lmat` / `.lmatinst`
  by AssetId through the AssetRegistry, caches resolved results, bumps a
  per-material **version** on edit/save so renderers re-upload only what
  changed; listens for file changes (existing VFS FileWatcher) to reload
  externally edited assets.
- Serialization: JSON via `Engine/Serialization`, versioned. Loader reads
  the current flat `.lmat` (v1, ignoring the dead `graph`/`properties`
  blocks) and writes v2. New extension `.lmatinst` registered as
  `AssetType::MaterialInstance`.
- Engine content: `Content/Engine/Materials/DefaultMaterial.lmat`,
  referenced as `/Engine/Materials/DefaultMaterial`, mounted so every
  project can reference it. Its values equal the old component defaults
  (base color 0.82/0.82/0.85, metallic 0, roughness 0.5) so untouched
  objects look identical after migration.

### 6.2 RHI contract (`Engine/Rendering/RHI`)

- `SceneInstance` loses albedo/metallic/roughness and gains a
  `materialHandle` per section (opaque handle from the renderer's material
  table) + section index/range.
- New renderer API: `MaterialHandle UpdateMaterial(key, const
  MaterialGpuDesc&)`, `ReleaseMaterial`, `TextureHandle
  UploadTexture(key, pixels/mips, colorSpaceInfo)`. `MaterialGpuDesc` is
  the flattened, renderer-agnostic description (constants, texture keys,
  flags) produced from `ResolvedMaterial` by the editor/runtime scene
  builder.
- Vertex format for meshes: position, normal, **tangent (xyz + w
  handedness)**, **uv0**.

### 6.3 Vulkan backend

- **Device features:** enable Vulkan 1.2 descriptor indexing
  (`runtimeDescriptorArray`, `descriptorBindingPartiallyBound`,
  `descriptorBindingVariableDescriptorCount`,
  `shaderSampledImageArrayNonUniformIndexing`,
  `descriptorBindingSampledImageUpdateAfterBind`) and `samplerAnisotropy`;
  fail with a clear error if missing.
- **`VulkanTextureCache` module:** uploads textures (from the existing
  `.ltex`/image loaders), generates the full **mip chain on the GPU**
  (blit chain), creates images with `MUTABLE_FORMAT` and **two views**
  (UNORM + SRGB). Color slots (Base Color, Emissive) use the SRGB view
  when the texture asset is flagged sRGB; data slots (Normal, Roughness,
  Metallic, AO, Opacity Mask, Specular) always use the UNORM view — so data
  maps are correct regardless of import flags. Anisotropic (16x) wrap
  sampler. Bindless: one large `sampler2D[]` array descriptor
  (update-after-bind), stable slot per texture view, free-list reuse.
- **`VulkanMaterialSystem` module:** a GPU **material table** (SSBO of
  packed `GpuMaterial` structs: constants, texture slot indices per input,
  channel/tiling/offset/flags). Material handles index this table; edits
  update one entry. Double-buffered or fenced writes so in-flight frames
  never read a half-written entry.
- **Shader template** (`material_common.glsl`, shared by every pass):
  `MaterialSample EvaluateMaterial(MaterialInputs in)` — samples and
  combines all inputs. *This function is the Phase 2 graph insertion
  point.* Includes TBN normal mapping (strength, green flip,
  renormalisation), channel select, tint/scale, tiling/offset, alpha clip.
- **G-buffer pass** (`gbuffer.vert/.frag` rewritten): new vertex layout,
  per-draw push constant = model matrix + normal matrix row data +
  material index. Outputs: RT0 base color + shading model id, RT1 world
  normal + roughness, RT2 metallic/specular/AO, **RT3 emissive (HDR)**.
  Masked materials `discard` below clip value. **Pipeline permutations:**
  {Opaque, Masked} × {one-sided, two-sided} (two-sided flips the normal
  for back faces).
- **Lighting pass** (`deferred_lighting.frag`): reads RT3 emissive and
  adds it; Unlit pixels output base color + emissive without lighting;
  F0 from Specular (`0.08 * specular`, UE convention, 0.5 → 0.04); AO
  from the material multiplies ambient/sky light. View modes (Base Color,
  etc.) keep working with the new buffers.
- **Shadow pass** (`shadow.vert/.frag`): new vertex layout; Masked
  materials sample opacity mask and discard; two-sided materials render
  with culling off. Permutations {Opaque, Masked} × {one/two-sided}.
- **Eye adaptation, bloom, overlay, view modes** unchanged except that
  emissive now feeds bloom naturally through the HDR buffer.
- Thumbnails and the Material Editor preview use a second
  `VulkanDeferredRenderer` instance (the existing, currently unused
  512×320 "material" instance) with a preview scene
  (sphere/cube/plane/cylinder, studio-style sky/lights). Material
  thumbnails move off the current CPU ray tracer onto this GPU path so
  they match the viewport exactly. Mesh thumbnails (today on the unused
  Renderer2 stack) move to the same path.
- **Selection outline:** a post-process pass (after composite/FXAA, before
  the gizmo overlay) draws an orange outline around the selected actor
  from a selection mask written during the G-buffer pass (stencil or a
  per-draw "selected" bit), Unreal-style. Replaces the old albedo tint.
- **Removed:** `Renderer2::DeferredShadingRenderer` and its passes
  (`Engine/Rendering/Renderer` G-buffer/shadow/lighting code that only it
  uses), `VulkanSceneView` forward rendering and `scene.vert/.frag`,
  `RenderSceneView` on the RHI (or repointed to the deferred renderer if a
  caller needs it), and the editor's `m_deferredRenderer` member and
  render-mode/debug-visualization hooks that only drive that dead stack.

### 6.4 Geometry (`Engine/Rendering/Mesh` + asset mesh path)

- Primitive generators (cube, plane, sphere, cylinder) emit UVs and
  tangents (cube: per-face UVs 0..1; sphere: equirect UV with seam
  duplication; cylinder: wrap + caps; plane: 0..1).
- Imported `.lmesh`: UV0 and tangents read when present; otherwise
  tangents generated (MikkTSpace-compatible per-vertex accumulation with
  Gram-Schmidt and handedness). Sections (submeshes) map to material
  slots.
- Texture color space (builder call recorded by Claude): `.ltex` uses its
  stored sRGB flag; a raw image with no sidecar defaults to sRGB unless its
  name marks it as data (`normal|nor|nrm|rough|metal|ao|occlusion|orm|arm|
  mask|opacity|height|disp`). `TextureImporter::ConvertToSRGB` currently
  bakes an sRGB curve into pixel values — that is fixed (pixels stay as
  authored; color space is a flag read through the SRGB/UNORM view), since
  otherwise colors convert twice. "Create Material from Textures" sets the
  sRGB flag on base color / emissive textures it assigns.
- Mesh GPU buffers rebuilt with the new vertex layout everywhere (scene,
  G-buffer, shadow, thumbnail paths).

### 6.5 Scene

- `MeshRendererComponent`: `std::vector<AssetId> materials` (one per
  section; empty entry = Default Material). Inline albedo/metallic/
  roughness removed.
- Scene serializer v-bump. **Migration on load:**
  - Legacy values equal to the Default Material → empty slot (no asset).
  - Other values → a Material Instance of the Default Material, one per
    unique value combination, named from the first entity using it, kept
    **in memory** and assigned to the slot. Before creating one, existing
    instances in `Assets/Materials/Migrated/` with identical overrides are
    reused (dedup across scenes).
  - New instance files are written to `Assets/Materials/Migrated/` only
    when the scene is **saved** — opening and discarding leaves no files.
  - With no project mounted (no `Assets/`), instances stay in memory and a
    warning explains they cannot be saved.
  - A user-facing Output Log message lists what was migrated.
- New primitives spawn with an empty slot (Default Material). The palette
  colour is removed.
- Editor scene builder resolves each slot through `MaterialLibrary`,
  pushes changed materials/textures to the renderer, fills
  `SceneInstance` material handles.

### 6.6 Editor

- **Material Editor panel** (new module `Editor/panels/MaterialEditor`,
  dockable tab opened by double-click on a material or instance, one tab
  per open asset, `*` when dirty):
  - Toolbar: Save (Ctrl+S), Browse to Asset, preview shape selector,
    preview background toggle.
  - Preview viewport: live render, orbit with mouse, auto-rotate off.
  - Details: Material Settings section; one section per input with
    constant editor (color picker / drag float), texture slot (asset
    picker combo with thumbnails, clear, browse), channel, tint/scale,
    tiling/offset, normal strength/flip, emissive intensity.
  - Instance mode: Parent picker; every parameter row shows an override
    checkbox (unchecked = inherited value shown dimmed, UE style).
  - Edits apply live to the preview *and* the level viewport; Save writes
    the asset and regenerates its thumbnail. Closing a dirty tab asks to
    Save / Discard / Cancel (modal).
- **Inspector — Mesh Renderer:** "Materials" list, one row per slot:
  thumbnail, asset picker (searchable menu of Material + Instance assets),
  browse-to, clear, and "Create Instance" (creates an instance of the
  current material next to it and assigns it). Shape combo unchanged.
- **Content Browser:**
  - Create menu: Material, Material Instance (of selected material).
  - Asset menu on a Material/Instance: Create Material Instance, Open.
  - Asset menu on selected Textures: **Create Material from Textures** —
    auto-assigns by name tokens (`diff|albedo|basecolor|color` → Base
    Color, `nor|normal|nrm` → Normal (`_gl`/`_dx` sets flip), `rough` →
    Roughness, `metal` → Metallic, `ao|occlusion` → AO, `arm|orm` →
    packed AO/Roughness/Metallic channels, `emiss` → Emissive, `opacity|
    mask|alpha` → Opacity Mask (+ Masked)).
  - Material thumbnails rendered on create/save.
- **Drag and drop (new Slate capability):** a drag payload system in
  `Engine/UI/Slate` (begin drag from Content Browser tiles, drop targets
  query payload type). Drop a material on an Inspector slot or on an
  actor in the viewport (picks the section under the cursor via the
  existing ray pick, extended to report section where available).
- All new actions log user-facing Output Log messages (LUMA_USER_*).

## 7. Testing

- Material model: defaults, serialization round-trip (v1 → v2 load, v2
  round-trip), instance override resolution (deep chains, cycle
  detection, missing parent), versioning.
- Content Browser create-from-textures name matching (table-driven).
- Geometry: primitive UV ranges, tangent orthogonality/handedness,
  generated tangents vs analytic on a plane/sphere.
- Scene migration: legacy scene → instances created/deduplicated, visuals
  (resolved constants) identical.
- Texture color-space rule (slot → view selection) unit-tested.
- GPU: headless render tests via `--screenshot` of a test scene covering
  textured, normal-mapped, masked (incl. shadow), two-sided, unlit and
  emissive materials; checked into the plan as verification steps.
- Vulkan validation clean (zero errors) for all paths.

## 8. Left to the builder

- Exact GPU struct packing, descriptor counts, binding numbers.
- Whether the material table is double-buffered or fence-guarded.
- Preview scene lighting setup details.
- File layout inside new modules, following existing conventions.
