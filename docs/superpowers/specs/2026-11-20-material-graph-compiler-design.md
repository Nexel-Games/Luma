# Material Graph & Compiler — Design (Material Editor project 1 of 3)

**Date:** 2026-11-20
**Status:** awaiting approval
**Follows:** `2026-11-20-pbr-material-system` (Phase 1: fixed-recipe PBR materials)
**Next projects:** 2 — Graph Editor UI (canvas, details, preview, undo, stats);
3 — node-library long tail. This document covers project 1 only.

## Intent

Replace Luma's fixed material recipe ("per input: constant or texture × tint, tiling,
offset") with **node-graph materials compiled to GPU shaders**, the way Unreal does
it, at AAA depth: any math, animation, blending, parameters, reusable functions,
global parameter collections, translucency, post-process materials and extra shading
models. The user asked for "full advance AAA feature … no demo, no shortcuts" and to
study the Unreal 5.8 source (`D:/output/UnrealEngine-5.8`) — architecture and
behaviour only, never copied code.

Success: an artist can build in a graph (file-authored in this project; visually in
project 2) anything a UE-class material can express in the supported domains, it
compiles in the editor in the background, renders correctly in every pass, and
Material Instances retune it without recompiling.

## Goals

1. **Graph model** — materials are graphs of expression nodes feeding a Material
   Output node; serialized in `.lmat` v4; existing v3 materials migrate automatically.
2. **Compiler** — translates a graph into GLSL for each pass/stage with Unreal's type
   rules, constant folding and *uniform expressions* (CPU-evaluated "preshaders"), and
   reports errors per node.
3. **Runtime shader compilation** — GLSL → SPIR-V in the editor via **shaderc** (ships
   in the Vulkan SDK: `shaderc_combined.lib`), asynchronous, with an on-disk cache.
4. **Per-material pipelines** — every pass a material appears in (G-buffer, shadow
   depth, selection mask, forward translucency, post-process, Material preview) uses
   that material's compiled shaders.
5. **Parameters & instances** — Scalar / Vector / Texture / Static Switch parameters
   (groups, sort priority, slider ranges, descriptions); instances override them by
   name; only static-switch overrides cause a recompile (own permutation).
6. **Material Functions** (`.lmatfunc`) — reusable sub-graphs with Function Input /
   Output nodes, inlined at compile; edits recompile every dependent material.
7. **Material Parameter Collections** (`.lmpc`) — global scalar/vector parameters
   bound per frame, settable from the editor/runtime without recompiling.
8. **Domains & blend modes** — Surface: Opaque, Masked, Translucent, Additive,
   Modulate (with refraction and a forward translucency pass); Post Process
   (blendable, before/after tone mapping) with a Scene Texture node.
9. **Shading models** — Default Lit, Unlit, Subsurface, Clear Coat, Two-Sided
   Foliage (G-buffer gains a custom-data target; lighting gains per-model branches).
10. **Vertex stage** — World Position Offset (wind, deformation) applied consistently
    in the G-buffer, shadow, selection and translucency passes; custom UV
    interpolators (vertex-computed values passed to the pixel stage).
11. **Errors & stats** — node-level compile errors surfaced to the console and kept
    for the editor; per-pass stats (SPIR-V instruction count, texture/sampler counts).

## Non-goals (this project)

- The visual graph editor, its undo system and preview UX (project 2). Here graphs are
  authored in files / code / tests and through the existing panels.
- Deferred decals, UI-domain materials, light functions, volumetric materials,
  Substrate, virtual texturing, tessellation/displacement, Nanite-style features —
  each needs a renderer feature that does not exist yet; listed as later projects.
- Platform shader formats other than SPIR-V for Vulkan.

## Anti-goals

- A demo path: a "graph view" over the old fixed recipe, a GPU bytecode interpreter,
  or any material feature that silently falls back without telling the user.
- Copying Unreal source.
- Making the Vulkan backend know about graphs: it receives SPIR-V blobs, layouts and
  parameter data through the RHI (modular-architecture rule).

## Approaches (decided)

1. **Compile each graph to shader code, compile at runtime, cache** — *chosen*
   (Unreal's model; the only one meeting "no demo, full AAA").
2. GPU bytecode interpreter in one uber-shader — rejected: slow, cannot express
   vertex/translucency/post-process features.
3. Visual front-end over the fixed recipe — rejected: no math/animation/blending.

Order agreed: this project (graph + compiler), then the graph editor, then the
node-library long tail.

## Architecture (modules)

| Module | Folder | Responsibility |
|---|---|---|
| `Luma::Material` (extended) | `Engine/Material` | Assets: Material (settings + graph), MaterialInstance, MaterialFunction, MaterialParameterCollection; JSON (de)serialization; v3→v4 migration; MaterialLibrary resolution. |
| `Luma::MaterialGraph` (new) | `Engine/Material/Graph` | Graph data model, pin types, the expression registry (each expression = one class: pins, properties, `Compile`). |
| `Luma::MaterialCompiler` (new) | `Engine/Material/Compiler` | Translator (graph → typed code chunks → GLSL per stage/pass), type rules, constant folding, uniform-expression (preshader) bytecode + evaluator, parameter/texture tables, static-switch permutations, errors, stats. Produces a backend-neutral `CompiledMaterial`. |
| `Luma::ShaderCompiler` (new) | `Engine/Rendering/ShaderCompiler` | GLSL→SPIR-V (shaderc), include resolution from `Engine/Rendering/Vulkan/shaders`, async job queue, disk cache (`Saved/ShaderCache`), SPIR-V reflection for stats. Generic: knows nothing about materials. |
| RHI (extended) | `Engine/Rendering/RHI` | `MaterialShaderDesc` (per-pass SPIR-V + raster/blend state + domain), parameter-data upload, parameter-collection upload. Replaces the fixed `MaterialGpuDesc` input list. |
| Vulkan (extended) | `Engine/Rendering/Vulkan` | Per-material pipeline cache (material × pass × state), shared pipeline layout, separate sampled-image + sampler tables, forward translucency pass, refraction scene-color copy, post-process material passes, G-buffer custom data target, shading-model lighting. |
| Editor bridge (extended) | `Editor/MaterialBridge` | Drives compile requests, applies instance parameters, shows the Default Material while a material compiles, reports errors to the console. |

## Design

### 1. Graph data model

- `MaterialGraph { nodes, links, comments }`. Node: stable `u64 id`, expression type
  name, editor position, property values (typed key/values from the expression's
  property schema). Link: `(fromNode, fromOutput) → (toNode, toInput)`; an input has
  at most one link.
- The **Material Output** node is the graph root; its inputs depend on domain,
  blend mode and shading model (Unreal's "material attributes" visibility rules):
  Base Color, Metallic, Specular, Roughness, Anisotropy, Emissive Color, Opacity,
  Opacity Mask, Normal, Tangent, World Position Offset, Subsurface Color, Custom
  Data 0/1 (Clear Coat, Clear Coat Roughness), Ambient Occlusion, Refraction, Pixel
  Depth Offset, plus Custom UV 0-7. Hidden/irrelevant pins are ignored with a
  warning if linked.
- Pin value types: `Float1..Float4`, `Bool` (static), `Texture2D`, `TextureCube`,
  `MaterialAttributes`, plus `Any` for polymorphic math.
- `.lmat` v4 = settings + graph. Settings: domain, blend mode, shading model, two
  sided, opacity-mask clip, translucency lighting mode, refraction method, post-process
  blend location and priority, "used with" flags.

### 2. Expression catalogue (project 1)

Every expression compiles through the same translator API; each has a property
schema, pin list, description and category (the editor's palette uses them).
- **Constants:** Constant, Constant2/3/4Vector, Static Bool.
- **Parameters:** Scalar, Vector, Texture Sample Parameter 2D / Cube, Texture Object
  Parameter, Static Switch Parameter, Static Bool Parameter, Collection Parameter.
- **Math:** Add, Subtract, Multiply, Divide, Power, Abs, Min, Max, Clamp, Saturate,
  One Minus, Lerp, Dot, Cross, Normalize, Length, Distance, Sqrt, Exp, Log2,
  Sine, Cosine, Tangent, Arcsine, Arccosine, Arctangent2, Floor, Ceil, Round, Frac,
  Fmod, Sign, Step, Smooth Step, If, Append, Component Mask, Break Out / Make
  Float2-4, Desaturation, Remap (Range).
- **Texture:** Texture Sample (2D/Cube; sampler: wrap/clamp/mirror, filter, MIP
  mode bias/level/derivatives; sampler type color/linear/normal/masks), Texture
  Object, Texture Property (size, texel size).
- **Coordinates:** Texture Coordinate (index, tiling, un-mirror), Panner, Rotator,
  World Position, Object Position, Object Radius/Bounds, Actor Position, Camera
  Position, Camera Vector, Vertex Normal WS, Pixel Normal WS, Vertex Tangent WS,
  Reflection Vector, Screen Position, View Size, Pixel Depth, Scene Depth, Time,
  Delta Time, Vertex Color.
- **Utility:** Fresnel, Bump Offset (parallax), Parallax Occlusion Mapping, Normal
  From Height (texture), Blend Angle Corrected Normals, Transform (vector/position
  between tangent/local/world/view), DDX, DDY, Custom (GLSL code with named
  inputs, typed output, extra outputs, include files), Vertex Interpolator, Named
  Reroute Declaration / Usage, Reroute.
- **Functions:** Material Function Call, Function Input (typed, preview default),
  Function Output.
- **Post process:** Scene Texture (Scene Color, Depth, World Normal, Base Color,
  Roughness, Metallic, Custom Data, Post Process Input 0), Scene Color (refraction
  sample for translucency).

### 3. Compiler

- **Code chunks:** compiling an input returns a chunk index; a chunk has GLSL code, a
  type, and a frequency — `Constant` (folded at compile time), `Uniform` (depends
  only on parameters/collections: evaluated on the CPU), or `Varying` (per pixel /
  vertex). Results are cached per (node, output, stage) so shared sub-graphs emit
  once; cycles are errors naming the nodes on the cycle.
- **Type rules (Unreal's):** a Float1 broadcasts to any width; otherwise widths must
  match, with explicit Append / Component Mask; textures only into texture
  pins; static bools only into static inputs. Errors carry node id + pin.
- **Uniform expressions (preshaders):** every Uniform chunk becomes a slot in the
  material's parameter block and a small bytecode program (load parameter / constant /
  collection value, arithmetic, built-in functions). Instances evaluate the program
  on the CPU with their overrides → new parameter-block bytes, no recompile.
- **Static switches:** resolved at compile time; each distinct static-parameter set
  is its own permutation (key = material + static values).
- **Per stage / pass:** the translator compiles the Material Output inputs needed by
  each shader: vertex (WPO, custom UVs/interpolators), G-buffer pixel, depth-only
  (WPO + opacity mask/clip + pixel depth offset), selection mask, forward
  translucency (full lighting), post-process. Pixel-only expressions used from the
  vertex stage are errors (Unreal's frequency rule).
- **Output:** `CompiledMaterial { per-pass GLSL, parameter block layout, preshader
  program, texture table (bindless slots + sampler states), parameter metadata,
  referenced functions/collections, static-switch key, errors, warnings, stats }`.
- **Templates:** hand-written GLSL templates per pass (`material_template_*.glsl`)
  define the vertex/pixel entry points, interpolants, and insertion points; the
  existing `material_common.glsl` evaluation is replaced by generated code.
- **Determinism:** generated code is stable (sorted, canonical names) so identical
  graphs hash identically.

### 4. Shader compilation & cache

- `ShaderCompiler` wraps shaderc (optimization on, debug info in Debug builds),
  resolves `#include` from the engine shader directory, and returns SPIR-V or the
  compiler's error log mapped back to generated lines.
- Async: a worker pool compiles jobs; results are handed back on the main thread,
  where pipelines are created. While a material compiles, it renders with its last
  good shaders, or with the Default Material on first compile; the console reports
  start/finish/failure (`LUMA_USER_*`).
- Cache: `Saved/ShaderCache/<key>.spv`; key = hash(generated GLSL, template
  versions, include hashes, shaderc version, options). Engine startup with a warm
  cache performs no compiles. The cache is safe to delete.

### 5. Renderer integration

- One shared pipeline layout for all material pipelines: set 0 view/frame data
  (camera, time, viewport size, collections), set 1 bindless sampled images + sampler
  table, set 2 material parameter blocks (storage buffer; each draw passes its
  block offset + per-object data in push constants).
- Pipeline cache keyed by (compiled-shader key, pass, cull mode, blend state,
  depth state); pipelines are destroyed when no material references them.
- **G-buffer:** adds RT4 (custom data: subsurface colour/opacity, clear coat and its
  roughness, foliage transmission); shading-model id stays in RT0.a; the lighting
  pass gains Subsurface, Clear Coat, Two-Sided Foliage branches (lighting functions
  move to a shared `lighting_common.glsl` used by both deferred and forward passes).
- **Shadows / selection:** use each material's depth-only / mask variants, so WPO
  and masked cut-outs cast and outline correctly.
- **Translucency:** a forward pass after lighting, back-to-front by object distance;
  Translucent / Additive / Modulate blending; lit per pixel with the sun (CSM),
  local lights and sky light (same functions as deferred); refraction samples a
  scene-colour copy made before the pass; opacity drives alpha; two-sided handling.
- **Post process materials:** the post-process stack (in the Environment /
  post-process settings) gains an ordered list of post-process materials; each
  runs as a fullscreen pass at its blend location (before / after tone mapping),
  reading scene textures.
- **Parameter collections:** one buffer per collection, uploaded when values change.
- The Material Editor preview renderer uses the same compiled pipelines.

### 6. Assets, instances, migration

- **Material Instance (`.lmatinst` v2):** parent + overrides by parameter name and
  type (scalar, vector, texture, static switch) + optional settings overrides that
  don't need recompiling (e.g. two-sided where allowed). Missing / renamed parameters
  warn and keep the override for re-linking.
- **Material Function (`.lmatfunc`):** graph + inputs/outputs + description +
  "expose to library" flag + category. Recursion is detected.
- **Material Parameter Collection (`.lmpc`):** ordered named scalars/vectors with
  defaults; runtime/editor API to set values.
- **Migration:** v3 materials become equivalent graphs (Texture Sample / Constant →
  Multiply (tint) → Material Output; UV tiling/offset via Texture Coordinate;
  invert via One Minus; normal strength/flip via the same math; height/bump via
  Parallax Occlusion Mapping / Normal From Height; emissive intensity via Multiply).
  Inputs the old instances overrode become parameters with the old names, and v1
  instances migrate to v2 overrides. Migrated materials render identically (golden
  image test against the Phase-1 renderer output).
- `MaterialLibrary` resolves graphs, functions and collections, tracks dependencies
  (function / collection / texture → materials) for hot reload, and keeps the existing
  save / revert / reload semantics.

### 7. Errors & stats

- Errors: `{ node id, pin, message }` (plus raw shaderc log for Custom nodes mapped
  to the node); a material with errors keeps rendering its last good version and
  the console says why. Warnings for unused parameters, ignored pins, implicit
  truncation.
- Stats per pass: SPIR-V instruction count, texture samples, sampler count,
  parameter-block size, preshader op count.

## Testing

- Unit (no GPU): graph (de)serialization round trip; v3→v4 migration; type rules
  (every legal/illegal combination); constant folding; preshader evaluation vs
  expected values; static-switch permutation keys; cycle and frequency errors with
  correct node ids; deterministic codegen (same graph → same hash).
- Compile coverage: every expression in a minimal graph compiles through shaderc
  for every pass it supports (catches template/codegen breakage).
- Cache: warm cache → zero compiles; changed include → recompiles.
- GPU tests (existing Vulkan test harness): a compiled material renders the expected
  value in an offscreen G-buffer readback; instance parameter changes update without
  recompiling; WPO moves shadow-map depth; translucency blends; post-process
  material changes the final image.
- Visual: MaterialShowcase migrated (identical to Phase 1), plus new fixtures using
  panner animation, layered blending with a mask, glass with refraction, foliage
  with WPO + two-sided foliage shading, clear-coat car paint, a post-process outline.
  0 Vulkan validation errors.

## Builder self-check (no subagent; read as the builder)

- *Where does Time come from?* Set 0 frame data (time, delta time), not the
  parameter block.
- *Bindless textures today are combined samplers with 2 views per texture.* This
  project changes to separate sampled images (linear + sRGB views) and a sampler
  table, so sampler state is per Texture Sample node.
- *How many custom UV / interpolators?* 8 Custom UVs + up to 16 interpolated
  float4s total, validated at compile.
- *Instances of Phase-1 per-input overrides?* Migrated to named parameters (§6).

## Calls I made (for review)

- Domains in this project: Surface (all five blend modes) and Post Process. Decals,
  UI, light functions, volumes are later projects.
- Shading models added: Subsurface, Clear Coat, Two-Sided Foliage (cloth, hair, eye,
  thin translucent later).
- Translucency is forward-lit per pixel with the same lighting functions as the
  deferred pass; sorting is per object.
- Separate sampled-image + sampler tables replace combined samplers.
- shaderc from the Vulkan SDK as the runtime compiler; cache under `Saved/ShaderCache`.
- A failing material keeps rendering its last good version; first compile shows the
  Default Material.

## Left to the builder

Exact parameter-block packing (std430 rules), preshader opcode encoding, worker-pool
size, pipeline eviction policy, template file split, expression class layout.
