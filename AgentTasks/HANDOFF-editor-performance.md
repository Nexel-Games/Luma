# Hand-off: Editor performance + graphics settings (branch `agent/editor-performance`)

Branch is cut from `origin/dev` (f3e326e6) and pushed. Last commit: `wip(perf): ...` (**does not compile yet**, see Step 0).
Build: `MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*" cmd.exe /c "Scripts\\_env.bat cmake --build build --config Development"`
(run `... cmake --preset msvc` first in a fresh worktree). Tests: `build/bin/Development/LumaTests.exe` — baseline **724 cases green** before this work.
Commit identity: `git -c user.name="DarkKnight390" -c user.email="jmdark.knightstudio@gmail.com"` + trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
Avoid `Engine/Plugins` and `Plugins/` (T-0006 runs elsewhere). Several files are CRLF: edit with exact-match Python scripts that read/write
with `newline=''` (see `AgentTasks/handoff-editor-performance/edit.py`), never bash heredocs.

## Owner's goal
Editor capped at 60 FPS on an empty scene (expects 1000+ uncapped); 6–15 FPS with meshes. Fix properly, expose every perf/quality
setting in Project Settings, persist per project, apply live. **No dead features, no stubs: every setting must really change behaviour.**
New owner requests (2026-11-22):
1. **Project Settings must be a real window styled like the Preferences window** (window icon, the same black divider lines and
   section layout). Look at `Editor/panels/Preferences/` and copy its look for `Editor/panels/ProjectSettings/src/ProjectSettings.cpp`
   (currently a `BeginModal`). Turn Preferences "inside out" into the same window style too if it is not already a window.
2. **Write full documentation from the ground up** when done (e.g. `docs/Performance-and-Graphics-Settings.md`): every setting
   (meaning, default, range, file key, live vs restart), the profiler/stats overlay, `--benchmark` CLI, renderer changes, measurements,
   file map. Detailed enough that another agent can write user docs from it alone.

## Findings so far (code reading, not yet measured)
- `Editor/LumaEditor/main.cpp:189` `rc.vsync = true` → FIFO → 60 FPS cap.
- Editor never set `enableValidation`, and its default was `true` → **Vulkan validation layers on in Development** (huge per-draw CPU cost).
- `VulkanDeferredRenderer` uses **one command buffer + one fence**; `RenderScene` starts with `vkWaitForFences` on the previous viewport
  frame → CPU/GPU serialization every frame.
- Viewport renders at **1.5× on 96-DPI** (2.25× pixels) — hardcoded in main.cpp.
- **No frustum culling**; every instance drawn in G-buffer + **all 4 shadow cascades** (2048², always cleared even when shadows off).
- One `vkCmdDrawIndexed` per section, pipeline bound per section, push constants per draw (no sorting / batching).
- `Log::Flush()` every frame (file flush) in main.cpp.
- `SetViewportDimensions` does a full `Cleanup()+Initialize()` on any size change (reloads environment, pipelines...).
- Texture samplers are **immutable** in the material set layout → anisotropy / mip bias can only apply at renderer creation.
- No SSAO / MSAA / mesh-LOD exist in the renderer: do not add settings for them (owner: no dead features). "LOD bias" = texture mip bias.

## Done (in the WIP commit)
- `Engine/Profiling` (`Luma::Profiling`): `Profiler` (CPU scopes `LUMA_PROFILE_SCOPE`, GPU timings per source, counters, rolling
  average, `StartCapture/TotalAverage`, `FormatReport`, `WriteReportJson`) and `FrameLimiter` (high-res waitable timer + spin).
- `Engine/Rendering/Settings` (`Luma::GraphicsSettings`): `Graphics::DisplaySettings` (vsync, presentMode, frameRateLimit,
  backgroundFrameRateLimit, realtimeViewport), `RenderingSettings` (tier, renderScale, AA None/FXAA, bloom, autoExposure, shadows,
  shadowMapSize, maxShadowCascades, maxShadowDistance, frustumCulling, shadowCasterCulling, maxAnisotropy, textureMipBias),
  tiers (`TierPreset/ApplyTier/MatchingTier`), `Sanitize`, Save/Load sections, `LoadFromConfigDir` (Config/Graphics.json, sections
  `Engine.Display` / `Engine.Rendering`), `ToRenderQuality`.
- `Luma/Renderer/RenderQuality.h`, `Luma/Renderer/FrustumCulling.h` (Frustum from viewProj, Vulkan 0..1 depth, AABB/sphere tests,
  `TransformAabb`, `ignoreNear` for shadow casters) — header-only, **not used yet**.
- RHI `Renderer.h`: `PresentMode {Fifo, Mailbox, Immediate}`, `RendererConfig{presentMode, enableValidation=false, maxAnisotropy,
  textureMipBias}` (**`vsync` field removed**), `Renderer::SetPresentMode/ActivePresentMode`.
- `VulkanSwapchain`: requested/active present mode with fallback, 3 images for mailbox; `VulkanRenderer::SetPresentMode` recreates
  the swapchain next frame; GPU timer "UI"; CPU scopes for fence/acquire and present.
- `Vulkan/Profiling/VulkanGpuTimer` (timestamp queries per slot, availability-checked, no stalls) + deferred renderer instrumented
  (GPU scopes: Sky Light, G-Buffer, Sun Shadows, Lighting, Grid, Translucency, Post Process, FXAA, Editor Overlays; counters
  draws/triangles/shadow draws/pipeline+descriptor binds; `SetProfileName("Preview")` for the material preview renderer).
- `Window::IsFocused/IsMinimized` (GLFW).
- Project Settings pages `Editor/panels/ProjectSettings/src/GraphicsPages.cpp` (Engine > Display & Frame Rate, Engine > Rendering
  Quality) with tooltips; registered in `EditorScreen::RegisterEditorUi`; `EditorScreen::GraphicsSettings()` accessor.

## Step 0 — make it compile (NOT done)
1. Apply `AgentTasks/handoff-editor-performance/e4.py` to `Editor/LumaEditor/main.cpp` (run from repo root with
   `PYTHONPATH=AgentTasks/handoff-editor-performance python AgentTasks/handoff-editor-performance/e4.py`). It failed only because
   main.cpp is **CRLF**: make `Ed` normalize (`s.replace('\r\n','\n')` on read, write back with `\r\n`) or match `\r\n`. It adds:
   validation flag (`--vulkan-validation`, `LUMA_VULKAN_VALIDATION`, on in Debug), startup graphics load, present mode from settings,
   frame limiter, profiler BeginFrame/EndFrame + scopes, live `SetPresentMode`, render scale (`ScaledViewportExtent`), `--benchmark N
   --benchmark-warmup N --benchmark-out file.json --benchmark-label s`.
2. Replace `rc.vsync = false;` with `rc.presentMode = Luma::PresentMode::Immediate;` in `Tests/Rendering/RHIAdapterTests.cpp` and
   `Tests/Rendering/VulkanTestFixture.h`; in `Runtime/Sandbox/main.cpp` replace the `rendererConfig.vsync` line (see step 6).
3. Build, fix compile errors, run tests.

## Remaining steps
4. **Measure "before"**: make a bench project (copy `Tests/Fixtures/MaterialShowcase`; generate a scene with ~1000 primitives by
   duplicating entities in `Assets/Scenes/Main.lscene` with Python) and an empty-scene project. Write `Config/Graphics.json` with
   `Engine.Display {vsync:false, presentMode:"Immediate", frameRateLimit:0}` and `Engine.Rendering {renderScale:1.5}` (= old behaviour).
   Run `LumaEditor.exe --project X.luma --benchmark 600 --benchmark-out before.json` from `build/bin/Development`. Record numbers.
5. **Fix hotspots** (measure after each):
   - `VulkanDeferredRenderer::SetQuality(RenderQuality)`: runtime shadow map size / cascades (recreate shadow image on change,
     replace `kShadowSize`/`kCascades` constants with members; skip shadow passes entirely when off), max distance clamp, FXAA off
     (copy/blit or point the UI texture at the composite), bloom/autoExposure overrides; call it from main.cpp every frame.
   - Frustum culling (G-buffer, selection, translucency) + per-cascade caster culling via `FrustumCulling.h`; count `culledInstances`.
   - Sort opaque draws by pipeline/material/mesh to cut binds.
   - 2 frames in flight in the deferred renderer (per-slot command buffers/fences/lighting UBO; check grid/overlay/eye-adaptation
     buffers for per-frame CPU writes) to remove the per-frame fence stall.
   - Resize: don't rebuild everything (keep env texture / pipelines / sky light; only size-dependent targets).
   - Pass `RendererConfig::maxAnisotropy/textureMipBias` into `VulkanTextureCache::CreateSamplers` (through VulkanDeferredRenderer ctor).
   - Log::Flush: flush only when something was logged or ~1 s.
   - Realtime Viewport off: skip `RenderScene` when camera/scene/selection/material revision unchanged (hash the SceneView inputs +
     a content revision counter in the deferred renderer), keep rendering ~1 s after a change for auto exposure.
6. Sandbox (`Runtime/Sandbox/main.cpp`): accept `--project <file.luma>`, `Graphics::LoadFromConfigDir`, use present mode +
   `FrameLimiter` (remove the 1 ms sleep), link `Luma::GraphicsSettings Luma::Profiling`.
7. Stats overlay: extend `ViewportPanel::DrawStats` (`Editor/panels/Viewport/src/Viewport.cpp:427`, toggled by Show > Stats) to show
   `Profiling::Global().Average()`: FPS, frame/CPU/GPU ms, draws, triangles, instances/culled, CPU scopes and GPU passes.
8. Project Settings window restyle (owner request 1 above).
9. Catch2 tests (add to `Tests/CMakeLists.txt`, link `Luma::Profiling Luma::GraphicsSettings`): settings save/load round trip,
   sanitize/clamp, tier match, `LoadFromConfigDir` (missing file, bad JSON, newer version), `EffectivePresentMode`/frame-limit,
   `FrameLimiter::WaitTime/NextFrameStart`, Profiler averaging/scopes/stale GPU sources, frustum tests (inside/outside/straddling,
   ignoreNear), `TransformAabb`, `VulkanSwapchain::ChoosePresentMode` fallback, Project Settings pages persisted through
   `ProjectSettingsStore` to Config/Graphics.json.
10. "After" benchmarks (uncapped, default High tier and old-equivalent settings), full suite green, smoke-run editor with
    `--vulkan-validation` → 0 `VUID`/`validation error` lines in `Saved/Logs/Editor.log`.
11. Documentation (owner request 2), commit, push, PRs `agent/editor-performance` → `dev` and → `main` with before/after numbers.
