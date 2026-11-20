# PBR Material System (Phase 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Unreal-style PBR material assets, Material Instances and texture binding rendered correctly by the single deferred renderer, with full editor tooling.

**Architecture:** Renderer-agnostic asset model in `Engine/Material` (materials, instances, resolver, library) feeds flattened `MaterialGpuDesc`s through a new RHI `SceneRenderer` interface to `VulkanDeferredRenderer`, which owns a bindless texture cache, a GPU material table, a mesh cache, and one shader template (`EvaluateMaterial`) shared by G-buffer and shadow passes — the Phase 2 graph insertion point. The editor gains a Material Editor panel, Inspector material slots, Content Browser creation flows and drag-and-drop.

**Tech Stack:** C++20 (MSVC), Vulkan 1.3 (+1.2 descriptor indexing), GLSL via glslc, EnTT, Catch2, Luma Slate UI.

**Spec:** `docs/superpowers/specs/2026-11-20-pbr-material-system-design.md`

## Global Constraints

- Every feature is its own module/folder with its own API; the Vulkan backend stays generic and receives feature data through the RHI (`Engine/Rendering/RHI`).
- No placeholders, dead buttons, stubs or TODO paths in shipped code; every input shown in UI is honoured by every relevant render pass.
- Asset references in files are Unreal-style paths: `/Game/...` = project `Assets/`, `/Engine/...` = engine `Content/Engine/`.
- Extensions: material `.lmat` (v2 JSON), material instance `.lmatinst`.
- Default Material: `/Engine/Materials/DefaultMaterial` — base color (0.82, 0.82, 0.85), metallic 0, roughness 0.5, specular 0.5.
- Opacity Mask Clip default 0.3333; F0 = 0.08 × Specular (0.5 → 0.04).
- Blend modes: Opaque, Masked. Shading models: DefaultLit, Unlit.
- User-facing messages use `LUMA_USER_INFO/WARN/ERROR`; engine diagnostics use `LUMA_LOG_*`.
- Build: `MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*" cmd.exe /c "Scripts\\_env.bat cmake --build build --config Development --target LumaEditor LumaTests"` (kill `LumaEditor.exe` first).
- Tests: `./build/bin/Development/LumaTests.exe "<tag>"`; whole suite must pass before each commit.
- Render checks: `./build/bin/Development/LumaEditor.exe --project <project>.luma --screenshot <png>`; `Saved/Logs/Editor.log` must contain zero Vulkan validation errors.
- Commits: no git identity is configured — commit with `git -c user.name="$(git log -1 --format=%an main)" -c user.email="$(git log -1 --format=%ae main)" commit ...`, message ending with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Branch: `feature/pbr-material-system`.

## Review Focus

1. A texture referenced by a material is deleted or renamed → the input falls back to its constant, a user warning is logged once, no crash (test in Task 4).
2. Non-power-of-two, very large (8192²) and grayscale / 16-bit / RGBA images → uploaded with a full mip chain and correct channel expansion (test in Task 8).
3. Editing an instance's parent to create a cycle (A→B→A) → rejected with a message, previous parent kept (test in Task 4).
4. Mesh asset swapped so slot count ≠ section count → extra slots preserved but unused, missing slots use the Default Material (test in Task 10).
5. Migrated, unsaved Material Instances when the scene is closed or the editor quits → the dirty-scene prompt covers them; nothing is silently lost (test in Task 10).

---

### Task 0: Baseline commit of pre-existing work

The working tree contains the user's earlier uncommitted work (Material graph removal, renderer/shadow changes) and this session's UI work in the same files later tasks edit.

- [ ] **Step 1:** Get the user's explicit OK to commit the current working tree as one baseline commit on `feature/pbr-material-system` (do not proceed without it).
- [ ] **Step 2:** Build + run the full suite; expected `All tests passed`.
- [ ] **Step 3:** `git add -A` (excluding `build/`, `Saved/`, scratch files per `.gitignore`), commit `chore: baseline before material system (UI pass + prior WIP)`.

### Task 1: Remove the unused render paths

**Files:**
- Create: `Engine/Rendering/Renderer/include/Luma/Renderer/DeferredSceneView.h` (moves `Renderer2::DeferredSceneView` + anything `VulkanDeferredRenderer` needs from `DeferredShadingRenderer.h`)
- Delete: `Renderer2::DeferredShadingRenderer` and the G-buffer/shadow/lighting sources only it uses in `Engine/Rendering/Renderer/src/`; `VulkanSceneView` forward path; `shaders/scene.vert`, `shaders/scene.frag`; `Renderer::RenderSceneView` from the RHI and its implementations.
- Modify: `Editor/LumaEditor/EditorScreen.{h,cpp}` (drop `m_deferredRenderer`, `InitializeDeferredRenderer`, render-mode/debug-visualization hooks), `Engine/Asset/src/MeshThumbnailRenderer.cpp` (drop its Renderer2 branch; CPU path stays until Task 14), CMake lists, `Tests/` NullRenderers that override removed methods.

**Interfaces:** Produces `Luma/Renderer/DeferredSceneView.h` with `Renderer2::DeferredSceneView` unchanged.

- [ ] **Step 1:** Grep for every reference to the removed types/functions/shaders; list them in the commit message body.
- [ ] **Step 2:** Remove/move as listed; build. Expected: builds clean.
- [ ] **Step 3:** Full suite passes; `--screenshot` of `TestProject` matches the pre-change render (compare visually against a capture taken before Step 2).
- [ ] **Step 4:** Commit `refactor: single deferred render path (remove unused renderers)`.

### Task 2: Asset paths + material asset model

**Files:**
- Create: `Engine/Asset/include/Luma/Asset/AssetPath.h`, `Engine/Asset/src/AssetPath.cpp`
- Replace: `Engine/Material/include/Luma/Material/{Material.h,MaterialTypes.h}` with `MaterialTypes.h`, `Material.h`, `MaterialInstance.h`
- Test: `Tests/Asset/AssetPathTests.cpp`, `Tests/Material/MaterialModelTests.cpp`

**Interfaces — Produces:**
- `class AssetPathResolver { void SetGameRoot(path); void SetEngineRoot(path); std::optional<path> ToFile(std::string_view virtualPath) const; std::optional<std::string> ToVirtual(const path& file) const; }` — virtual paths have no extension (`/Game/Materials/M_Wood`); `ToFile` resolves the extension by probing the registry-known extensions.
- `enum class MaterialParameter : u8 { BaseColor, Metallic, Specular, Roughness, Normal, EmissiveColor, AmbientOcclusion, OpacityMask, Count }` + `const char* ParameterKey(MaterialParameter)` (`"baseColor"`, …) + `bool IsColorParameter(MaterialParameter)` (true for BaseColor, EmissiveColor).
- `enum class TextureChannel : u8 { RGB, R, G, B, A }`, `enum class BlendMode : u8 { Opaque, Masked }`, `enum class ShadingModel : u8 { DefaultLit, Unlit }`.
- `struct MaterialInput { Math::Vec4 constant; std::string texture; /*virtual path, empty = none*/ TextureChannel channel; Math::Vec4 tint{1,1,1,1}; Math::Vec2 uvTiling{1,1}; Math::Vec2 uvOffset{0,0}; f32 normalStrength = 1; bool flipGreen = false; f32 emissiveIntensity = 1; }`
- `struct MaterialSettings { BlendMode blendMode; ShadingModel shadingModel; bool twoSided = false; f32 opacityMaskClip = 0.3333f; }`
- `struct Material { std::string name; MaterialSettings settings; std::array<MaterialInput, Count> inputs; static Material MakeDefault(); }` — `MakeDefault` uses the Global Constraints values; defaults per input: Metallic 0, Specular 0.5, Roughness 0.5, Normal (0,0,1), Emissive (0,0,0), AO 1, OpacityMask 1.
- `struct MaterialInstance { std::string name; std::string parent; std::array<bool, Count> overridden{}; std::array<MaterialInput, Count> inputs; std::optional<bool> twoSided; std::optional<f32> opacityMaskClip; std::optional<BlendMode> blendMode; }`

- [ ] **Step 1: Write failing tests** — `TEST_CASE("AssetPath maps /Game and /Engine to roots")`: `/Game/Materials/M_Wood` ↔ `<game>/Materials/M_Wood.lmat`; path outside both roots → `nullopt`; Windows separators normalized. `TEST_CASE("Default material matches legacy component defaults")`: BaseColor constant == (0.82,0.82,0.85), Roughness .x == 0.5, Specular .x == 0.5, opacityMaskClip == 0.3333f, blendMode Opaque.
- [ ] **Step 2:** Run `LumaTests "[assetpath],[material]"` — FAIL (missing headers).
- [ ] **Step 3:** Implement; register test files in `Tests/CMakeLists.txt`.
- [ ] **Step 4:** Tests pass; full suite passes.
- [ ] **Step 5:** Commit `feat(material): asset paths and v2 material model`.

### Task 3: Serialization + Material Instance asset type

**Files:**
- Modify: `Engine/Material/src/MaterialSerializer.cpp`, `include/.../MaterialSerializer.h`; `Engine/Asset/include/Luma/Asset/AssetType.h`, `Engine/Asset/src/AssetType.cpp`, `AssetMetadata.cpp`
- Create: `Engine/Material/{include/Luma/Material/MaterialInstanceSerializer.h, src/MaterialInstanceSerializer.cpp}`
- Test: `Tests/Material/MaterialSerializerTests.cpp` (+ fixture `Tests/Material/Fixtures/v1_material.lmat` copied from `TestProject/Assets/Materials/TestMaterial.lmat`)

**Interfaces — Produces:** `MaterialSerializer::{SaveToFile, LoadFromFile, ToJson, FromJson}` (same signatures, v2 format with `"version": 2`, inputs keyed by `ParameterKey`), `MaterialInstanceSerializer` with the same four statics for `MaterialInstance`; `AssetType::MaterialInstance` (appended before `Folder`/`Count` handling as the enum allows, name `"MaterialInstance"`, extension `lmatinst`, icon `"Sphere"`).

- [ ] **Step 1: Write failing tests:** round-trip every field of `Material` and `MaterialInstance` (incl. optional overrides absent vs present); v1 fixture loads with baseColor (0.5,0.5,0.5), roughness 0.5, maps ignored, no error; unknown future keys ignored; malformed JSON returns false with a non-empty error; `AssetTypeFromExtension("lmatinst") == AssetType::MaterialInstance`.
- [ ] **Step 2:** Run `LumaTests "[material][serialization]"` — FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Tests + full suite pass.
- [ ] **Step 5:** Commit `feat(material): v2 .lmat and .lmatinst serialization`.

### Task 4: Resolver, MaterialLibrary, Default Material content

**Files:**
- Create: `Engine/Material/{include/Luma/Material/MaterialResolver.h, MaterialLibrary.h; src/MaterialResolver.cpp, MaterialLibrary.cpp}`, `Content/Engine/Materials/DefaultMaterial.lmat`
- Test: `Tests/Material/MaterialResolverTests.cpp`, `Tests/Material/MaterialLibraryTests.cpp`

**Interfaces — Produces:**
- `struct ResolvedMaterial { MaterialSettings settings; std::array<MaterialInput, Count> inputs; std::string rootMaterial; u64 version; }`
- `class MaterialLibrary { explicit MaterialLibrary(AssetPathResolver&); const Material* GetMaterial(std::string_view path); const MaterialInstance* GetInstance(std::string_view path); bool IsInstance(std::string_view path) const; const ResolvedMaterial& Resolve(std::string_view path); /* "" → Default */ void SetMaterial(std::string_view path, Material); void SetInstance(std::string_view path, MaterialInstance); /* in-memory edit, bumps version */ bool Save(std::string_view path); bool IsDirty(std::string_view path) const; std::vector<std::string> DirtyAssets() const; void Reload(std::string_view path); /* file changed */ bool CanSetParent(std::string_view instance, std::string_view parent) const; u64 Version(std::string_view path) const; std::string CreateInMemoryInstance(std::string_view desiredPath, MaterialInstance); }`
- Resolution: walk parents (max depth 16); cycle or depth overflow → treat as Default + `LUMA_USER_WARN` once per asset; missing parent → Default + warn; overrides apply child-over-parent; missing texture file → input keeps constant, warn once.

- [ ] **Step 1: Write failing tests:** 3-deep chain overrides (grandchild overrides roughness only → baseColor from root); `CanSetParent("A","B")` false when B→A; cycle on disk resolves to Default without hanging; missing parent → Default; `Resolve("")` == Default; `SetInstance` bumps `Version` and marks dirty; `Save` writes file and clears dirty; deleted texture file → input reports no texture and warning logged once (capture with a `LogBuffer` sink).
- [ ] **Step 2:** Run `LumaTests "[material][resolver],[material][library]"` — FAIL.
- [ ] **Step 3:** Implement; mount `Content/Engine` as the `/Engine` root (editor startup adds it to the AssetPathResolver and AssetRegistry).
- [ ] **Step 4:** Tests + full suite pass.
- [ ] **Step 5:** Commit `feat(material): resolver, library and Default Material`.

### Task 5: Vertex format with UVs and tangents

**Files:**
- Modify: `Engine/Rendering/Mesh/include/Luma/Mesh/Mesh.h`, `src/Mesh.cpp`
- Create: `Engine/Rendering/Mesh/{include/Luma/Mesh/Tangents.h, src/Tangents.cpp, include/Luma/Mesh/MeshConversion.h, src/MeshConversion.cpp}`
- Test: `Tests/Rendering/MeshGeometryTests.cpp`

**Interfaces — Produces:**
- `struct MeshVertex { Math::Vec3 position; Math::Vec3 normal; Math::Vec4 tangent; /* w = ±1 handedness */ Math::Vec2 uv0; };`
- `struct MeshSection { u32 indexOffset; u32 indexCount; std::string materialName; };`
- `struct MeshData { std::vector<MeshVertex> vertices; std::vector<u32> indices; std::vector<MeshSection> sections; /* primitives: one section */ };`
- `void ComputeTangents(MeshData&)` — per-triangle accumulation weighted by UV area, Gram-Schmidt against the normal, handedness from bitangent sign (MikkTSpace-compatible results on mirrored UVs).
- `MeshData ConvertLumaMesh(const LumaMeshData&)` — uses stored tangent+bitangent when non-zero (handedness = sign(dot(cross(n,t), b))), otherwise calls `ComputeTangents`; one `MeshSection` per `LumaSubmesh`.

- [ ] **Step 1: Write failing tests:** every primitive's UVs within [0,1]; every tangent unit length and ⟂ normal (|dot| < 1e-3); plane tangent == (1,0,0,1); sphere has a duplicated seam (no triangle spanning u 0→1); `ComputeTangents` on a quad with mirrored UVs yields w = -1; `ConvertLumaMesh` of a 2-submesh mesh gives 2 sections with matching index ranges and material names.
- [ ] **Step 2:** Run `LumaTests "[mesh]"` — FAIL.
- [ ] **Step 3:** Implement (cube: per-face 0..1 UVs; sphere: equirect with seam column; cylinder: wrap + planar caps; plane 0..1).
- [ ] **Step 4:** Tests + full suite pass (renderer still compiles: update its vertex input to the new stride but keep reading position/normal only until Task 11).
- [ ] **Step 5:** Commit `feat(mesh): UVs, tangents and sections`.

### Task 6: Texture color space + importer fix

**Files:**
- Create: `Engine/Asset/{include/Luma/Asset/TextureColorSpace.h, src/TextureColorSpace.cpp}`
- Modify: `Engine/Asset/src/TextureImporter.cpp` (remove baking of the sRGB curve into pixels; keep the flag)
- Test: `Tests/Asset/TextureColorSpaceTests.cpp`

**Interfaces — Produces:**
- `bool IsTextureSRGB(const std::filesystem::path& file, const LumaTextureMetadata* meta)` — metadata flag wins; else name tokens `normal|nor|nrm|rough|metal|ao|occlusion|orm|arm|mask|opacity|height|disp` (case-insensitive, token-delimited by `_ - . space`) → false, otherwise true.
- `enum class TextureView : u8 { SRGB, Linear }`; `TextureView SelectTextureView(MaterialParameter p, bool textureIsSRGB)` — SRGB only when `IsColorParameter(p) && textureIsSRGB`.

- [ ] **Step 1: Write failing tests:** table-driven names (`Barrel_02_diff_2k.png`→true, `barrel_02_nor_gl_2k.png`→false, `T_Rock_ORM.png`→false, `Snormal.png`→true because not token-delimited); metadata `sRGB=false` overrides name; `SelectTextureView(Normal, true) == Linear`; `SelectTextureView(BaseColor, true) == SRGB`; importer output pixels equal source pixels for an sRGB-flagged import.
- [ ] **Step 2:** Run `LumaTests "[texture]"` — FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Tests + full suite pass.
- [ ] **Step 5:** Commit `fix(texture): color space as a flag, not baked into pixels`.

### Task 7: RHI SceneRenderer contract

**Files:**
- Create: `Engine/Rendering/RHI/include/Luma/RHI/SceneRenderer.h`, `MaterialGpuDesc.h`
- Modify: `Engine/Rendering/RHI/include/Luma/RHI/Renderer.h` (`SceneInstance`), `VulkanDeferredRenderer.h` (inherits `SceneRenderer`)
- Test: `Tests/Rendering/MaterialGpuDescTests.cpp`

**Interfaces — Produces:**
- `using MeshHandle = u32; using MaterialHandle = u32; using GpuTextureHandle = u32; constexpr u32 kInvalidHandle = ~0u;`
- `struct MaterialGpuDesc { BlendMode blend; ShadingModel shading; bool twoSided; f32 opacityMaskClip; struct Input { Math::Vec4 constant; GpuTextureHandle texture; TextureView view; TextureChannel channel; Math::Vec4 tint; Math::Vec2 tiling, offset; f32 normalStrength; bool flipGreen; f32 emissiveIntensity; }; std::array<Input, MaterialParameter::Count> inputs; }` (RHI copies of the enums live here so RHI does not depend on `Engine/Material`; a converter in the editor bridge maps them).
- `struct TextureUploadDesc { u32 width, height; TexturePixelFormat format; /* R8, RG8, RGBA8, R16, RGBA16 */ const void* pixels; bool srgb; }`
- `class SceneRenderer { virtual MeshHandle UploadMesh(u64 key, const MeshData&) = 0; virtual void ReleaseMesh(MeshHandle) = 0; virtual GpuTextureHandle UploadTexture(u64 key, const TextureUploadDesc&) = 0; virtual void ReleaseTexture(GpuTextureHandle) = 0; virtual MaterialHandle UpdateMaterial(u64 key, const MaterialGpuDesc&) = 0; virtual void ReleaseMaterial(MaterialHandle) = 0; virtual MeshHandle PrimitiveMesh(MeshPrimitive) const = 0; };`
- `SceneInstance` becomes `{ Math::Mat4 model; MeshHandle mesh; std::array<MaterialHandle, 16> sectionMaterials; u32 sectionCount; bool selected; }` (sections beyond 16 use the last material; documented).

- [ ] **Step 1: Write failing test:** `MaterialGpuDesc` default has every input texture == `kInvalidHandle`; `static_assert` sizes stable.
- [ ] **Step 2–4:** Implement headers; `VulkanDeferredRenderer` declares the overrides (implemented in Tasks 8–9; this task's commit includes Task 8 so nothing is left unimplemented — do Tasks 7–9 on the branch before building the editor, committing after Task 9 passes).

### Task 8: Device features + VulkanTextureCache

**Files:**
- Modify: `Engine/Rendering/Vulkan/src/Vulkan/VulkanDevice.cpp` (enable Vulkan 1.2 `runtimeDescriptorArray`, `descriptorBindingPartiallyBound`, `descriptorBindingVariableDescriptorCount`, `shaderSampledImageArrayNonUniformIndexing`, `descriptorBindingSampledImageUpdateAfterBind`; `samplerAnisotropy`; fail with a clear `LUMA_LOG_ERROR` and refuse the device if missing)
- Create: `Engine/Rendering/Vulkan/src/Vulkan/Textures/VulkanTextureCache.{h,cpp}`
- Test: `Tests/Rendering/VulkanTextureCacheTests.cpp` (headless device, same setup as `Tests/Rendering/RHIAdapterTests.cpp`)

**Interfaces — Produces:** `class VulkanTextureCache { VulkanTextureCache(VkPhysicalDevice, VkDevice, VkQueue, u32 queueFamily, u32 maxTextures = 4096); GpuTextureHandle Upload(u64 key, const TextureUploadDesc&); void Release(GpuTextureHandle); u32 BindlessIndex(GpuTextureHandle, TextureView) const; VkDescriptorSetLayout Layout() const; VkDescriptorSet Set() const; u32 MipCount(GpuTextureHandle) const; }` — images `MUTABLE_FORMAT` with UNORM + SRGB views, full mip chain via `vkCmdBlitImage`, 16× anisotropic repeat sampler, one bindless `sampler2D[]` array (update-after-bind, partially bound), freed slots reused; index 0/1 reserved for a 1×1 white texture (UNORM/SRGB).

- [ ] **Step 1: Write failing tests:** upload 300×200 RGBA8 → `MipCount == 9`; 8192×8192 → 14 mips; R8 and RGBA16 uploads succeed; SRGB and Linear indices differ for one texture; Release then Upload reuses the freed index; uploading the same key twice returns the same handle.
- [ ] **Step 2:** Run `LumaTests "[vulkan][texture]"` — FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Tests pass; validation layer reports no errors during the tests.

### Task 9: VulkanMeshCache + VulkanMaterialSystem

**Files:**
- Create: `Engine/Rendering/Vulkan/src/Vulkan/Meshes/VulkanMeshCache.{h,cpp}`, `Engine/Rendering/Vulkan/src/Vulkan/Materials/VulkanMaterialSystem.{h,cpp}`
- Modify: `VulkanDeferredRenderer.{h,cpp}` — implement `SceneRenderer` by delegating; replace `m_primitives`/custom mesh code with `VulkanMeshCache` (primitives registered at init and returned by `PrimitiveMesh`).
- Test: `Tests/Rendering/VulkanMaterialSystemTests.cpp`

**Interfaces — Produces:**
- `VulkanMeshCache::Upload(u64 key, const MeshData&) -> MeshHandle`, `Get(MeshHandle) -> const GpuMesh*` with `{ GpuBuffer vertices, indices; std::vector<MeshSection> sections; }`.
- `struct GpuMaterial` (std430, 16-byte aligned) = flags (blend, shading, twoSided, flipGreen per input), opacityMaskClip, and per input: constant vec4, tint vec4, tiling+offset vec4, (texIndex, channel, normalStrength/emissiveIntensity) — layout mirrored exactly in `material_common.glsl` (Task 11).
- `VulkanMaterialSystem::Update(u64 key, const MaterialGpuDesc&, const VulkanTextureCache&) -> MaterialHandle` writes one entry of a persistently mapped SSBO; per-frame-in-flight copies (2) so an in-flight frame never reads a half-written entry; `Index(MaterialHandle) -> u32`; handle 0 = Default Material.

- [ ] **Step 1: Write failing tests:** updating a material twice keeps its handle; two keys get distinct indices; released index reused; `GpuMaterial` texIndex == the texture cache's bindless index for the selected view; mesh upload of a 2-section mesh reports 2 sections.
- [ ] **Step 2:** Run `LumaTests "[vulkan][material]"` — FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Tests + full suite pass; editor still renders primitives (G-buffer still uses the old push path until Task 11).
- [ ] **Step 5:** Commit Tasks 7–9: `feat(render): scene renderer contract, texture cache, mesh cache, material table`.

### Task 10: Scene component, migration, editor material bridge

**Files:**
- Modify: `Engine/Scene/include/Luma/Scene/Components.h` (`MeshRendererComponent`: remove albedo/metallic/roughness, add `std::vector<std::string> materials`), `Engine/Scene/src/ComponentReflection.cpp`, `Engine/Scene/src/SceneSerializer.cpp` (version bump; legacy read path)
- Create: `Engine/Scene/{include/Luma/Scene/MaterialMigration.h, src/MaterialMigration.cpp}`; `Editor/LumaEditor/MaterialSceneBridge.{h,cpp}` (resolves slots via `MaterialLibrary`, converts to `MaterialGpuDesc`, uploads textures/meshes, caches by version, fills `SceneInstance`)
- Modify: `Editor/LumaEditor/EditorScreen.{h,cpp}` (own `AssetPathResolver`, `MaterialLibrary`, bridge; spawn with empty slots; remove palette + selection tint; scene save also saves dirty migrated instances; dirty-scene prompt counts unsaved material assets; the existing VFS `FileWatcher` on `Assets/` calls `MaterialLibrary::Reload` for changed `.lmat`/`.lmatinst` files and re-uploads changed textures)
- Create fixture generator: `Tests/Fixtures/MaterialShowcase/` (project file, scene, PNG textures written by `Tests/Fixtures/GenerateMaterialShowcase.cpp` test helper via stb_image_write: checker base color, bumps normal map, roughness gradient, alpha-mask leaves, emissive stripes)
- Test: `Tests/Scene/MaterialMigrationTests.cpp`, `Tests/Editor/MaterialSceneBridgeTests.cpp`

**Interfaces — Produces:**
- `struct LegacyMaterialValues { Math::Vec3 albedo; f32 metallic, roughness; };` `struct MigrationResult { std::vector<std::pair<std::string /*virtualPath*/, MaterialInstance>> created; std::vector<std::string> reused; };` `MigrationResult MigrateLegacyMaterials(Scene&, const std::vector<std::pair<Entity, LegacyMaterialValues>>&, MaterialLibrary&)` — values equal to Default (within 1e-4) → empty slot; others dedup by value against `/Game/Materials/Migrated/*` and within the scene; names `MI_<FirstEntityName>` (sanitized, unique).
- `class MaterialSceneBridge { void Sync(SceneRenderer&, MaterialLibrary&, AssetRegistry&); void BuildInstances(const Scene&, Entity selected, std::vector<SceneInstance>&); }`

- [ ] **Step 1: Write failing tests:** legacy scene with 3 default-valued + 2 red + 1 blue entities → 2 instances created, default ones get empty slots, resolved base colors equal the legacy albedo; second migration of another scene with the same red reuses the existing instance; migration writes no file until `MaterialLibrary::Save`; scene round-trip keeps `materials` paths; **Review Focus 4:** mesh with 3 sections and 1 slot → sections 2–3 resolve to Default, 5 slots on a 1-section mesh keep all 5 slots in the component; **Review Focus 5:** `DirtyAssets()` lists migrated instances so the dirty prompt includes them.
- [ ] **Step 2:** Run `LumaTests "[migration],[bridge]"` — FAIL.
- [ ] **Step 3:** Implement; run the fixture generator test once to create `Tests/Fixtures/MaterialShowcase` (commit generated files).
- [ ] **Step 4:** Tests + full suite pass; editor opens `TestProject` with identical visuals (screenshot compare).
- [ ] **Step 5:** Commit `feat(scene): material slots, legacy migration, editor material bridge`.

### Task 11: Shader template, G-buffer and lighting

**Files:**
- Create: `Engine/Rendering/Vulkan/shaders/material_common.glsl`
- Rewrite: `shaders/gbuffer.vert`, `shaders/gbuffer.frag`; modify `shaders/deferred_lighting.frag`, `VulkanDeferredRenderer.cpp` (RT3 emissive `R16G16B16A16_SFLOAT`, pipeline permutations {Opaque, Masked} × {OneSided, TwoSided}, set 1 = texture cache bindless set, set 2 = material SSBO, push = model matrix + material index, draw loop over instances × sections), CMake shader DEPENDS adds `material_common.glsl`.

**Interfaces:**
- `material_common.glsl`: `struct MaterialSample { vec3 baseColor; float metallic; float specular; float roughness; vec3 normalWS; vec3 emissive; float ao; float opacityMask; }`; `MaterialSample EvaluateMaterial(uint materialIndex, vec2 uv, vec3 normalWS, vec4 tangentWS, bool frontFacing)` — the Phase 2 graph insertion point (document this at the top of the file).
- G-buffer layout: RT0 = baseColor.rgb + shadingModel/255 in a; RT1 = normal (encoded) + roughness; RT2 = metallic, specular, ao; RT3 = emissive HDR.
- Lighting: F0 = mix(vec3(0.08 × specular), baseColor, metallic); Unlit pixels → baseColor + emissive; DefaultLit adds emissive after lighting; material AO multiplies sky/ambient terms only; view modes (Base Color, World Normal, Roughness, Metallic) read the new buffers.

- [ ] **Step 1:** Build the `MaterialShowcase` scene expectations list (one entity per feature: textured, normal-mapped, masked, two-sided plane, unlit, emissive, packed ORM).
- [ ] **Step 2:** Implement shaders + renderer changes; build. Expected: glslc compiles all shaders.
- [ ] **Step 3:** `--screenshot` `Tests/Fixtures/MaterialShowcase` → visually confirm: checker visible on textured cube, bumps lit correctly as the sun moves (two captures with different sun angles), masked holes show background, two-sided plane lit on both sides, unlit cube flat, emissive stripes bloom; `TestProject` unchanged. Zero validation errors.
- [ ] **Step 4:** Full suite passes.
- [ ] **Step 5:** Commit `feat(render): material shader template, G-buffer and lighting`.

### Task 12: Shadow pass permutations

**Files:** `shaders/shadow.vert`, `shaders/shadow.frag`, `VulkanDeferredRenderer.cpp` (`CreateShadowPipeline`, `RecordSunShadows`).

**Interfaces:** shadow pass uses `EvaluateMaterial`'s opacity-mask path only (`float EvaluateOpacityMask(uint materialIndex, vec2 uv)` added to `material_common.glsl`); permutations {Opaque, Masked} × {cull back, cull none}.

- [ ] **Step 1:** Implement; build.
- [ ] **Step 2:** `--screenshot` MaterialShowcase: masked leaves cast leaf-shaped shadows (holes in the shadow), two-sided plane casts shadow when seen from behind the light. Zero validation errors.
- [ ] **Step 3:** Commit `feat(render): masked and two-sided shadows`.

### Task 13: Selection outline

**Files:** Create `Engine/Rendering/Vulkan/src/Vulkan/Selection/VulkanSelectionOutlinePass.{h,cpp}`, `shaders/selection_outline.frag`; modify `VulkanDeferredRenderer.cpp` (selected instances write stencil ref 1 in the G-buffer pass via a second pipeline state or dynamic stencil reference; outline pass after FXAA, before the overlay pass), depth format with stencil if needed (`D24_UNORM_S8_UINT` or `D32_SFLOAT_S8_UINT`, whichever the device supports).

**Interfaces:** outline color = theme accent (255,140,0), width 2 px, also visible through occluders at 35 % alpha (Unreal behaviour).

- [ ] **Step 1:** Implement; build.
- [ ] **Step 2:** `--screenshot` with a selected entity (add `--select <EntityName>` editor CLI flag used only for captures) → orange outline around the selected mesh only.
- [ ] **Step 3:** Commit `feat(render): Unreal-style selection outline`.

### Task 14: Preview renderer + GPU thumbnails

**Files:** Create `Editor/LumaEditor/MaterialPreview.{h,cpp}` (drives the secondary `VulkanDeferredRenderer` 512×320 instance: preview mesh shape, studio sky/lights, orbit); modify `Engine/Asset/src/MaterialThumbnailRenderer.cpp` and `MeshThumbnailRenderer.cpp` to render through a `ThumbnailRenderCallback` the editor installs (GPU path), removing the CPU ray tracer and CPU mesh rasterizer.

**Interfaces:** `class MaterialPreview { TextureHandle Render(std::string_view materialPath, PreviewShape, f32 yaw, f32 pitch); bool Capture(std::string_view materialPath, const path& png, u32 size); }`; `enum class PreviewShape { Sphere, Cube, Plane, Cylinder }`.

- [ ] **Step 1: Write failing test:** `ThumbnailManager` with a stub callback produces a PNG for a `.lmat` (callback invoked once per request, cached after).
- [ ] **Step 2–4:** Implement; content browser thumbnails for MaterialShowcase materials show the textured sphere; tests pass.
- [ ] **Step 5:** Commit `feat(editor): GPU material preview and thumbnails`.

### Task 15: Slate drag and drop

**Files:** Create `Engine/UI/Slate/{include/Luma/Slate/DragDrop.h, src/DragDrop.cpp}`; modify `Context.{h,cpp}` (owns the drag state; draws the drag ghost in the overlay pass; cancels on Escape or release over no target).

**Interfaces:** `struct DragPayload { std::string type; std::string data; std::string label; TextureHandle icon; }`; `Context::BeginDrag(u64 sourceId, const Rect& source, DragPayload)` (starts after 6 px movement with the button held); `bool Context::IsDragging(std::string_view type) const`; `const DragPayload* Context::AcceptDrop(const Rect& target, std::string_view type)` (returns payload on release inside `target`; highlights the target while hovered).

- [ ] **Step 1: Write failing tests** (`Tests/Slate/DragDropTests.cpp`): press + move 3 px → not dragging; move 10 px → dragging; release over target of matching type → `AcceptDrop` returns payload once; wrong type → nullptr; Escape cancels.
- [ ] **Step 2–4:** Implement; tests pass.
- [ ] **Step 5:** Commit `feat(slate): drag and drop`.

### Task 16: Material Editor panel

**Files:** Create `Editor/panels/MaterialEditor/{CMakeLists.txt, include/Luma/Editor/Panels/MaterialEditor.h, src/MaterialEditor.cpp}`; modify `Editor/panels/CMakeLists.txt`, `EditorScreen.cpp` (open on Content Browser double-click of `.lmat`/`.lmatinst`; one dock tab per asset; tab title `M_Name*` when dirty; close-dirty modal Save/Discard/Cancel).

**Interfaces:** `class MaterialEditorPanel { explicit MaterialEditorPanel(std::string assetPath); void Draw(Slate::Context&, const Slate::Rect&, PanelContext&); bool IsDirty() const; const std::string& AssetPath() const; }`. PanelContext gains `MaterialLibrary* materials`, `MaterialPreview* materialPreview`, `std::function<void(std::string)> onOpenAsset`.

Layout per spec §6.6: toolbar (Save Ctrl+S, Browse to Asset, preview shape combo, background toggle) — preview (orbit drag) — Details: Material Settings; one section per input (constant via ColorPicker/DragFloat, texture slot = thumbnail + searchable asset menu + clear + browse, channel combo, tint/scale, tiling/offset, normal strength + flip green, emissive intensity); instance mode: Parent picker (rejects cycles via `CanSetParent`, user error message) and per-row override checkbox with inherited values dimmed. All controls have tooltips.

- [ ] **Step 1: Write failing tests** (`Tests/Editor/MaterialEditorTests.cpp`, Slate NullRenderer harness): editing roughness marks dirty and bumps library version; unchecking an override restores the parent value; setting a cyclic parent is refused and parent unchanged; Save clears dirty and writes the file.
- [ ] **Step 2–4:** Implement; tests pass; manual check in editor: edit MaterialShowcase material, viewport updates live.
- [ ] **Step 5:** Commit `feat(editor): Material Editor panel`.

### Task 17: Inspector material slots

**Files:** modify `Editor/panels/Inspector/src/Inspector.cpp`, `include/.../Inspector.h`.

Mesh Renderer section: "Materials" list sized to the mesh's section count (extra stored slots listed as "Unused" rows); each row: 40 px thumbnail, asset picker (searchable menu of Material + Material Instance assets, Default Material first), Browse, Clear, Create Instance (creates `MI_<name>` next to the parent via `MaterialLibrary`, saves it, assigns it, logs `LUMA_USER_INFO`); accepts drops of type `"asset/material"`.

- [ ] **Step 1: Write failing tests** (`Tests/Editor/InspectorMaterialTests.cpp`): picking a material writes the path into the slot; Create Instance creates a file whose parent is the previous material and assigns it; dropping a material payload on row 1 sets slot 1.
- [ ] **Step 2–4:** Implement; tests pass.
- [ ] **Step 5:** Commit `feat(editor): Inspector material slots`.

### Task 18: Content Browser material workflows + viewport drop

**Files:** modify `Editor/panels/ContentBrowser/src/ContentBrowser.cpp`, `Editor/panels/Viewport/src/Viewport.cpp`, `EditorScreen.cpp`; create `Engine/Material/{include/Luma/Material/MaterialFromTextures.h, src/MaterialFromTextures.cpp}`.

**Interfaces:** `Material BuildMaterialFromTextures(const std::vector<std::string>& textureVirtualPaths)` — token rules from spec §6.6 (diff|albedo|basecolor|color → BaseColor; nor|normal|nrm → Normal, `_dx` sets flipGreen, `_gl` clears it; rough → Roughness (R); metal → Metallic (R); ao|occlusion → AO (R); arm|orm → AO=R, Roughness=G, Metallic=B; emiss → Emissive; opacity|mask|alpha → OpacityMask (R) and blendMode Masked); sets sRGB flag on base color / emissive textures via metadata. Content Browser: Create menu adds Material, Material Instance; asset menu on Material/Instance adds Create Material Instance, Open; selection of textures adds Create Material from Textures; tiles of Material/Instance/Texture start a drag (`asset/material`, `asset/texture`). Viewport accepts `asset/material` drops: picks the actor and section under the cursor (extend `PickEntity` to report the section) and assigns.

- [ ] **Step 1: Write failing tests** (`Tests/Material/MaterialFromTexturesTests.cpp`): Barrel set (`barrel_02_diff_2k`, `barrel_02_nor_gl_2k`, `barrel_02_rough_2k`, `barrel_02_metal_2k`) → 4 inputs mapped, flipGreen false; `T_Rock_ARM` → AO R, Roughness G, Metallic B; `leaf_alpha` → Masked; unknown names ignored and reported.
- [ ] **Step 2–4:** Implement; tests pass; manual: create a material from the Barrel textures in the user's TestProject copy (scratch), assign by drag onto the barrel, textures render.
- [ ] **Step 5:** Commit `feat(editor): material creation workflows and drag-to-assign`.

### Task 19: Final verification + roadmap

- [ ] **Step 1:** Full suite passes; MaterialShowcase and TestProject screenshots reviewed; zero validation errors.
- [ ] **Step 2:** Update `GOAL.md` §2.4 and §5.10 status/logs (Phase 1 done, graph = Phase 2).
- [ ] **Step 3:** Commit `docs: material system phase 1 complete`.
