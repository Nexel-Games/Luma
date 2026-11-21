# Material Graph & Compiler Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Luma's fixed-recipe materials with Unreal-style node-graph materials compiled to GLSL → SPIR-V at runtime, rendered through per-material pipelines in every pass, with parameters/instances, functions, collections, translucency, post-process materials and three new shading models.

**Architecture:** `Luma::MaterialGraph` (data model + expression catalogue) → `Luma::MaterialCompiler` (typed code chunks, constant folding, uniform preshaders, permutations, per-pass GLSL from templates) → `Luma::ShaderCompiler` (shaderc, async, disk cache) → RHI `CompiledMaterialGpu` → Vulkan per-material pipeline cache with one shared material pipeline layout. The editor bridge drives compiles and instance parameter evaluation.

**Tech Stack:** C++20, Vulkan 1.3, GLSL 450, shaderc (`D:/VulkanSDK/1.4.341.1/Lib/shaderc_combined.lib`), Catch2, Luma Slate.

**Spec:** `docs/superpowers/specs/2026-11-20-material-graph-compiler-design.md`

## Global Constraints

- Study UE 5.8 (`D:/output/UnrealEngine-5.8`) for behaviour — `Engine/Source/Runtime/Engine/Public/MaterialCompiler.h`, `Private/Materials/HLSLMaterialTranslator.*`, `Public/MaterialShared.h` (uniform expressions), `Public/Materials/MaterialExpression*.h`; never copy code.
- Vulkan backend stays generic: it receives SPIR-V, state and parameter bytes through the RHI; no graph types below `Luma::MaterialCompiler`.
- Deferred renderer is the main path; translucency is the only forward pass.
- No placeholders, no silent fallbacks: every fallback (Default Material while compiling, last-good on error) logs a `LUMA_USER_*` line once per cause.
- Edit files byte-exact (mixed CRLF/LF); kill `LumaEditor.exe` before building; build `MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*" cmd.exe /c "Scripts\\_env.bat cmake --build build --config Development"`; tests `./build/bin/Development/LumaTests.exe "<tags>"`; GPU tests use `Tests/Rendering/VulkanTestFixture.h` and assert `ValidationErrors() == 0`.
- Agent coordination: stay inside the lead areas listed in `AgentTasks/BOARD.md`; `Engine/Asset/**` belongs to the engine agent (T-0003) — read-only here.
- Push constants ≤ 128 bytes; descriptor sets: 0 = view/frame, 1 = textures + samplers, 2 = material parameters.
- Every task ends with the whole suite green and a commit on `feature/material-graph`.

## Review Focus

1. A material whose graph is edited while it is compiling never renders a half-updated state (old pipelines until the new set is complete).
2. Instances changing only parameters never trigger a shader compile; static-switch overrides compile exactly one new permutation.
3. Migrated Phase-1 materials render identically (golden image within tolerance) — including masked, two-sided, height/POM, bump, cavity, invert.
4. A Custom node's GLSL error, a type error and a cycle each point at the right node id and keep the last good shaders on screen.
5. Translucent objects sort and blend correctly with each other and with opaque geometry, and receive sun shadows.

---

### Task 0: Branch and baselines

- [ ] **Step 1:** `git checkout -b feature/material-graph dev`.
- [ ] **Step 2:** Capture Phase-1 golden images: scratch copy of `Tests/Fixtures/MaterialShowcase`; `LumaEditor.exe --project <copy> --screenshot Tests/Fixtures/MaterialShowcase/golden_phase1.png` and the 7 material previews via `--preview-capture /Game/Materials/<M> Tests/Fixtures/MaterialShowcase/golden_<M>.png` (256 px). Commit `test: Phase-1 material golden images`.
  Expected: 8 PNGs, 0 validation errors.

### Task 1: Graph data model

**Files:** Create `Engine/Material/Graph/include/Luma/MaterialGraph/{Graph.h,PinTypes.h}`, `Engine/Material/Graph/src/Graph.cpp`, `Engine/Material/Graph/CMakeLists.txt` (`Luma::MaterialGraph`); Test `Tests/Material/GraphTests.cpp`.

**Interfaces — Produces:**
```cpp
namespace Luma::MaterialGraph {
enum class ValueType : u8 { Float1, Float2, Float3, Float4, Bool, Texture2D, TextureCube, Any };
using NodeId = u64;
struct PropertyValue { /* variant<bool,i64,f64,Math::Vec4,std::string> */ };
struct Node { NodeId id; std::string type; Math::Vec2 position; std::map<std::string, PropertyValue> properties; std::string comment; };
struct PinRef { NodeId node; u32 pin; };
struct Link { PinRef from; PinRef to; };            // output -> input
struct Comment { u64 id; Math::Vec2 position, size; std::string text; Math::Vec4 color; };
class Graph {
public:
    NodeId AddNode(std::string type, Math::Vec2 position);   // ids monotonically increasing, never reused
    bool RemoveNode(NodeId);                                  // removes its links
    Node* Find(NodeId); const Node* Find(NodeId) const;
    bool Connect(PinRef from, PinRef to, std::string* err);   // replaces an existing link into `to`
    void Disconnect(PinRef to);
    std::optional<PinRef> Source(PinRef to) const;
    const std::vector<Node>& Nodes() const; const std::vector<Link>& Links() const;
    std::vector<Comment>& Comments();
    NodeId OutputNode() const;                                // the single "MaterialOutput" node (created by the ctor)
    std::vector<NodeId> FindCycle() const;                    // empty = acyclic; else the node ids on one cycle
    NodeId NextId() const;
};
}
```
- [ ] **Step 1: Failing tests** (`[graph]`): new graph has exactly one `MaterialOutput` node; AddNode ids strictly increase, never reused after RemoveNode; Connect replaces an existing input link; RemoveNode removes incident links; `FindCycle` on A→B→A returns {A,B}; Connect to a non-existent pin index fails with message (pin validity checked later by the registry—here only node existence).
- [ ] **Step 2:** Run `LumaTests.exe "[graph]"` → compile failure.
- [ ] **Step 3:** Implement; register module in root `Engine/Material/CMakeLists.txt` (`add_subdirectory(Graph)`), link in `Tests/CMakeLists.txt`.
- [ ] **Step 4:** Pass; suite green. **Step 5:** Commit `feat(material): graph data model`.

### Task 2: Expression catalogue (descriptors)

**Files:** `Engine/Material/Graph/include/Luma/MaterialGraph/ExpressionRegistry.h`, `src/ExpressionRegistry.cpp`, `src/Expressions/*.cpp` (one file per category: Constants, Parameters, Math, Texture, Coordinates, Utility, Functions, PostProcess, Output); Test `Tests/Material/ExpressionRegistryTests.cpp`.

**Produces:**
```cpp
struct PinDesc { std::string name; ValueType type; bool optional = true; PropertyValue defaultValue; std::string tooltip; };
struct PropertyDesc { std::string key, label, tooltip; enum class Kind { Bool, Int, Float, Vec4, String, Enum, Asset } kind;
                      PropertyValue defaultValue; std::vector<std::string> enumValues; f64 min = 0, max = 0; std::string assetType; };
struct ExpressionDesc {
    std::string type, displayName, category, description;
    std::vector<PinDesc> inputs, outputs;
    std::vector<PropertyDesc> properties;
    bool pixelOnly = false;                  // e.g. DDX, ScreenPosition, SceneTexture, PixelDepth
    bool hiddenInPalette = false;            // MaterialOutput, FunctionInput outside functions
    std::vector<std::string> keywords;       // palette search
};
class ExpressionRegistry {
public:
    static const ExpressionRegistry& Get();
    const ExpressionDesc* Find(std::string_view type) const;
    std::vector<const ExpressionDesc*> All() const;           // sorted by category then name
};
```
Register exactly the catalogue of spec §2 (≈90 types). `MaterialOutput` inputs: BaseColor(F3), Metallic(F1), Specular(F1), Roughness(F1), Anisotropy(F1), EmissiveColor(F3), Opacity(F1), OpacityMask(F1), Normal(F3), Tangent(F3), WorldPositionOffset(F3), SubsurfaceColor(F3), CustomData0(F1), CustomData1(F1), AmbientOcclusion(F1), Refraction(F1), PixelDepthOffset(F1), CustomUV0..7(F2). Values used when a pin is unconnected (Unreal's convention): BaseColor 0, Metallic 0, Specular 0.5, Roughness 0.5, Emissive 0, Opacity 1, OpacityMask 1, Normal (0,0,1), AO 1, all others 0. (The Default Material's grey 0.82/0.82/0.85 comes from its own graph — Task 3 `MakeDefault()` — not from pin defaults.)

- [ ] **Step 1: Failing tests** (`[graph][registry]`): every spec §2 type name resolves (list them in the test as a table); `MaterialOutput` has the 25 inputs above in that order with those defaults; `Lerp` inputs A,B,Alpha with defaults 0,1,0.5; `TextureSample` properties include `sampler` enum {Color, Linear, Normal, Masks, LinearGrayscale}, `address` {Wrap, Clamp, Mirror}, `filter` {Linear, Nearest, Anisotropic}, `mipMode` {Default, Bias, Level, Derivatives}; `DDX` is pixelOnly; `All()` sorted; no duplicate type names.
- [ ] **Steps 2–4:** fail → implement → pass; suite green. **Step 5:** Commit `feat(material): expression catalogue`.

### Task 3: Assets — .lmat v4, functions, collections, instances v2, library

**Files:** Modify `Engine/Material/include/Luma/Material/{Material.h,MaterialTypes.h,MaterialInstance.h,MaterialLibrary.h}`, `src/{MaterialJson.cpp,MaterialTypes.cpp,MaterialLibrary.cpp,MaterialSerializer.cpp,MaterialInstanceSerializer.cpp}`; Create `include/Luma/Material/{MaterialFunction.h,ParameterCollection.h}`, `src/{MaterialFunction.cpp,ParameterCollection.cpp,GraphJson.cpp}`; Tests `Tests/Material/{MaterialSerializerTests.cpp,MaterialLibraryTests.cpp,MaterialAssetsV4Tests.cpp}`.

**Produces:**
```cpp
enum class MaterialDomain : u8 { Surface, PostProcess };
enum class BlendMode : u8 { Opaque, Masked, Translucent, Additive, Modulate };
enum class ShadingModel : u8 { DefaultLit, Unlit, Subsurface, ClearCoat, TwoSidedFoliage };
enum class TranslucencyLighting : u8 { SurfacePerPixel, Unlit };
enum class RefractionMethod : u8 { None, IndexOfRefraction, PixelOffset };
enum class PostProcessLocation : u8 { BeforeTonemapping, AfterTonemapping };
struct MaterialSettings {                 // replaces the v3 struct
    MaterialDomain domain = MaterialDomain::Surface; BlendMode blendMode = BlendMode::Opaque;
    ShadingModel shadingModel = ShadingModel::DefaultLit; bool twoSided = false; f32 opacityMaskClip = 0.3333f;
    TranslucencyLighting translucencyLighting = TranslucencyLighting::SurfacePerPixel;
    RefractionMethod refraction = RefractionMethod::None;
    PostProcessLocation ppLocation = PostProcessLocation::BeforeTonemapping; i32 ppPriority = 0;
};
struct Material { std::string name; MaterialSettings settings; MaterialGraph::Graph graph; static Material MakeDefault(); };
struct ParameterOverride { std::string name; enum class Kind { Scalar, Vector, Texture, StaticSwitch } kind;
                           f32 scalar = 0; Math::Vec4 vector{}; std::string texture; bool staticSwitch = false; };
struct MaterialInstance { std::string parent; std::vector<ParameterOverride> overrides;
                          std::optional<bool> twoSided; std::optional<f32> opacityMaskClip; };
struct MaterialFunction { std::string name, description, category; bool exposeToLibrary = true; MaterialGraph::Graph graph; };
struct ParameterCollection { struct Scalar { std::string name; f32 value; }; struct Vector { std::string name; Math::Vec4 value; };
                             std::vector<Scalar> scalars; std::vector<Vector> vectors; u32 Revision() const; };
```
`MaterialLibrary` gains: `const MaterialFunction* GetFunction(path)`, `SetFunction/Save`, `const ParameterCollection* GetCollection(path)`, `SetCollection`, `SetCollectionValue(path, name, value)` (runtime, no save, bumps revision), `std::vector<std::string> Dependents(path)` (materials depending on a function/collection/texture), file extensions `.lmatfunc`, `.lmpc`. `MakeDefault()` = graph with `VectorParameter "BaseColor" (0.82,0.82,0.85,1)` → BaseColor, `ScalarParameter "Roughness" 0.5` → Roughness (so the Default Material stays tweakable through instances).
- [ ] **Step 1: Failing tests** (`[material][v4]`): v4 material round trip (settings + graph nodes/links/properties/comments, ids preserved); function and collection round trips; instance v2 round trip incl. static switch override; library resolves `.lmatfunc`/`.lmpc`; `Dependents()` of a function lists the materials that call it (graph scan of `MaterialFunctionCall.function`); `SetCollectionValue` bumps `Revision()`; loading a v3 file is routed to migration (Task 4) — here assert it is detected (`version == 3`).
- [ ] **Steps 2–4:** fail → implement → pass; existing material tests that assert v3 behaviour are updated to v4 (ledger each as a ruling). **Step 5:** Commit `feat(material): v4 assets — graph materials, functions, collections, instances v2`.

### Task 4: v3 → v4 migration

**Files:** `Engine/Material/src/MaterialMigrationV4.cpp`, `include/Luma/Material/MaterialMigrationV4.h`; Test `Tests/Material/MaterialMigrationV4Tests.cpp`.

**Produces:** `Material MigrateV3(const MaterialV3& old, std::vector<std::string>* notes)`; `MaterialInstance MigrateInstanceV1(const MaterialInstanceV1&, const Material& parentV4)`. `MaterialV3` = the Phase-1 struct, kept under `namespace Luma::Material::Legacy` for loading only.

Mapping per v3 input (parameter names = the input display names, so v1 instance overrides map 1:1):
- No texture: `VectorParameter "<Input>"` (colours) / `ScalarParameter "<Input>"` (scalars) with the constant → output pin.
- Texture: `TextureCoordinate` (tiling U/V, offset via `Add` of `Constant2Vector`) → `TextureSampleParameter2D "<Input> Texture"` (sampler type from v3 colour space / channel) → `ComponentMask` per v3 channel → `OneMinus` if invert → `Multiply` by `VectorParameter "<Input> Tint"` (colours) / `ScalarParameter "<Input> Scale"` → output.
- Normal: strength via `Multiply` of XY then `Append` Z; flipGreen via `Multiply (1,-1,1)`.
- Emissive: extra `ScalarParameter "Emissive Intensity"` multiply.
- Height: `ParallaxOcclusionMapping` (height texture, scale = normalStrength) whose UV output feeds **every** TextureCoordinate consumer (one shared POM node).
- Bump: `NormalFromHeight` combined with the normal by `BlendAngleCorrectedNormals`.
- Cavity: multiply into AO.
- Settings: blendMode/twoSided/clip/shading copied.
Instances v1 → v2: each overridden v3 input property becomes the matching parameter override (constant → `<Input>`, texture → `<Input> Texture`, tint → `<Input> Tint`, …).
- [ ] **Step 1: Failing tests** (`[material][migration]`): constant-only material → 2 parameter nodes wired; textured base colour with tiling 4 → TextureCoordinate tiling (4,4) + sample + tint multiply; invert roughness → OneMinus present; height+bump+cavity material → one POM node feeding all samples, NormalFromHeight + BlendAngleCorrectedNormals, cavity multiply on AO; v1 instance overriding BaseColor tint → v2 override "BaseColor Tint"; migration notes mention every converted input.
- [ ] **Steps 2–4.** **Step 5:** Commit `feat(material): v3 to v4 graph migration`.

### Task 5: Compiler core

**Files:** Create `Engine/Material/Compiler/{include/Luma/MaterialCompiler/Translator.h,CompiledMaterial.h,CodeChunk.h}`, `src/Translator.cpp`, `CMakeLists.txt` (`Luma::MaterialCompiler`); Test `Tests/Material/CompilerCoreTests.cpp`.

**Produces:**
```cpp
namespace Luma::MaterialCompiler {
enum class Frequency : u8 { Constant, Uniform, Varying };
enum class Stage : u8 { Vertex, Pixel };
enum class Pass : u8 { GBuffer, DepthOnly, SelectionMask, Forward, PostProcess, Count };
struct Chunk { std::string code; MaterialGraph::ValueType type; Frequency freq; std::optional<Math::Vec4> constant; };
struct CompileError { MaterialGraph::NodeId node = 0; i32 pin = -1; std::string message; bool warning = false; };
class Translator {            // one per (material, static-switch set)
public:
    Translator(const Material::Material&, const Material::MaterialLibrary&, StaticSwitchSet);
    i32 CompileInput(MaterialGraph::NodeId node, u32 inputPin, Stage);       // follows the link or uses the pin default
    i32 CompileOutput(MaterialGraph::NodeId node, u32 outputPin, Stage);     // cached per (node,pin,stage)
    // emission helpers used by expressions:
    i32 Constant(Math::Vec4 v, MaterialGraph::ValueType t);
    i32 Binary(std::string_view op, i32 a, i32 b, MaterialGraph::NodeId at);     // + - * / with UE broadcast rules
    i32 Call(std::string_view fn, std::span<const i32> args, MaterialGraph::ValueType result, MaterialGraph::NodeId at);
    i32 Cast(i32 chunk, MaterialGraph::ValueType to, MaterialGraph::NodeId at); // Float1 broadcast; error otherwise
    i32 UniformScalar(std::string_view paramName, f32 def, ...); i32 UniformVector(...); i32 TextureParam(...);
    void Error(MaterialGraph::NodeId, i32 pin, std::string msg);
    CompiledMaterial Finish();
};
}
```
Rules: result type of binary ops = wider of the two when one is Float1, else equal widths required; constant folding when both operands Constant (store `constant`); frequency = max of operand frequencies; Varying chunks become local GLSL variables `vN` in emission order, Uniform chunks become preshader slots (Task 7) read as `MP(slot)`; cycle detection before compiling (Graph::FindCycle) → error naming the cycle nodes; pixel-only expressions requested in Vertex stage → error "X can't be used in the vertex shader (World Position Offset / Custom UVs)". Pin visibility rules: inputs of MaterialOutput not used by the domain/blend/shading model are not compiled; if linked → warning "Pin <name> is ignored for <mode>".
- [ ] **Step 1: Failing tests** (`[compiler]`): (Constant 2 + Constant 3) folds to constant 5 with no GLSL variable; Float3 + Float1 → Float3; Float2 + Float3 → error at that node; a shared sub-graph used by two outputs emits once; cycle → error lists both nodes; DDX in WPO → vertex-stage error at the DDX node; Roughness linked on an Unlit material → warning; generated code of an identical graph is byte-identical across two compiles (determinism).
- [ ] **Steps 2–4.** **Step 5:** Commit `feat(material-compiler): translator core`.

### Task 6: Expression compilation

**Files:** `Engine/Material/Compiler/src/Expressions/*.cpp` (mirrors Task 2's categories), `src/ExpressionCompile.h` (dispatch table type → `i32 Compile(Translator&, const Node&, u32 outputPin, Stage)`); Test `Tests/Material/ExpressionCompileTests.cpp`.

GLSL helper library `Engine/Rendering/Vulkan/shaders/material_functions.glsl` (Fresnel (Schlick on `dot(N,V)` with exponent + base reflect fraction), POM (moved from `material_common.glsl`), NormalFromHeight (per-texel slope, moved), BlendAngleCorrectedNormals (reoriented normal mapping), Panner/Rotator math, Desaturation (luminance weights 0.3/0.59/0.11), Remap, transforms between Tangent/Local/World/View using interpolants).
- [ ] **Step 1: Failing tests** (`[compiler][expr]`), table-driven: for every expression type, a minimal graph (inputs from Constants) compiles without errors; result type and frequency per the table (e.g. Time → Float1 Varying, ScalarParameter → Float1 Uniform, Constant3Vector → Float3 Constant, Panner(Time) → Float2 Varying, Dot(F3,F3) → Float1, Append(F2,F1) → F3, ComponentMask(F4,"RG") → F2, If → selected branch type, Fresnel → F1 Varying (pixel-only? no: usable in both)); folded values: Lerp(0,10,0.25)=2.5, Clamp(5,0,1)=1, OneMinus(0.25)=0.75, Power(2,3)=8, Sine(0)=0, Remap(0.5,0,1,10,20)=15; Custom node: inputs named, output type honoured, extra outputs compile, code inserted verbatim inside a generated function `CustomExpressionN`; MaterialFunctionCall compiles via Task 8 (here: unknown function asset → error at the node).
- [ ] **Steps 2–4.** **Step 5:** Commit `feat(material-compiler): expression compilation`.

### Task 7: Uniform expressions, parameter blocks, textures, permutations

**Files:** `Engine/Material/Compiler/{include/.../Preshader.h,src/Preshader.cpp,src/ParameterLayout.cpp}`; Test `Tests/Material/PreshaderTests.cpp`.

**Produces:**
```cpp
enum class PreOp : u8 { PushConst, PushScalarParam, PushVectorParam, PushCollection, Add, Sub, Mul, Div, Neg,
                        Min, Max, Clamp, Saturate, Pow, Abs, Floor, Ceil, Frac, Sqrt, Sin, Cos, Lerp, Dot, Swizzle, Append, Store };
struct PreshaderProgram { std::vector<u8> code; std::vector<Math::Vec4> constants; };
struct ParameterInfo { std::string name, group, description; enum class Kind { Scalar, Vector, Texture, StaticSwitch } kind;
                       f32 sliderMin = 0, sliderMax = 0; i32 sortPriority = 32; Math::Vec4 defaultValue; std::string defaultTexture; };
struct TextureSlot { u32 slot; std::string defaultTexture; std::string parameter; SamplerState sampler; bool srgb; bool cube; };
struct ParameterBlockLayout { u32 vec4Count; std::vector<TextureSlot> textures; std::vector<std::pair<std::string,u32>> collectionSlots; };
std::vector<Math::Vec4> EvaluatePreshader(const PreshaderProgram&, const ParameterBlockLayout&,
                                          std::span<const Material::ParameterOverride>, const ParameterValueSource& collections,
                                          const TextureResolver& textures);   // returns the full block (vec4 per slot)
struct StaticSwitchSet { std::vector<std::pair<std::string,bool>> values; u64 Key() const; };
```
Layout: one vec4 per uniform chunk (deterministic order of first use); textures occupy one slot each, stored as `uvec4(floatBitsToUint)`: x = sampled-image index (view chosen by sRGB), y = sampler index, z = 1 if bound; collection base offsets occupy slots read by `LUMA_COLLECTION(slot, index)`.
- [ ] **Step 1: Failing tests** (`[compiler][preshader]`): `ScalarParameter A (2) * Constant 3` → one uniform slot, evaluates 6, with override A=4 → 12, no varying code; vector param swizzle `.zx` evaluates correctly; Lerp of two vector params by scalar param; texture param slot packs the resolver's index and sampler; static switch true/false selects branch and changes `StaticSwitchSet::Key()`; parameter metadata (group, sort priority, slider range) collected; duplicate parameter names with different types → error on the second node.
- [ ] **Steps 2–4.** **Step 5:** Commit `feat(material-compiler): uniform expressions, parameter blocks, static switches`.

### Task 8: Material functions and parameter collections in the compiler

**Files:** `Engine/Material/Compiler/src/Expressions/Functions.cpp`, `src/Expressions/Collections.cpp`; Test `Tests/Material/FunctionCompileTests.cpp`.

Function call = compile the function graph inline with its `FunctionInput` nodes bound to the caller's input chunks (or their preview default when unconnected), `FunctionOutput` pins as call outputs; chunk cache keyed by (call node, inner node) so two calls don't share; recursion depth > 16 or a function calling itself → error at the call.
- [ ] **Step 1: Failing tests** (`[compiler][function]`): function "Double" (input X, output X*2) used twice with constants 3 and 4 → folded 6 and 8; nested function; self-recursive function → error at the call node; missing input with preview default 1 → uses 1; CollectionParameter → uniform slot reading the collection, changing the collection value changes the evaluated block without recompiling (same GLSL hash).
- [ ] **Steps 2–4.** **Step 5:** Commit `feat(material-compiler): functions and parameter collections`.

### Task 9: Shader compiler module

**Files:** Create `Engine/Rendering/ShaderCompiler/{include/Luma/ShaderCompiler/ShaderCompiler.h,src/ShaderCompiler.cpp,src/ShaderCache.cpp,src/SpirvStats.cpp,CMakeLists.txt}` (`Luma::ShaderCompiler`, links `${Vulkan_INCLUDE_DIR}/../Lib/shaderc_combined.lib` (Release) / `shaderc_combinedd.lib` (Debug) found via `find_library` next to `Vulkan_LIBRARY`); Test `Tests/Rendering/ShaderCompilerTests.cpp`.

**Produces:**
```cpp
namespace Luma::ShaderCompiler {
enum class ShaderStage : u8 { Vertex, Fragment, Compute };
struct CompileRequest { std::string source, name; ShaderStage stage; std::vector<std::pair<std::string,std::string>> defines; bool optimize = true; };
struct CompileResult { bool ok = false; std::vector<u32> spirv; std::string log; std::vector<std::pair<u32,std::string>> errors; /* line, msg */ u64 key = 0; bool fromCache = false; };
struct SpirvStats { u32 instructions = 0, textureSamples = 0, samplers = 0; };
class Compiler {
public:
    Compiler(std::filesystem::path includeDir, std::filesystem::path cacheDir, u32 workers);
    CompileResult CompileSync(const CompileRequest&);
    u64 Submit(CompileRequest);                       // async ticket
    std::vector<std::pair<u64, CompileResult>> Poll(); // finished jobs (main thread)
    static SpirvStats Stats(std::span<const u32> spirv);
    static u64 CacheKey(const CompileRequest&, std::string_view includeHash);  // FNV-1a over source, stage, defines, includes, shaderc version, options
};
}
```
`#include` resolution via shaderc includer callback reading from `includeDir`; include-content hash cached per file mtime; cache file `cacheDir/<key hex>.spv`.
- [ ] **Step 1: Failing tests** (`[shadercompiler]`): trivial fragment shader compiles, SPIR-V magic 0x07230203; syntax error reports line 3 for an error on line 3; `#include "material_common.glsl"`-style include resolves from the include dir; second identical compile returns `fromCache`; changing an included file changes the key; `Submit` ×8 + `Poll` until all done returns 8 ok results; `Stats` counts ≥1 texture sample in a sampling shader.
- [ ] **Steps 2–4.** **Step 5:** Commit `feat(shadercompiler): runtime GLSL to SPIR-V with async jobs and disk cache`.

### Task 10: Pass templates and code generation

**Files:** Create shaders `Engine/Rendering/Vulkan/shaders/{material_template.vert,material_gbuffer.frag,material_depth.frag,material_selection.frag,material_forward.frag,material_postprocess.frag,material_frame.glsl,material_params.glsl,lighting_common.glsl}`; Modify `Engine/Material/Compiler/src/Translator.cpp` (`Finish()` assembles per-pass sources); Test `Tests/Material/CompileCoverageTests.cpp`.

Templates expose insertion points `// @LUMA_MATERIAL_VERTEX` (WPO + custom UVs + interpolator writes) and `// @LUMA_MATERIAL_PIXEL` (body that fills `MaterialPixel` = all output values). Common blocks: `material_frame.glsl` (set 0 binding 0 `FrameUBO { mat4 view, proj, viewProj, invViewProj, prevViewProj; vec4 camPos; vec4 time /*x time, y delta, z frame*/; vec4 viewport /*w,h,1/w,1/h*/; }`, binding 1 collections `vec4 lumaCollections[]`), `material_params.glsl` (set 1 binding 0 `texture2D lumaTextures[]`, binding 1 `sampler lumaSamplers[12]`, binding 2 `textureCube lumaCubes[]`; set 2 binding 0 `readonly buffer { vec4 lumaParams[]; }`; push constant `{ mat4 model; vec4 normalMatrix[3]; uint paramOffset; uint objectId; uint flags; uint pad; }` = 128 bytes). `lighting_common.glsl` = BRDF + light loop + shadows extracted from `deferred_lighting.frag` (used by Task 14/15).
- [ ] **Step 1: Failing test** (`[compiler][coverage]`): for every registered expression type (from `ExpressionRegistry::All()`, skipping MaterialOutput/FunctionInput/Output) build a graph that routes its first output (adapted by Append/ComponentMask) into BaseColor (pixel) and, if not pixelOnly, into WPO (vertex); compile all passes valid for Surface/Opaque, Surface/Translucent, PostProcess; every source compiles through `ShaderCompiler::CompileSync` with no errors. Expected initially: failures listing each broken type.
- [ ] **Steps 2–4.** **Step 5:** Commit `feat(material-compiler): pass templates and per-pass code generation`.

### Task 11: RHI — compiled material description

**Files:** Modify `Engine/Rendering/RHI/include/Luma/RHI/{MaterialGpuDesc.h,SceneRenderer.h,Renderer.h}`; Test `Tests/Rendering/MaterialGpuDescTests.cpp`.

**Produces:**
```cpp
enum class GpuPass : u8 { GBuffer, DepthOnly, SelectionMask, Forward, PostProcess, Count };
struct GpuShaderPair { std::vector<u32> vertex, fragment; u64 key = 0; };
struct CompiledMaterialGpu {
    u64 shaderKey = 0;                                  // identity of the compiled shader set
    std::array<std::optional<GpuShaderPair>, size_t(GpuPass::Count)> passes;
    GpuBlendMode blend; GpuShadingModel shading; bool twoSided; bool writesCustomData; bool usesWPO;
    bool usesRefraction; GpuPostProcessLocation ppLocation; i32 ppPriority;
    std::vector<GpuTextureBinding> textures;            // slot, texture handle, sRGB, sampler index, cube
};
struct MaterialParameterBlock { std::vector<Math::Vec4> values; };   // per material/instance
// SceneRenderer:
virtual MaterialHandle UpdateMaterial(u64 key, const CompiledMaterialGpu&, const MaterialParameterBlock&) = 0;  // replaces the MaterialGpuDesc overload
virtual void UpdateMaterialParameters(MaterialHandle, const MaterialParameterBlock&) = 0;                      // no pipeline work
virtual void SetCollection(u32 index, std::span<const Math::Vec4> values) = 0;
```
`GpuBlendMode` extends to {Opaque, Masked, Translucent, Additive, Modulate}; `GpuShadingModel` to {DefaultLit, Unlit, Subsurface, ClearCoat, TwoSidedFoliage}. `MaterialGpuDesc` and `MakeDefaultMaterialGpuDesc` are deleted (Default Material is a compiled graph).
- [ ] **Step 1: Failing tests** (`[rhi][material]`): enum values/counts; `CompiledMaterialGpu` default has no passes; parameter block equality.
- [ ] **Steps 2–4** (callers stop compiling — that is expected; this task updates RHI + adapters' signatures only, Vulkan implementation stubs are **not** allowed: Tasks 11–13 are committed together after Task 13 if the build can't be green in between; ledger this). **Step 5:** commit with Task 13.

### Task 12: Vulkan — textures + samplers, shared material layout, frame data

**Files:** Modify `Engine/Rendering/Vulkan/src/Vulkan/Textures/VulkanTextureCache.{h,cpp}` (binding 0 sampled images (2 views per texture), binding 1 immutable sampler table — index = filter(3) × address(3) + aniso flag rows, 12 total, binding 2 cubes), Create `src/Vulkan/Materials/VulkanMaterialLayout.{h,cpp}` (set layouts 0/1/2, pipeline layout, frame UBO ring, collections buffer, param buffer (per frame in flight, sub-allocated per material, grows by doubling)); Tests `Tests/Rendering/{VulkanTextureCacheTests.cpp,VulkanMaterialLayoutTests.cpp}`.
- [ ] **Step 1: Failing tests** (`[vulkan][material]`): sampler table has 12 entries with the expected address/filter per index; texture views linear/sRGB at 2i/2i+1 preserved; param buffer sub-allocations don't overlap and survive growth (old offsets remain valid until the frame retires); push-constant range is exactly 128 bytes; 0 validation errors.
- [ ] **Steps 2–4.** **Step 5:** commit with Task 13.

### Task 13: Vulkan — per-material pipelines in G-buffer, shadow, selection

**Files:** Create `src/Vulkan/Materials/VulkanMaterialPipelines.{h,cpp}` (cache keyed by (shaderKey, pass, cull, blend, depth, render pass id); refcounts; destroyed when unused for `kFramesInFlight` frames); Modify `VulkanMaterialSystem.{h,cpp}` (material = CompiledMaterialGpu + block offset; atomic swap of the whole pass set when all pipelines of a new shaderKey exist), `VulkanDeferredRenderer.{h,cpp}` (G-buffer adds RT4 `R8G8B8A8_UNORM` custom data; draw loops bind per-material pipelines + set 2 offset via push constant; shadow and selection passes use DepthOnly/SelectionMask pipelines; remove fixed gbuffer/shadow/selection material pipelines and `material_common.glsl` / `gbuffer.frag` / `shadow.frag` / `selection_mask.frag` once unused); Test `Tests/Rendering/MaterialRenderTests.cpp`.
- [ ] **Step 1: Failing GPU tests** (`[vulkan][render]`), offscreen 64×64 through a `VulkanDeferredRenderer` created on the fixture device: an Unlit material outputting Emissive (0.25,0.5,0.75) fills the centre pixel with that value (±1/255 after the known tone mapping disabled — use view mode Unlit/BaseColor path); a material with WPO (0,1,0) moves the object's shadow-map depth (read back the shadow map texel under it); a masked material with OpacityMask 0 writes no G-buffer and no shadow; changing only the parameter block changes the pixel without creating pipelines (pipeline count stays); compile-swap: until all new pipelines exist the old ones draw; 0 validation errors.
- [ ] **Steps 2–4.** **Step 5:** Commit Tasks 11–13 together: `feat(rendering): compiled material pipelines (RHI, layout, G-buffer/shadow/selection)`.

### Task 14: Shading models in deferred lighting

**Files:** Modify `deferred_lighting.frag` (include `lighting_common.glsl`; read RT4; branches), `VulkanDeferredRenderer.cpp` (bind RT4 at binding 10); Test `MaterialRenderTests.cpp`.

Models (implement from these definitions): **Subsurface** — Lambert diffuse + wrap lighting `max(0,(NdotL + w)/(1+w))` with w = subsurface opacity-derived (0..1) tinted by Subsurface Color, plus back transmission `pow(saturate(dot(V,-L)), 4) * subsurfaceColor * (1 - opacity)`; **Clear Coat** — base GGX layer + second GGX lobe with F0 0.04, roughness = Clear Coat Roughness, energy-attenuated base by `(1 - Fcoat * clearCoat)`; **Two-Sided Foliage** — default lit + transmission `saturate(dot(-N, L)) * subsurfaceColor` (thin). RT4 packing: rgb = subsurface colour or (clearCoat, clearCoatRoughness, 0), a = subsurface opacity.
- [ ] **Step 1: Failing GPU tests:** Subsurface sphere lit from behind brighter on the unlit side than DefaultLit with the same base colour; Clear Coat adds a second highlight (pixel at the coat highlight brighter than base-only); foliage plane lit from behind shows transmission colour; 0 validation errors.
- [ ] **Steps 2–4.** **Step 5:** Commit `feat(rendering): subsurface, clear coat and two-sided foliage shading models`.

### Task 15: Translucency (forward pass, refraction, sorting)

**Files:** Modify `VulkanDeferredRenderer.{h,cpp}` (after lighting + sky: copy HDR scene colour to a sampled copy when any visible material uses refraction; forward render pass loading HDR target + depth (read-only depth test, no write); draw translucent instances back-to-front by bounds-centre distance; blend states: Translucent = premultiplied alpha `ONE, ONE_MINUS_SRC_ALPHA` with shader outputting `color*opacity`, Additive `ONE, ONE`, Modulate `DST_COLOR, ZERO`); `material_forward.frag` lights with `lighting_common.glsl` (sun + CSM + local lights + sky IBL) and refraction offsets the scene-colour lookup (IOR: `offset = (refraction - 1) * normalVS.xy * scale`; PixelOffset: offset by normal.xy * refraction in pixels); Test `MaterialRenderTests.cpp`.
- [ ] **Step 1: Failing GPU tests:** red translucent quad (opacity 0.5) over a white opaque quad → centre ≈ 50% blend; two translucent quads → farther drawn first (colour order correct both ways); Additive adds; Modulate multiplies; refraction shifts a striped background (pixel differs from non-refracted); translucent quad in sun shadow darker than in light; 0 validation errors.
- [ ] **Steps 2–4.** **Step 5:** Commit `feat(rendering): forward translucency with refraction`.

### Task 16: Post-process materials

**Files:** Modify `Engine/Scene/include/Luma/Scene/Components.h` (`PostProcessComponent::materials` — vector of {path, weight, enabled}), scene (de)serialization + Inspector rows (list with add/remove/asset combo); RHI `PostProcessParams::materials` (handles + weights); Vulkan: passes before tone mapping (after lighting/translucency, before bloom) and after tone mapping (after bloom composite, before FXAA), ping-pong targets; Scene Texture inputs bound (scene colour copy, depth, G-buffer RTs, post-process input 0 = previous pass); Test `MaterialRenderTests.cpp`, `Tests/Scene/SceneSerializerTests.cpp`.
- [ ] **Step 1: Failing tests:** PostProcessComponent materials round-trip; a PP material outputting `1 - SceneColor` inverts the final image (after-tonemapping location); priority ordering of two PP materials; disabled entry skipped; 0 validation errors.
- [ ] **Steps 2–4.** **Step 5:** Commit `feat(rendering): post-process materials`.

### Task 17: Parameter collections at runtime

**Files:** `Editor/MaterialBridge` (collection index allocation by path; upload on revision change), `VulkanMaterialLayout` collections buffer; Test `Tests/Editor/MaterialBridgeTests.cpp`.
- [ ] **Step 1: Failing tests:** two materials reading collection `/Game/MPC_Wind.Strength` get the same value; `SetCollectionValue` updates the GPU buffer next frame without recompiling; deleting a collection asset → materials using it report an error and render last good.
- [ ] **Steps 2–4.** **Step 5:** Commit `feat(material): runtime parameter collections`.

### Task 18: Editor bridge — compile orchestration

**Files:** Modify `Editor/MaterialBridge/{include,src}/MaterialSceneBridge.*`; Test `Tests/Editor/MaterialBridgeTests.cpp`.

Flow: material change → `Translator` (main thread, fast) → per-pass `ShaderCompiler::Submit` → on all passes done: `UpdateMaterial(key, compiled, block)`; instances → `EvaluatePreshader` → `UpdateMaterialParameters`; static-switch overrides → own permutation key `(parent path, switch key)`; errors → `LUMA_USER_ERROR("Materials", "<path>: <node type> — <message>")` once per distinct error set, keep last good; first compile → Default Material handle with `LUMA_USER_INFO("Materials", "Compiling <path>…")` and "Compiled <path> (<n> instructions)"; dependency invalidation via `MaterialLibrary::Dependents`.
- [ ] **Step 1: Failing tests** (with a fake `SceneRenderer` recording calls): instance parameter edit → `UpdateMaterialParameters` only; static switch override → one new compile, second instance with the same switch reuses it; function edit recompiles its dependents only; erroneous edit keeps the previous handle's shaders and logs once; Default Material used until first compile completes.
- [ ] **Steps 2–4.** **Step 5:** Commit `feat(editor): material compile orchestration`.

### Task 19: Material preview, thumbnails and the interim Material Editor panel

**Files:** `Editor/LumaEditor/MaterialPreview.*` (uses compiled pipelines via the bridge), `Editor/panels/MaterialEditor/**` (replace fixed input rows with: Settings (domain, blend, shading, two-sided, clip, translucency, refraction, PP location/priority), **Parameters** grouped by group/sort priority with typed widgets (SpinBox with slider range, colour picker, texture combo, static switch checkbox), **Errors & Stats** section (errors with node type + message; per-pass instruction/sampler counts); instance mode edits overrides with reset-to-parent arrows); Test `Tests/Editor/MaterialEditorTests.cpp`.
- [ ] **Step 1: Failing tests:** parameters listed in group/sort order; editing a scalar param on an instance writes an override and calls the bridge parameter path; reset-to-parent removes the override; errors section shows a compile error string; settings change triggers recompile.
- [ ] **Steps 2–4.** Visual check: Material Editor screenshot on MaterialShowcase M_Bricks-like material. **Step 5:** Commit `feat(editor): material preview + parameter-driven Material Editor panel`.

### Task 20: Content Browser asset kinds

**Files:** `Editor/panels/ContentBrowser/**` (create menu: Material Function, Parameter Collection; icons/type names), `Editor/LumaEditor/EditorScreen.cpp` (file watcher reloads `.lmatfunc`/`.lmpc`), `Engine/Asset/src/AssetType.cpp` (**coordinate with T-0003:** add `lmatfunc`/`lmpc` extensions to `MaterialFunction`/`ParameterCollection` asset types via a small request to the engine agent or a lead-made commit after T-0003 lands — record which); Tests `Tests/Asset/AssetTypeTests.cpp` (or existing).
- [ ] **Step 1: Failing tests:** extensions map to the new types; creating a Material Function from the menu writes a valid `.lmatfunc` with one FunctionOutput.
- [ ] **Steps 2–4.** **Step 5:** Commit `feat(editor): material function and parameter collection assets`.

### Task 21: Fixtures and visual verification

**Files:** `Tests/Fixtures/MaterialShowcase/Assets/Materials/*` (migrated on load; commit the v4 files), new `Tests/Fixtures/MaterialGraphShowcase/` project: `M_Lava` (Panner + Time emissive), `M_LayeredRock` (two texture sets blended by a mask + height lerp), `M_Glass` (Translucent, IOR refraction, Fresnel opacity), `M_Foliage` (Masked, Two-Sided Foliage, WPO wind from `MPC_Wind`), `M_CarPaint` (Clear Coat, instance `MI_CarPaint_Blue`), `PP_Outline` (post-process edge detect from SceneDepth), `MF_Wind` function used by foliage; Test `Tests/Rendering/MaterialGoldenTests.cpp`.
- [ ] **Step 1: Failing test:** render MaterialShowcase and each preview → compare to Task 0 goldens: mean absolute error < 2/255 per channel, max < 24/255 (POM/bump differences allowed only within that); 0 validation errors.
- [ ] **Steps 2–3:** fix differences until green (each fix ledgered). **Step 4:** Manual: screenshot MaterialGraphShowcase (each material visible and correct: lava scrolls between two captures 1 s apart, glass refracts, foliage casts animated shadows, car paint has two highlights, outline visible). **Step 5:** Commit `test: material graph showcase and golden comparisons`.

### Task 22: Docs and final review

- [ ] Update `GOAL.md` §2.4/§5.10 (graph materials, compiler, domains, shading models, collections, functions; open: decals/UI/light functions/volumes, cloth/hair/eye, graph editor (project 2)). Update `AgentTasks/BOARD.md` "Lead is working on". Commit `docs: material graph & compiler`.
- [ ] Final whole-branch review (fresh reviewer, Review Focus above), fix pass, merge `feature/material-graph` → `dev` → push; offer `dev` → `main` after the user tests.
