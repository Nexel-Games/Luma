# Luma Engine — Build Stack

The checklist we tick off as the engine grows into a AAA engine. Each item is marked from the
**actual code**, not from plans.

**Legend:** ✅ Implemented (works, tested) · 🟡 Partial (exists but incomplete, unwired or demo-level) · ❌ Not implemented
**Last audit:** 2026-11-23, local `main` @ `f7d5cb6d` (local `main` is 22 commits ahead and 4 behind `origin/main`)
**Rule:** change a mark only when the code, tests and docs are in. If it works but something is missing, it's 🟡, not ✅.
**Relation to `GOAL.md`:** `GOAL.md` is the phase-by-phase roadmap. This file is the system-by-system scorecard.

---

## 0. Fix first: foundation problems that get more expensive the longer they stay

These matter more than any new feature. Each one is a decision that the rest of the engine will build on
top of. The later it's fixed, the more code has to change.

| # | Problem found | Why it matters later | Fix |
|---|---|---|---|
| F1 | **No standalone runtime.** `Runtime/Sandbox` is a Milestone-1 demo layer (logs keys, closes on Escape): it can't load a project, scene or renderer. Only the editor can run a scene. | A game can't ship. Every system written so far assumes the editor exists, and untangling that later touches everything. | A real **Luma Runtime** executable: load project → scene → systems → render loop, no editor code linked. Make it build in CI from day one. |
| F2 | **The editor owns the runtime.** `EditorScreen.cpp`, `MaterialPreview.*` and `main.cpp` include and drive `VulkanDeferredRenderer` directly. | Breaks "editor is a client of engine services". The runtime would have to copy editor code, and a second graphics backend would mean rewriting the editor. | A renderer service behind the RHI (`SceneRenderer`/`Renderer` interfaces only), used by both editor and runtime. No editor file includes `Luma/Rendering/Vulkan/*`. |
| F3 | **Dead/placeholder rendering modules.** `LumaRenderGraph`, `LumaShader` (old `glslc` shell-out) and `LumaUniformBuffer` are built but linked by nothing. `RHI.h` has free functions "implemented once the Vulkan backend is complete". | Two half-designs of the same thing. New code may build on the wrong one, and placeholders break the no-stub rule. | Decide the real **render graph** (wire it into the deferred renderer, or delete it and write the real one). Delete `LumaShader`/`LumaUniformBuffer`/stub `RHI.h` functions. |
| F4 | **No editor-wide Undo/Redo** (only the graph editor has an `UndoStack`). T-0002 is still Draft. | Every editor feature added without transactions has to be retrofitted. That gets worse with each feature. | Transaction system (T-0002) on the reflection/registry layer, used by the Inspector, outliner, gizmos and plugins. |
| F5 | **No input abstraction:** raw GLFW keycodes, no Luma key enum, no actions/axes/contexts, no gamepad. | Gameplay, editor shortcuts and the runtime would all hard-code keys. | Input system: Luma key/button enums, action & axis mappings, contexts (editor/play), gamepad, rebinding. |
| F6 | **Single-precision world coordinates** (`Math::Vec3` floats everywhere). | Big worlds jitter past ~5–10 km. Switching later touches transforms, physics, rendering and serialization. | Decide now: double-precision world positions (camera-relative rendering) or floats with world origin rebasing. Write it into the transform/scene design before physics lands. |
| F7 | **No crash handling, no CI.** No minidump/crash reporter. No `.github/workflows`: tests only run when someone runs them. | Regressions land unnoticed, and crashes in the field leave nothing to debug. | CI (build all configs + full test suite on every push/PR) and a crash handler (minidump + log + "crash folder" message). |
| F8 | **Plugin/module system not finished** (registries exist; the framework is in progress with the agent, T-0006). | Systems written now as hard-wired code would need porting into modules later. | Finish T-0006 (full Unreal model: descriptors, modules, phases, DLL editor builds), then build new systems (physics, audio, AI...) as modules. |
| F9 | **Assets load synchronously; no cooking or packaging.** | Load hitches and no shippable data format. Streaming bolted on later changes every loader. | Async loading through the job system with handles/futures, and a cook step (source → runtime formats) before packaging. |

---

## 1. Core Engine Framework

| Item | Status | Evidence / gap |
|---|---|---|
| Engine lifecycle / application loop | 🟡 | `Core/Application`, `EngineLoop`, `LayerStack`. Editor-centric; no runtime host (F1). |
| Memory: allocators, arenas, pools, tracking | ✅ | `Core/Memory` (`Alloc`, `Arena`, `Pool`, `Track`). |
| Logging + user console | ✅ | `Core/Log`, `LogBuffer`, `LUMA_USER_*` vs engine logs. |
| Assertions | ✅ | `Core/Assert.h`. |
| Configuration (layered: engine → project → user → platform) | 🟡 | `Core/Config` exists. Per-project layering and plugin config layers not complete. |
| Object lifetime & handles | 🟡 | Generational handles in caches (textures, meshes). No engine-wide handle/weak-reference model for gameplay objects. |
| Threading primitives | ✅ | Standard library plus the job system. |
| Versioning (engine version, semver) | ✅ | `Core/Version.h`, `SemVer`. |
| Crash handling / minidumps | ❌ | F7. |

## 2. Rendering Architecture

| Item | Status | Evidence / gap |
|---|---|---|
| RHI (backend-independent) | 🟡 | `Rendering/RHI` interfaces exist, but the editor uses the Vulkan renderer directly (F2) and `RHI.h` has stub functions (F3). |
| Vulkan backend | ✅ | `Rendering/Vulkan` (deferred renderer, caches, passes). |
| Other backends (DX12/Metal) | ❌ | Not started (later phase). |
| Render graph | 🟡 | `LumaRenderGraph` exists but is unused (F3). Passes are hand-sequenced. |
| Runtime shader compilation + cache | ✅ | `Luma::ShaderCompiler` (shaderc, async, disk cache). |
| Material system (node graph → GLSL, instances, preshaders) | ✅ | Material compiler + editors (PR #6 graph editor). |
| PBR deferred lighting, sky atmosphere, sky light IBL | ✅ | `deferred_lighting.frag`, `lighting_common.glsl`, sky light passes. |
| Shadows (CSM, PCSS) | ✅ | Sun cascades; cached shadow maps. |
| Local light shadows (point/spot) | ❌ | Only sun shadows. |
| Post (bloom, auto exposure, tone mapping, FXAA) | ✅ | Bloom, eye adaptation, FXAA. |
| TAA / temporal upscaling (FSR/DLSS) | ❌ | |
| Translucency, post-process materials, shading models | 🟡 | Per the material plan: verify Tasks 14–16 status on the branch before ticking. |
| Visibility: frustum culling | ✅ | `FrustumCulling.h`, scene draw culling. |
| Visibility: occlusion culling (Hi-Z) | ❌ | |
| LOD (automatic mesh LODs) | ✅ | Background-built, cached LODs. |
| GPU resource management (residency, budgets) | 🟡 | Caches with release/GC. No memory budgets or texture streaming. |
| Global illumination / ray tracing / virtualized geometry | ❌ | Later phase. |
| Debug draw API | ✅ | RHI `DebugDraw` (lines, boxes, cones, frustums). |
| GPU profiling | ✅ | `VulkanGpuTimer`, profiler. |

## 3. Entity & Component Architecture

| Item | Status | Evidence / gap |
|---|---|---|
| ECS world (EnTT) | ✅ | `Engine/Scene`. |
| Component registry + reflection-driven serialize/inspect | ✅ | `ComponentRegistry`, `TypeInfo`, `InspectorDrawers`. |
| Unknown-component preservation (missing plugins) | ✅ | `UnresolvedComponents` (T-0005). |
| Transform hierarchy (parent/child) | ✅ | `Hierarchy.h` (`HierarchyComponent`, `SetParent`). |
| Scene systems + fixed-step runner | ✅ | `SceneSystems.h`, `SceneRunner`. |
| Play-in-Editor with authoring-state isolation | 🟡 | `BeginPlay` exists. Verify Stop fully restores the scene and that it's tested. |
| Prefabs / instancing | ❌ | Only an asset-type entry. |
| Entity templates ("Create" menu) | ✅ | `EntityTemplates()`. |

## 4. Physics & Collision

| Item | Status | Evidence / gap |
|---|---|---|
| Physics abstraction + backend (PhysX plugin, T-0007) | ❌ | Plan only. Blocked on the plugin framework and SDK decisions. |
| Rigid bodies, colliders, CCT, queries, joints | ❌ | |
| Fixed timestep hook | ✅ | `SceneRunner` fixed step exists for physics to plug into. |

## 5. Multithreading & Job System

| Item | Status | Evidence / gap |
|---|---|---|
| Job system (workers, priorities, dependencies, ParallelFor, main-thread jobs) | ✅ | `Engine/Jobs`. |
| User-visible task manager (progress/cancel) | ✅ | `Engine/Tasks`. |
| Frame task graph (whole frame scheduled as jobs: game → physics → anim → render extraction) | ❌ | The frame runs sequentially on the main thread. Jobs are used for background work (~20 call sites). |
| Render thread / render extraction | ❌ | Rendering records on the game thread. |

## 6. Asset & Resource Management

| Item | Status | Evidence / gap |
|---|---|---|
| Asset registry with stable IDs | ✅ | `AssetId`, registry, path index. |
| Dependency tracking | 🟡 | `AssetDependencyManager` exists. Verify coverage (materials ↔ textures ↔ meshes, rename fix-ups). |
| Importers (models, textures, audio, HDR/EXR/PSD) | ✅ | `Engine/AssetImport` (assimp, stb, tinyexr), Import window. |
| Asset type registry (extensible) | ✅ | `AssetTypes()` (T-0005). |
| Thumbnails (async) | ✅ | Worker-thread thumbnails. |
| Async loading (handles/futures, priorities) | ❌ | F9. |
| Cooking (source → runtime formats) | ❌ | F9. |
| Streaming & memory residency budgets | ❌ | |
| Hot reload of changed source files | 🟡 | `SourceWatch` re-imports textures/meshes. Not general. |

## 7. World & Scene Management

| Item | Status | Evidence / gap |
|---|---|---|
| Scene save/load (versioned, migration) | ✅ | `SceneSerializer` (versioned). |
| Level transitions / multiple scenes | 🟡 | One active scene. No transition or loading flow. |
| Spatial indexing (BVH/octree) | ❌ | Needed for culling, picking and queries at scale. |
| World partition / streaming | ❌ | |
| Large-world coordinates | ❌ | F6. |

## 8. Gameplay & Scripting

| Item | Status | Evidence / gap |
|---|---|---|
| Native gameplay API (stable, not engine internals) | ❌ | Gameplay would go through engine internals today. |
| Scripting language | ❌ | Lua 5.5.1 vendored in `ThirdParty/` but not built or bound. |
| Visual scripting | ❌ | The generic `Editor/GraphEditor` module can host it later. |
| Gameplay events / messaging | 🟡 | Core event system for platform events. No gameplay event bus. |
| Game framework (game mode, player controller, camera manager) | ❌ | |

## 9. Animation

| Item | Status | Evidence / gap |
|---|---|---|
| Skeletal mesh import | 🟡 | `importSkeleton` option and bone-weight layout reserved. Skeleton/skin data not used at runtime. |
| Skinning, animation clips, blend trees/graphs, IK, root motion, retargeting | ❌ | |

## 10. Audio

| Item | Status | Evidence / gap |
|---|---|---|
| Decode (WAV/MP3) + editor preview + waveform | ✅ | `Engine/Audio` (miniaudio). |
| Runtime audio engine: sources, listener, 3D spatialization, buses/mixer, streaming, effects | ❌ | |

## 11. AI

| Item | Status | Evidence / gap |
|---|---|---|
| Navmesh, pathfinding, behavior trees, perception, crowds | ❌ | |

## 12. Networking

| Item | Status | Evidence / gap |
|---|---|---|
| Transport, replication, authority, prediction, reconciliation, lag compensation | ❌ | Decide the replication model before gameplay code assumes single-player. |

## 13. Module & Plugin Architecture

| Item | Status | Evidence / gap |
|---|---|---|
| Runtime registries (components, systems, assets, menus, panels, settings, inspector) | ✅ | T-0005. |
| Plugin framework (descriptors, modules, phases, Plugins window, wizard, DLL editor builds) | 🟡 | In progress (T-0006, `AgentTasks/PLUGIN_ARCHITECTURE.md`). A preview Plugins page exists on the agent's branch. |
| Engine split into modules with explicit dependencies | 🟡 | Static libs per subsystem with CMake dependencies. DLL/modular builds pending (T-0006 step 5). |

## 14. Editor & Tooling

| Item | Status | Evidence / gap |
|---|---|---|
| Docking UI (Slate), persistent layout | ✅ | `DockSpace`, layout saved per project. |
| Viewport (gizmos, picking, drag-and-drop placement, camera/light gizmos, camera preview) | ✅ | |
| World Outliner (hierarchy, striping, virtualized) | ✅ | |
| Inspector (reflection-driven + custom drawers) | ✅ | |
| Content Browser (thumbnails, folders, moves) | ✅ | |
| Material / Material Instance editors (node graph) | ✅ | Graph editor (PR #6). |
| Project Browser, Project Settings, Preferences | ✅ | |
| Undo/Redo (editor-wide) | ❌ | F4. |
| Profiler UI (CPU/GPU) | 🟡 | Profiler + GPU timer exist. The UI covers part of the plan (no memory/asset-load tracks). |
| Debugger integration, visual logger | ❌ | |
| Editor as a client of engine services | ❌ | F2. |

## 15. Build, Packaging & Deployment

| Item | Status | Evidence / gap |
|---|---|---|
| CMake build, 4 configurations (Debug/Development/Release/Shipping) | ✅ | |
| Automated tests (Catch2, ~750 cases incl. GPU tests) | ✅ | `Tests/`. |
| Continuous integration | ❌ | F7. |
| Shader compilation (build-time glslc + runtime shaderc) | ✅ | |
| Cooking | ❌ | F9. |
| Packaging a game | ❌ | Needs F1 + F9. |
| Platforms beyond Windows | ❌ | GLFW is portable, but only Windows is built and tested. |

## Platform Abstraction (Layer 2)

| Item | Status | Evidence / gap |
|---|---|---|
| Windowing (GLFW) | ✅ | `Engine/Platform`. |
| File I/O / virtual file system | ✅ | `Engine/VFS`. |
| Input (raw keyboard/mouse events) | 🟡 | F5. |
| Gamepad | ❌ | F5. |
| OS services abstraction (paths, processes, dialogs) | 🟡 | Partly in Platform. Some Windows calls live in the editor (e.g. delete only works on Windows). |

---

## Suggested order

1. **Foundation fixes F1–F9** (above), in roughly this order: F3 → F2 → F1 → F7 → F4 → F5 → F6 → F9, alongside F8 (agent).
2. Then the subsystems as plugins/modules: Physics (T-0007) → Audio runtime → Animation → Scripting → AI → Networking.
3. AAA-scale work (frame task graph, render thread, streaming, budgets, GI, virtualized geometry) once 1–2 are stable.
