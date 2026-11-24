# T-0007 — Luma Physics (NVIDIA PhysX 5.13) + Blast destruction plugin

**Status:** Approved (owner asked for it 2026-11-24; SDK is in the repo)
**Assigned to:** engine agent
**Created:** 2026-11-24 by lead
**Branch:** `agent/T-0007-physics` from the latest `dev`
**Area / files:** new `Engine/Physics/**`, new `cmake/LumaPhysX.cmake`, new `Plugins/LumaBlast/**`,
new `Plugins/LumaFlow/**` (phase 9 only), new `Tests/Physics/**`, new `docs/physics/**`, new files under
`Editor/panels/ProjectSettings/` (the Physics page), new Inspector drawers registered through
`InspectorDrawers()`, and the call sites listed in §8. **Never edit anything under `ThirdParty/PhysX/`.**
If the SDK needs a patch, ask in *Questions*.

---

## 1. Standard

Same standard as T-0006, and it applies here in full:

- **Production implementation, no shortcuts.** No demo code, no placeholders, no "TODO later", no
  component that serialises but does nothing in Play, and no UI that shows state the simulation
  doesn't have.
- **A phase is COMPLETE only when its code, tests, docs and manual check are all done.** Otherwise
  report it as PARTIAL or BLOCKED, with the reason and evidence (real command output).
- **TDD.** Write the failing test first and watch it fail. The whole suite stays green before every push.
- **Never fake a GPU feature.** If a GPU feature can't run, the component says so clearly in the
  Inspector and in a `LUMA_USER_WARN`. It never silently does nothing.
- **Never download anything** (SDKs, packman packages, Boost, Cap'n Proto…). Everything must build from
  what is in the repository. If something is missing, ask in *Questions*.

## 2. What the user gets

All PhysX features, exposed the way Unreal exposes Chaos/PhysX, in Luma's own style:

1. **Rigid bodies** (static, dynamic, kinematic) and **colliders**: box, sphere, capsule, convex mesh,
   triangle mesh (static only), height field later via terrain. Also physics materials, collision
   layers, triggers, CCD, sleeping, axis locks, mass/centre of mass/inertia overrides.
2. **Scene queries**: raycast, sweep, overlap, with layer filtering. **Collision and trigger events.**
3. **Joints**: fixed, hinge (revolute), prismatic, spherical, distance, D6 (configurable), with limits,
   drives/motors and break force. **Articulations** (reduced coordinate) for ragdolls and mechanisms.
4. **Character controller** (capsule/box kinematic controller): step offset, slope limit, pushing
   bodies, grounded state.
5. **Vehicles** (PhysX Vehicle SDK, `physx/include/vehicle`, the component-based "vehicle2" API):
   - wheel colliders with suspension, tyres, brakes and steering (Ackermann)
   - engine, gearbox, clutch and differential (FWD / RWD / 4WD), plus direct-drive and tank drives
6. **GPU (CUDA) features**, on NVIDIA GPUs: **PBD particle fluids** (liquids, with isosurface
   extraction for rendering), **cloth** (deformable surfaces), **soft bodies** (deformable volumes),
   with attachments to rigid bodies.
7. **Wind**: global directional wind and local wind zones (sphere / box), with gusts and turbulence.
   Wind pushes rigid bodies (drag), cloth and particles.
8. **Blast destruction** as a plugin, **disabled by default**, which developers enable in the
   Plugins window: fracture authoring (Voronoi / slicing), destructible actors, damage, stress solver.
9. **Flow** (smoke, fire, gas) as a second opt-in plugin, **only after** the lead adds the renderer
   hook (see phase 9).
10. Editor: the **Physics Project Settings page**, collider gizmos, the physics debug view, the
    Physics Material asset and editor, and the PhysX Visual Debugger (PVD/OmniPVD) connection in
    Development builds.

## 3. Decisions (already made, don't reopen without a Question)

| Topic | Decision |
|---|---|
| SDK | **PhysX 5.13.0** (`ThirdParty/PhysX/physx/version.txt`), **vendored** in the repo (not a submodule). Blast **5.0.6** is at `ThirdParty/PhysX/blast`, Flow at `ThirdParty/PhysX/flow`. `schemas/` (USD) is **not used**; the package's `ovphysx/` (Omniverse USD/Python tool) was left out of the repo. |
| Build | `cmake/LumaPhysX.cmake` builds PhysX from source with CMake: `add_subdirectory(ThirdParty/PhysX/physx/compiler/public …)` or the FetchContent-style entry described in `physx/documentation/fetchcontent/FetchContent.md`, with **`FETCHCONTENT_FULLY_DISCONNECTED`/no network**. Static PhysX libs; **`/MD` CRT** to match Luma (`CMAKE_MSVC_RUNTIME_LIBRARY` is MultiThreadedDLL). Snippets, samples and tests are **off**. If PhysX's CMake insists on packman downloads, write the minimal target list yourself from `physx/source/*` (the source folders map 1:1 to libraries) and record it in the Result. |
| GPU | Option **`LUMA_PHYSX_GPU`** (default **ON when `find_package(CUDAToolkit)` finds CUDA ≥ 12.8**, otherwise OFF). Owner's machine: **RTX 5060, driver 616.56, CUDA 12.8**, so GPU features must be built and tested there. At runtime, GPU features also need a valid `PxCudaContextManager`. Without one, the CPU path runs and GPU components report "requires an NVIDIA GPU (CUDA)". |
| Module | **`Engine/Physics` → `Luma::Physics`**, always on (core engine system, like Chaos in Unreal). **PhysX headers are PRIVATE**: no `Px*` type in any public Luma header. The public API uses `Math::Vec3/Quat/Mat4`, `Entity` and Luma enums. |
| Units / axes | Metres, kilograms, seconds, **Y-up**, gravity default `(0, -9.81, 0)`. `TransformComponent` rotation is Euler degrees applied **Y·X·Z** (see `TransformComponent::Matrix`). Write `ToQuat(euler)` / `ToEuler(quat)` with a round-trip test. |
| Simulation | A **scene system** registered through `SceneSystems()` (`ISceneSystem`): `OnPlayBegin` builds the PhysX scene from the play copy, `FixedUpdate(dt)` steps (`SceneRunner` already does fixed steps from `SimulationSettings`), and `OnPlayEnd` releases everything. Physics runs **only in Play / Simulate**, never on the authoring scene. |
| Transforms | Bodies read and write **world** transforms. A child entity with a collider and **no** rigid body of its own becomes a shape on its nearest ancestor body (compound collider, the Unreal behaviour). Scale is baked into shape geometry (non-uniform scale on spheres/capsules uses the max axis; document it). |
| Threads | Implement **`PxCpuDispatcher` on top of `Luma::Jobs::JobSystem`** (one thread pool for the engine). Use `PxDefaultCpuDispatcher` only in tests that have no job system. |
| Assets | **Physics Material** asset `.lphysmat` (JSON: static friction, dynamic friction, restitution, friction/restitution combine modes, density), registered through the asset-type registry. **Collision cooking** (convex/triangle) is cached under the project's `Saved/PhysicsCache/` and keyed by mesh file hash + cooking params, like the LOD cache (`Engine/Rendering/Mesh/MeshLodCache.h`). |
| Layers | 32 collision layers with names and a symmetric collide/ignore matrix, stored in project config (Physics page). Filtering uses `PxFilterData` word0/word1 plus a custom filter shader. Trigger shapes report enter/exit only. |
| Blast | **Plugin `Plugins/LumaBlast`**, `"EnabledByDefault": false`, `"Category": "Physics"`, `"CreatedBy": "Brickstar Games"`. It depends on `Luma::Physics`' public API only (never on PhysX headers through Luma). Blast's **Cap'n Proto serialization extension is not used**: Luma stores `NvBlastAsset` (a flat, relocatable memory block) in its own `.ldestructible` file. Blast's authoring mesh cleaner needs **Boost.Multiprecision**, which is **not** in the repo: ask in *Questions* before using that file. Everything else in authoring (Voronoi, slicing, convex decomposition) should build without it. Verify that and report. |
| Flow | Plugin `Plugins/LumaFlow`, disabled by default. **Phase 9 is blocked** until the lead adds an RHI hook for third-party compute/volume rendering (`Engine/Rendering` is lead-owned). Write your proposed RHI interface in *Questions* when you reach it. |
| Rendering deformables | Cloth, soft bodies and fluid surfaces need **dynamic vertex data** each frame. `Engine/Rendering/**` is lead-owned. Propose the RHI entry point you need (e.g. `Renderer::UpdateDynamicMesh(handle, span<MeshVertex>)`) in *Questions* **at the start of phase 6**, and work on the simulation side while the lead adds it. |

## 4. Components (public, serialised, reflected, Inspector-drawn)

All of these are registered through `Components()` / the T-0005 registries, round-trip through the scene
file, and show in the Inspector with their own drawer where reflection alone is not enough.

| Component | Key fields |
|---|---|
| `RigidBodyComponent` | `type` (Static / Dynamic / Kinematic), `mass` (0 = from density), `linearDamping`, `angularDamping`, `enableGravity`, `ccd` (Off / Swept / Speculative), `lockPosition` xyz, `lockRotation` xyz, `startAwake`, `maxAngularVelocity`, `centerOfMassOffset`, `solverIterations` (pos/vel) |
| `BoxColliderComponent` | `halfExtents`, `center`, `rotation`, `material` (/Game path), `isTrigger`, `layer` |
| `SphereColliderComponent` | `radius`, `center`, `material`, `isTrigger`, `layer` |
| `CapsuleColliderComponent` | `radius`, `halfHeight`, `axis` (X/Y/Z), `center`, `material`, `isTrigger`, `layer` |
| `MeshColliderComponent` | `mesh` (empty = the MeshRenderer's mesh), `convex` (bool; triangle meshes allowed only on static/kinematic bodies, otherwise a clear Inspector error), `material`, `isTrigger`, `layer` |
| `JointComponent` | `kind` (Fixed / Hinge / Prismatic / Spherical / Distance / D6), `connectedEntity` (empty = world), anchors, axis, limits, drive (target, stiffness, damping, max force), `breakForce`, `breakTorque`, `collideConnected` |
| `ArticulationComponent` + `ArticulationLinkComponent` | root/links built from the entity hierarchy, joint type per link, limits, drives |
| `CharacterControllerComponent` | `shape` (Capsule/Box), `radius`, `height`, `stepOffset`, `slopeLimitDeg`, `skinWidth`, `minMoveDistance`, `layer`, `pushForce` |
| `VehicleComponent` | drive type (Engine / Direct / Tank), engine torque curve, max RPM, gear ratios, final drive, clutch, differential (FWD/RWD/4WD + split), Ackermann params, chassis mass/inertia |
| `WheelColliderComponent` (child of the vehicle) | radius, width, mass, MOI, suspension travel, spring strength, damper, camber, tyre lateral/longitudinal stiffness and friction-vs-slip, max steer, max brake/handbrake torque, `driven` |
| `ClothComponent` (GPU) | source mesh, mass, stretch/bend/shear stiffness, damping, thickness, self-collision, pinned vertices (painted or by vertex-colour channel), attachment to parent body |
| `SoftBodyComponent` (GPU) | source mesh, Young's modulus, Poisson ratio, damping, resolution, attachments |
| `FluidEmitterComponent` (GPU) | shape (box / sphere / nozzle), rate, initial velocity, particle spacing, viscosity, cohesion, surface tension, max particles, render as isosurface/points |
| `WindZoneComponent` | `mode` (Directional / Sphere / Box), `direction`, `strength`, `radius`/`extents`, `falloff`, `gustStrength`, `gustFrequency`, `turbulence` |

Runtime API (C++, `Luma/Physics/PhysicsWorld.h`), reached from the play scene:
`AddForce/AddImpulse/AddTorque/SetLinearVelocity/GetLinearVelocity/SetKinematicTarget/WakeUp/Sleep`,
`Raycast/Sweep/Overlap` (results: entity, point, normal, distance, layer),
`CharacterMove(entity, displacement, dt) → {collisionFlags, grounded}`,
`VehicleInput(entity, {throttle, brake, steer, handbrake, gear})`,
`ApplyRadialDamage(center, radius, damage)` (Blast plugin), and
the event queues `ContactEvents()` / `TriggerEvents()` (drained each frame).

## 5. Phases

Each phase ends with: tests green, docs updated (`docs/physics/<phase>.md`), a manual check, a commit,
a push to your branch, and a phase report in **Result**.

### Phase 0 — Build integration
- `cmake/LumaPhysX.cmake`: build PhysX from `ThirdParty/PhysX/physx` (CPU libs + optional GPU),
  static, `/MD`, warnings from PhysX not promoted to errors, no network.
- `Engine/Physics` skeleton: `PhysicsSystem` (foundation, physics, cooking params, dispatcher, optional
  CUDA context), created once by the editor and the Sandbox runtime.
- Tests (`[physics]`): create/destroy the system twice in one process (no leaks: the PhysX foundation
  ref count returns to 0), and the job-system dispatcher runs N tasks.
- **Expected:** `cmake --build build` and `build/static` both link. The editor starts with
  `Physics: PhysX 5.13.0 (GPU: yes, NVIDIA GeForce RTX 5060)` in the developer log.

### Phase 1 — Rigid bodies, colliders, materials, layers, queries, events
- Components above (body + 4 colliders), the Physics Material asset, the layer matrix, the scene system
  and the query/event API.
- **Tests with exact values** (fixed step 1/60, gravity −9.81, default material):
  - a dynamic sphere dropped from y=10, after 60 steps: `y ≈ 10 − 9.81/3600 · 60·61/2 = 5.0133` (±0.02)
  - a box resting on a static plane, after 120 steps: its y stays within 0.01 of its half-height, and it sleeps
  - restitution 1.0 + restitution-combine Max: the bounce apex reaches ≥ 0.9 of the drop height
  - a kinematic body moved by `SetKinematicTarget` pushes a dynamic box
  - layer A ignoring layer B: no contact and no event; triggers send exactly 1 enter + 1 exit
  - raycast down from (0,5,0) onto a unit cube at the origin hits at y=0.5, normal (0,1,0), distance 4.5
  - a compound child collider moves with its parent body
  - Euler↔quaternion round trip within 1e-4 degrees for 1,000 random rotations
  - the scene file round-trips every new component field
- Editor: collider wireframes through `DebugDrawer()` for the selected entity; **Show → Physics** in
  the viewport (PhysX visualization: shapes, contacts, joint frames); the Physics Material editor.
- **Manual:** Play on a scratch copy of `Tests/Fixtures/MaterialShowcase` with a stack of 20 boxes falls
  and settles. Stop restores the authoring scene exactly.

### Phase 2 — Joints and articulations
- All six joint kinds with limits, drives and breaking. Articulations from the hierarchy.
- Tests: a hinge with limits ±45° never exceeds 45.5°; a joint with breakForce 100 breaks under a
  200 N pull and sends a break event; a prismatic drive reaches its target ±1 cm in 2 s; a 3-link
  articulation pendulum conserves energy within 2% over 5 s (no damping).

### Phase 3 — Character controller
- `CharacterControllerComponent` through `PxControllerManager`, with Y-up and Luma layers.
- Tests: walking into a 0.3 m step with stepOffset 0.35 climbs it, and stepOffset 0.25 blocks; a 50°
  slope with limit 45° is not climbable; `grounded` is true after landing; walking into a dynamic
  box with pushForce > 0 moves the box.

### Phase 4 — Vehicles
- Engine drive, direct drive and tank drive on the PhysX vehicle (component sequence) API. Wheel
  colliders are child entities. Raycast or sweep wheel queries against Luma layers.
- Tests: on flat ground, 3 s of full throttle in 1st→auto gears: forward speed > 8 m/s and the
  vehicle stays upright; full brake from 15 m/s stops within the distance from the tyre friction you
  configure (assert < 25 m); steering right yaws clockwise (seen from +Y); a 4WD split sends torque to
  all 4 wheels.
- **Manual:** a vehicle prefab with 4 wheel colliders drives in Play, controlled from a test-only input
  binding that the editor exposes under **Tools → Physics → Drive Selected Vehicle** (it reads the
  keyboard through Slate; this is a real tool, kept).

### Phase 5 — Wind
- `WindZoneComponent` plus a wind field sampler (`SampleWind(position, time)`). Rigid bodies get
  aerodynamic drag `F = ½·ρ·Cd·A·|v_rel|·v_rel`, with A from the shape bounds and Cd per material
  (default 1.0, ρ = 1.225).
- Tests: a 1 kg sphere in 10 m/s directional wind accelerates toward +X; a sphere outside a sphere zone
  is unaffected; gusts are deterministic for a fixed seed.

### Phase 6 — GPU: particle fluids, cloth, soft bodies (requires `LUMA_PHYSX_GPU`)
- PBD particle system fluids (`PxPBDParticleSystem`, `PxPBDMaterial`) with isosurface extraction
  (`PxIsosurfaceExtraction`) for rendering. Cloth = `PxDeformableSurface`; soft bodies =
  `PxDeformableVolume`. Attachments to rigid bodies. Wind feeds cloth/particles.
- **First step:** ask in *Questions* for the RHI dynamic-mesh entry point (see §3).
- Tests (tagged `[physics][gpu]`; they **skip with a printed reason** when no CUDA context exists, and
  never pass silently): 1,000 particles in a closed box settle below half the box height in 3 s; a
  cloth square pinned at two corners hangs, and its lowest vertex is below the pins after 2 s; a soft
  cube dropped on the ground deforms and recovers to within 5% of its rest volume.

### Phase 7 — Blast destruction plugin (`Plugins/LumaBlast`, disabled by default)
- CMake for Blast sources: `lowlevel`, `globals`, `toolkit`, `extensions/{authoring, authoringCommon,
  assetutils, stress, shaders}`. **Not** `serialization` (Cap'n Proto).
- Authoring: a **Fracture** tool in the static-mesh asset context menu: Voronoi with N cells, uniform or
  clustered, plus slicing. It writes `.ldestructible` (Blast asset + chunk meshes + collision).
- `DestructibleComponent` on an entity: chunks become PhysX actors through `Luma::Physics` only.
  Impact damage from contact events, plus `ApplyRadialDamage`, plus the stress solver for
  structures.
- Tests: fracturing a unit cube into 8 Voronoi cells yields 8 support chunks whose volumes sum to
  1.0 ± 1%; radial damage above the health threshold splits the actor into ≥ 2 actors; with the
  plugin disabled the component shows "missing plugin — data kept" (T-0006 behaviour) and the scene
  still loads.
- **Manual:** enable **LumaBlast** in the Plugins window (it is off by default), restart, fracture
  a cube, Play, and shoot it with the radial-damage test tool.

### Phase 8 — Editor polish and PVD
- Physics Project Settings page: gravity, layers + matrix, solver type (PGS/TGS), default
  iterations, bounce threshold, CCD, enhanced determinism, GPU dynamics on/off (greyed out without
  CUDA), PVD host/port.
- `--pvd` connects the PhysX Visual Debugger in Development builds.
- `docs/physics/` user guide: each component, units, layers, GPU requirements, Blast workflow.

### Phase 9 — Flow plugin (smoke / fire), BLOCKED on the lead
- Starts when the lead has added the RHI hook you proposed. Same plugin rules as Blast (disabled by
  default).

## 6. Acceptance criteria

- [ ] Phases 0–8 COMPLETE (phase 6 runs on the owner's RTX 5060). Phase 9 is BLOCKED or COMPLETE, honestly reported.
- [ ] `LumaTests` whole suite green in both `build` (modular DLLs) and `build/static`.
- [ ] 0 Vulkan validation errors in a manual run.
- [ ] No `Px*` type in any public Luma header (grep proof in the Result).
- [ ] LumaBlast is **off** in a new project, and turning it on/off in the Plugins window works with
      the restart prompt.
- [ ] Play → Stop leaves the authoring scene byte-identical (save before and after, diff).

## 7. Out of scope

- Scripting bindings (Lua/Python) for physics: a later task, once the Code Editor scripting runtime exists.
- Network replication of physics.
- Terrain height fields (no terrain system yet).
- Anything in `ThirdParty/PhysX/schemas` (USD schemas; Luma doesn't use USD).

## 8. Call sites you may edit (one line or one block each)

- Top-level `CMakeLists.txt`: `include(cmake/LumaPhysX.cmake)` and `add_subdirectory(Engine/Physics)`.
- `Editor/LumaEditor/main.cpp` and `Runtime/Sandbox/main.cpp`: create/destroy the `PhysicsSystem`.
- `Editor/LumaEditor/EditorScreen.cpp`: register the physics scene system, the Physics settings page,
  the viewport Show → Physics toggle, and the Tools → Physics menu.
- `Editor/LumaEditor/CMakeLists.txt`, `Runtime/Sandbox/CMakeLists.txt`, `Tests/CMakeLists.txt`: link `Luma::Physics`.

## 9. References

- PhysX docs: `ThirdParty/PhysX/physx/documentation`, `physx/SKILLS.md`, and the snippets under
  `physx/snippets/`:
  - rigid bodies and queries: `snippethelloworld`, `snippettriggers`, `snippetcontactreport`,
    `snippetccd`, `snippetconvexmeshcreate`, `snippettrianglemeshcreate`
  - joints and articulations: `snippetjoint`, `snippetjointdrive`, `snippetarticulationrc`
  - vehicles: `snippetvehiclefourwheeldrive`, `snippetvehicledirectdrive`, `snippetvehicletankdrive`
  - GPU fluids, cloth and soft bodies: `snippetpbf`, `snippetisosurface`, `snippetdeformablesurface`,
    `snippetdeformablevolume`, `snippetdeformablevolumeattachment`
  - threading and debugging: `snippetmultithreading`, `snippetomnipvd`

  The character controller has no snippet; use `physx/include/characterkinematic/`.
- Blast: `ThirdParty/PhysX/blast/docs`, `blast/source/test` (how the SDK is driven).
- Luma: `Engine/Scene/include/Luma/Scene/SceneSystems.h`, `ComponentRegistry.h`, `Hierarchy.h`,
  `Engine/Rendering/RHI/include/Luma/RHI/DebugDraw.h`, `Engine/Asset/include/Luma/Asset/LumaMesh.h`
  (`ReadMesh`), `Engine/Rendering/Mesh/include/Luma/Mesh/Mesh.h` (`BuildPrimitive`), `Plugins/SampleSpinner`
  (plugin layout and `luma_add_module`).
- Unreal behaviour to match (described here, no source needed):
  - Colliders on child components merge into the parent body.
  - Physics runs only in PIE/Simulate.
  - Collision channels are a named matrix in Project Settings.
  - Destruction is an optional plugin.

## Questions

_(engine agent)_

## Result

_(engine agent: one report per phase)_

## Review

_(lead)_
