# Plugin readiness: what is hard-coded today, and what we fix before the agent starts

**Status:** Draft — waiting for the owner's approval
**Written:** 2026-11-21 by lead (from a full read of the repository on `feature/material-graph` / `dev`)
**Related:** `AgentTasks/T-0005-core-extensibility.md` (our fixes), `AgentTasks/T-0006-plugin-framework.md` (the agent's task),
task package `Luma_Plugins_and_Physics_Agent_Tasks/` (the colleague's brief)

The plugin agent's P0 audit was right: Luma has **no plugin system**, and several core systems
list their contents **in code**, so a plugin cannot add anything to them without editing the core.
The agent is not allowed to refactor core systems on its own (package rule: "no unilateral refactor of
core ECS"), so it would stop and ask for permission at every one of them. This document lists each
blocker, decides who fixes it, and answers every permission question up front.

Rule for everything below: **production implementation only.** No demo code, no stubs that report
success, no "phase 1 now, real version later" placeholders, no hard-coded lists replaced by a
different hard-coded list. Every hook is complete, tested and documented when it lands.

---

## 1. Blockers found in the code

| # | Hard-coded today | Where | Why it blocks plugins | Fix | Who |
|---|---|---|---|---|---|
| B1 | The scene serializer writes and reads a fixed list of 7 components | `Engine/Scene/src/SceneSerializer.cpp:64-70` (save), `:98-105` (load) | A plugin component is never saved. | Serializer walks a runtime **Component Registry** | Us (T-0005) |
| B2 | Unknown component keys are **dropped** on load, so the next save deletes them | `SceneSerializer.cpp` load loop; documented in `SceneSerializer.h` | Disabling a plugin (or opening a scene on a machine without it) destroys the scene's data. | Keep unknown components as raw JSON per entity and write them back unchanged | Us (T-0005) |
| B3 | Component reflection is compile-time only (`GetTypeInfo<T>()` specializations), with no runtime list | `Engine/Scene/include/Luma/Scene/ComponentReflection.h`, `src/ComponentReflection.cpp` | Nothing can ask "which components exist?", which is needed for serialization, the Inspector and plugins. | Component Registry built on the existing `TypeInfo`; built-ins register like plugins will | Us (T-0005) |
| B4 | The Inspector's **Add Component** menu is a fixed list | `Editor/panels/Inspector/src/Inspector.cpp:63-…` (`BuildAddComponentMenu`) | Plugin components can't be added by the user. | The menu comes from the registry (category, icon, tooltip, one-per-entity / one-per-scene rules) | Us (T-0005) |
| B5 | The Inspector draws every component with hand-written code in one large `Draw` | `Inspector.cpp:204-…` | Plugin components can't show their fields. | A reflection-driven property drawer for registered components; built-ins can keep custom drawers through the same registry hook | Us (T-0005) |
| B6 | Scene setup and the outliner special-case the built-in types (`emplace<LightComponent>`, `all_of<EnvironmentComponent>`, outliner icons) | `Editor/LumaEditor/EditorScreen.cpp:183,565,771-823`; `Editor/panels/WorldOutliner/src/WorldOutliner.cpp:86-110,343` | Plugins can't set the outliner icon or one-per-scene rules for their components. | The registry carries icon, singleton rules and "Create menu" entries; the outliner asks the registry. (`Scene::DuplicateEntity` is already generic.) | Us (T-0005) |
| B7 | The main menu bar is `{"File","Edit","Assets","Window"}` with a `switch` per index | `EditorScreen.cpp:1535`, `BuildMainMenu` `:1923` | Plugins can't add menu entries or open their panels. | An **editor menu registry**: menus and sections by id; built-in entries registered the same way | Us (T-0005) |
| B8 | Dock tabs are a fixed set of panels drawn by name | `EditorScreen.cpp:~1150-1180` | Plugins can't add panels. | An **editor panel (tab) registry**: id, title, icon, draw callback, default dock slot, open/close from the Window menu | Us (T-0005) |
| B9 | The Project Settings window is **never opened**, and its `SaveSettings`/`LoadSettings` return `true` without touching disk. Its rendering pages edit a struct nothing reads. | `Editor/panels/ProjectSettings/src/ProjectSettings.cpp:469-478` | The plugin UI belongs here, and it can't sit on a fake save. | Open it from **Edit → Project Settings**. Real persistence to `Config/` through the serialization module. A **settings-page registry** so plugins (and Plugins itself) add pages. Rendering pages: see §3 Q9. | Us (T-0005) |
| B10 | The `.luma` reader extracts three strings by text search (ad hoc JSON), plus an INI fallback | `Engine/Project/src/Project/Project.cpp:72-215` | It can't store a plugin list, and it would lose keys it doesn't know. | Parse/write `.luma` with `Luma::Serialization` (`SerialValue`); keep unknown keys; add a `fileVersion` with migration of today's two formats | Us (T-0005) |
| B11 | Asset types are a closed `enum class AssetType` with `switch` statements in 5 files | `Engine/Asset/include/Luma/Asset/AssetType.h:14`, `AssetType.cpp` (35 cases), `AssetMetadata.cpp`, `ContentBrowser.cpp`, `ImportSession.cpp`, `ImportPlan.cpp` | Physics needs new asset kinds (Physics Material, cooked collision). Plugins can't add any. | Asset type registry (stable string id + extension + icon + thumbnail/import hooks). Built-ins register; the enum stays only as built-in ids. | Us (T-0005); **T-0003's area is done, so no conflict** |
| B12 | No engine-level update loop for runtime systems (no "systems" list, no play-in-editor; Play/Pause/Stop are disabled buttons) | `EditorScreen.cpp:1598-1606` | Physics has nowhere to tick, and no authoring-scene isolation. | (a) **Scene system registry** with fixed-step and variable-step phases. (b) **Play-in-Editor**: copy the scene, tick systems, restore on Stop. | Us (T-0005) — PIE is a core editor feature, not a plugin |
| B13 | No public debug-draw API (the overlay line pass is internal to Vulkan) | `Engine/Rendering/Vulkan/src/Vulkan/Overlay/` | Physics debug draw can't reach the viewport without touching lead-owned rendering. | RHI `DebugDraw` interface (lines, boxes, spheres, capsules; per-frame, depth on/off) | **Lead** (rendering owner), in T-0005 |
| B14 | The engine version exists only as `project(VERSION 0.1.0)` in CMake and the `.luma` "engineVersion" string | `CMakeLists.txt:3` | Manifests need a compatibility range checked against a real engine version. | Generated `Luma/Core/Version.h` (major/minor/patch, from CMake) + semver compare helper | Us (T-0005) |
| B15 | No editor undo system (T-0002 is still Draft) | — | Physics component edits must be undoable (package F6). | Not a plugin blocker. Plugin components get undo for free once T-0002 lands on the registry. **Scheduled after T-0005, before physics F6.** | Separate task (T-0002) |
| B16 | Lua source is vendored (`ThirdParty/lua-5.5.1`) but nothing builds or binds it | — | Package asks for "Lua hooks only through the existing bridge". There is none. | **Out of scope** for both tasks. Lua bindings come with the scripting task. Plugins expose C++ APIs only. | Later task |
| B17 | No packaging pipeline (only `Runtime/Sandbox` builds) | — | Plugin P5 asks for packaged games with plugins on/off. | P5 does the **build-level** separation (runtime vs editor plugin targets, Sandbox links enabled runtime plugins). Packaging itself is reported **BLOCKED** honestly. | Agent (T-0006) |

Also found, not a blocker but must be handled:
- **Uncommitted work in the main checkout** touches `EditorScreen.cpp`, `main.cpp`, `Inspector.cpp`,
  `ProjectSettings.cpp`, `WorldOutliner.cpp`, `ContentBrowser.cpp` and Slate files (icon-atlas work).
  It must be committed and pushed to `dev` **before T-0005 starts**, or every fix above conflicts with it.

## 2. Split

1. **Owner:** commit and push the pending icon-atlas work to `dev`.
2. **T-0005 Core Extensibility (us: lead + owner):** B1–B14 (B13 by the lead). The core is
   registry-driven when this lands, and every built-in uses the same registries plugins will use.
3. **T-0006 Plugin Framework (agent):** manifests, discovery, dependency resolution, lifecycle,
   per-project enablement, Project Settings → Plugins, static plugin targets, sample plugins, docs.
   It builds **on** T-0005's registries and never edits their internals.
4. **T-0007 NVIDIA PhysX (agent, later):** stays Draft until T-0006 is Accepted and the SDK decisions in §4 are made.

## 3. Permission questions the agent would ask, answered now

| Q | Question | Answer |
|---|---|---|
| Q1 | Static plugins or dynamic DLLs? | **Static plugins** (compiled-in CMake targets, discovered by manifest, enabled per project, started and stopped by the plugin manager). Dynamic DLL loading is **not approved**. It needs an ABI design (C ABI boundary, allocator ownership, version handshake) reviewed by the owner first. Design the manager so a dynamic loader can be added later behind the same interface. |
| Q2 | Enable/disable live or restart? | **Restart required.** The toggle saves the project state; the change applies on the next editor launch. No unload with live components. |
| Q3 | Where do manifests live? | `Plugins/<PluginId>/<PluginId>.lplugin` (JSON) under the **engine** root (engine plugins) and under the **project** root (project plugins). Project plugins override nothing. A duplicate id is an error. |
| Q4 | Where is the per-project enabled state stored? | In the `.luma` file under `"plugins"` (schema-versioned), after T-0005 makes `.luma` a real JSON document. |
| Q5 | May the agent edit `Engine/Scene`, `Engine/Project`, `Editor/panels/Inspector`, `Editor/panels/ProjectSettings`, `Editor/LumaEditor`? | **Only through the registries T-0005 adds**, plus one call site each where the plugin manager starts and stops (`Editor/LumaEditor/main.cpp`/`EditorScreen.cpp` startup/shutdown, `Runtime/Sandbox/main.cpp`). Anything else needs a Question in the task file. |
| Q6 | Which extension points to implement? | Exactly the T-0005 registries: components, editor panels, menu entries, Project Settings pages, asset types, scene systems, import translators (`TranslatorRegistry` already exists — make it extendable). **Deferred and documented:** scripting bindings, render features, custom RHI. |
| Q7 | New third-party libraries? | None needed for T-0006. Never download SDKs or binaries without the owner. |
| Q8 | Packaging? | Build-level separation only; packaging reported **BLOCKED** (no pipeline exists). |
| Q9 | The decorative Rendering pages in Project Settings? | T-0005 **removes** the fake Rendering pages (nothing reads them; rendering settings live in the scene's Environment / Post Process components). Real pages come back when a system actually reads them. |
| Q10 | Branch and merge? | Work on `agent/T-0006-plugin-framework` from `dev` after T-0005 is merged. Push that branch only. The lead reviews, then merges to `dev`; only the lead merges `dev` → `main`. |
| Q11 | Security/trust? | Native plugins run with full engine privileges. Discovery only reads manifests and never executes code. Document the trust boundary. |

## 4. Decisions still needed from the owner (physics, T-0007)

1. **Backend:** NVIDIA PhysX 5 (open source, BSD-3, `github.com/NVIDIA-Omniverse/PhysX`) as the
   colleague's brief asks. (GOAL.md 3.4 suggested Jolt; the brief overrides it.) Confirm.
2. **SDK provisioning (our side):** we add PhysX at a **pinned release tag** as a git submodule
   under `ThirdParty/PhysX`, and the agent writes the CMake integration. The alternative is a prebuilt
   SDK in a fixed path. Confirm which, and the tag.
3. **GPU features** (particles/PBD, deformable surfaces/volumes) need CUDA and an NVIDIA GPU.
   Confirm the test machine's GPU and whether the CUDA toolkit is installed. Otherwise those phases are
   **CONDITIONAL**, and the agent must not fake them.
4. **Blast:** check that an official Blast release builds against the pinned PhysX 5 version
   before approving F8.
5. **Units and axes:** Luma is Y-up (matrices in `TransformComponent::Matrix`), with units to be
   confirmed as metres; PhysX uses the same conventions, so no conversion layer is expected.
