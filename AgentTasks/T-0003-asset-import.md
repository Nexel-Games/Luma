# T-0003 — Asset Import Pipeline & Import Window

**Status:** In Progress
**Assigned to:** engine agent
**Created:** 2026-11-20 by lead (planned with the user)
**Design (read first):** `docs/superpowers/specs/2026-11-20-asset-import-pipeline-design.md`
**Area / files:** `CMakeLists.txt`, `cmake/LumaDependencies.cmake`, `Engine/Asset/**`,
`Engine/Audio/**` (new), `Editor/panels/ImportDialog/**` (new),
`Editor/panels/ContentBrowser/**`, `Editor/LumaEditor/EditorScreen.*` (import wiring
only), `Editor/LumaEditor/main.cpp` (audio init only), `Tools/ImportFixtureGen/**`
(new), `Tests/Asset/**`, `Tests/Audio/**` (new), `Tests/Editor/ImportDialog*`,
`Tests/Fixtures/Import/**`, `Tests/CMakeLists.txt`, `GOAL.md`.
**Do not touch:** `Engine/Material/**` (use its public API only), `Engine/Rendering/**`,
`Editor/MaterialBridge/**`, `Editor/panels/MaterialEditor/**`,
`Editor/LumaEditor/MaterialPreview.*`.

## Goal

Unreal-style importing. The user picks files (button, context menu, or drag & drop
from Windows Explorer); an **Import window** lists what each file contains with
checkboxes plus per-type settings; the importer creates engine assets (meshes with
their materials and textures, textures, sounds) with `.meta` sidecars; the Content
Browser then shows them **with thumbnails** (3D render, image, waveform) and sounds can
be previewed. Formats: glTF, GLB, FBX, OBJ · WAV, MP3 · PNG, JPEG, TGA, BMP, PSD,
HDR, EXR.

## How Unreal does it (summary — you do not have the UE source; this is all you need)

Unreal 5's **Interchange** framework splits importing into three stages. We mirror
the *structure*, in our own code:
1. **Translators** — one per format family. A translator reads a source file and
   describes its contents as a tree of neutral *nodes* (scene nodes, mesh nodes,
   material nodes, texture nodes, audio nodes) in a node container. Heavy data
   (vertex buffers, pixels, samples) is a *payload* fetched only when an asset is
   actually created. Translators never create assets.
2. **Pipelines** — configurable objects whose public settings are what the import
   dialog shows. The *generic assets pipeline* aggregates sub-pipelines (common,
   meshes, materials, textures, audio). A pipeline walks the translated nodes and
   emits *factory nodes*: "create a static mesh named X from mesh nodes A, B with these
   options". Disabling a node in the dialog removes its factory node.
3. **Factories** — one per asset type; they turn a factory node + payload into the
   asset file. After all factories run, the asset registry is updated and thumbnails
   are generated.

The **import dialog** (`SInterchangePipelineConfigurationDialog`) is a modal window:
a header (destination path, preset/"pipeline stack" selector), a **left tree** with
every source file and its contents (meshes, materials, textures, audio) each with a
checkbox and a type icon, a **right details panel** showing the pipeline settings
grouped in collapsible categories (only settings relevant to the content are visible),
a **conflict area** when destination assets already exist, and buttons **Import**,
**Import All** (apply to all remaining files of the batch), **Cancel**, plus **Reset to
Defaults**. Reimport reopens the dialog with the settings stored in the asset.

## Global constraints

- Read `AgentTasks/README.md` rules. Production quality; no stubs, placeholders, or
  settings that do nothing.
- Luma coordinate convention: **right-handed, +Y up, metres**, front = −Z (glTF's
  convention). Verify in Task 4 Step 1 with a test before relying on it.
- Use **only** `ThirdParty/` libraries: Assimp (`ThirdParty/assimp`, v6.0.5),
  miniaudio (`ThirdParty/miniaudio`), tinyexr (`ThirdParty/tinyexr.h` + `miniz.h`),
  stb (`stb_image.h` already fetched by `Engine/Asset/CMakeLists.txt`). No new
  downloads.
- User-facing messages `LUMA_USER_INFO/WARN/ERROR` with category `"Import"`;
  developer logs `LUMA_LOG_*`.
- Build: `MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*" cmd.exe /c "Scripts\\_env.bat cmake --build build --config Development"`
  (close `LumaEditor.exe` first). Tests: `./build/bin/Development/LumaTests.exe "<tag>"`.
- All new tests tagged `[import]` (plus a sub-tag). Whole suite green before every push.
- Fixtures already committed in `Tests/Fixtures/Import/` (see the header of
  `make_fixtures.py` for their exact contents); never edit them by hand.

## Review focus (the lead will check these hardest)

1. FBX in centimetres / Z-up imports at the same size and orientation as the glTF.
2. Shared textures/materials referenced by several meshes are created once.
3. Unchecking an item really skips it (no file written); "Skip" conflicts never
   overwrite.
4. Cancel mid-import leaves no half-written files (write to temp, then rename).
5. Reimport reuses the stored settings and keeps the asset's GUID (scene references
   stay valid).

---

## Task 1 — Dependencies from ThirdParty

**Files:** `cmake/LumaDependencies.cmake`, `Engine/Asset/CMakeLists.txt`,
`Engine/Audio/CMakeLists.txt` (new, skeleton only here), root `CMakeLists.txt`.

- [x] **Step 1:** Replace the Assimp `FetchContent_Declare` / `luma_require_assimp` with
  `ThirdParty/assimp`:
  ```cmake
  function(luma_require_assimp)
      if(NOT TARGET assimp)
          set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
          set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
          set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
          set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
          set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
          set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
          set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
          set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
          foreach(fmt OBJ FBX GLTF)
              set(ASSIMP_BUILD_${fmt}_IMPORTER ON CACHE BOOL "" FORCE)
          endforeach()
          foreach(fmt FBX GLTF OBJ)   # exporters: fixture generation only
              set(ASSIMP_BUILD_${fmt}_EXPORTER ON CACHE BOOL "" FORCE)
          endforeach()
          add_subdirectory(${PROJECT_SOURCE_DIR}/ThirdParty/assimp
                           ${CMAKE_BINARY_DIR}/ThirdParty/assimp EXCLUDE_FROM_ALL)
          set_target_properties(assimp PROPERTIES FOLDER "ThirdParty")
      endif()
  endfunction()
  ```
  Keep the MSVC `/EHsc` line. Delete the old assimp `FetchContent_Declare`.
- [x] **Step 2:** Add `luma_require_miniaudio()`: a static library `luma_miniaudio` built from
  `ThirdParty/miniaudio/miniaudio.c`, include dir `ThirdParty/miniaudio`, compile
  definitions `MA_NO_GENERATION` and `MA_NO_ENCODING` (we decode + play only),
  warnings off for that target only.
- [x] **Step 3:** Add `luma_require_tinyexr()`: a static library `luma_tinyexr` from a new
  `ThirdParty/tinyexr_impl.cpp` containing
  ```cpp
  #define TINYEXR_USE_MINIZ 1
  #define TINYEXR_IMPLEMENTATION
  #include "miniz.h"
  #include "tinyexr.h"
  ```
  (add `ThirdParty/miniz.c` only if the build reports missing miniz symbols; the
  single-header `miniz.h` in ThirdParty may need `MINIZ_HEADER_FILE_ONLY` undefined —
  check the header and document what you did in *Result*).
- [x] **Step 4:** Reconfigure + full build. Expected: builds; `LumaTests.exe "[asset]"` passes
  (existing `MeshImporterTests` now run on Assimp 6). If an Assimp-6 behaviour change
  breaks an existing test, fix the code (not the test) and note it under Deviations.
- [x] **Step 5:** Commit `build: assimp/miniaudio/tinyexr from ThirdParty (T-0003)`.

## Task 2 — FBX fixture

**Files:** `Tools/ImportFixtureGen/main.cpp`, `Tools/ImportFixtureGen/CMakeLists.txt`,
root `CMakeLists.txt` (add `Tools/ImportFixtureGen` behind option
`LUMA_BUILD_TOOLS`, default ON), `Tests/Fixtures/Import/robot.fbx`.

- [x] **Step 1:** Write the tool: load `robot.gltf` with `Assimp::Importer`, then before export
  set FBX unit metadata so the file is **centimetres, Z-up**:
  scale every root node by 100 and rotate +90° about X (Y-up → Z-up), and set
  `scene->mMetaData` keys `UnitScaleFactor` = 1.0 (cm), `UpAxis` = 2,
  `UpAxisSign` = 1, `FrontAxis` = 1, `FrontAxisSign` = -1, `CoordAxis` = 0,
  `CoordAxisSign` = 1. Export with `Assimp::Exporter::Export(scene, "fbx", out)`.
  Usage: `ImportFixtureGen <in.gltf> <out.fbx>`.
- [x] **Step 2:** Build, run it to produce `Tests/Fixtures/Import/robot.fbx`, then verify by
  loading it back with Assimp and printing node names, mesh count (2), material
  names (Paint, Metal) and the metadata keys. If the exporter does not write the
  metadata, record that in *Result* — Task 4's unit/axis tests are designed to work
  from metadata passed to a pure function, so they still cover the conversion.
- [x] **Step 3:** Commit the tool and `robot.fbx`: `test: FBX import fixture (T-0003)`.

## Task 3 — Import scene, settings schema, metadata

**Files:** `Engine/Asset/include/Luma/Asset/Import/ImportScene.h`,
`.../Import/ImportSettings.h`, `.../Import/ImportMetadata.h` and `src/Import/*.cpp`;
tests `Tests/Asset/Import/ImportSceneTests.cpp`.

**Interfaces (produce):**
```cpp
namespace Luma::Import {
enum class NodeKind : u8 { File, Mesh, Material, Texture, Sound };
using NodeId = u32;                       // index into ImportScene::nodes

struct TextureRef { NodeId texture = kNone; };   // kNone = static constexpr NodeId(-1)

struct MaterialDesc {                     // neutral PBR description
    Math::Vec4 baseColor{1, 1, 1, 1};
    f32 metallic = 0.0f, roughness = 0.5f, specular = 0.5f;
    Math::Vec3 emissive{0, 0, 0};
    f32 opacityMaskClip = 0.0f;           // > 0 = masked (alpha test)
    bool twoSided = false;
    TextureRef baseColorTex, normalTex, metallicRoughnessTex /*G=rough, B=metal*/,
               occlusionTex, emissiveTex, opacityTex;
};

struct ImportNode {
    NodeKind kind;
    std::string name;                     // source name ("Body", "Paint", "T_Paint_D")
    std::filesystem::path sourceFile;     // file this node came from
    NodeId parent = kNone;                // the File node this node belongs to (kNone for File nodes)
    bool enabled = true;                  // dialog checkbox
    std::string error;                    // non-empty = cannot import (shown red, disabled)
    // kind-specific
    std::vector<NodeId> meshMaterials;    // Mesh: one material node per section
    u32 meshVertexCount = 0, meshTriangleCount = 0;
    MaterialDesc material;                // Material
    std::filesystem::path textureFile;    // Texture: external file, or empty if embedded
    std::vector<u8> textureEmbedded;      // Texture: encoded bytes (png/jpg) when embedded
    std::string textureEmbeddedExt;       // "png" / "jpg"
    bool textureIsNormalMap = false;      // usage hint from the material slot
    bool textureIsLinear = false;         // metallic/roughness/AO/normal → linear
    u32 soundChannels = 0, soundSampleRate = 0; f64 soundSeconds = 0.0;   // Sound
};

struct ImportScene {
    std::vector<ImportNode> nodes;
    NodeId AddNode(ImportNode n);
    std::vector<NodeId> Children(NodeId file) const;   // nodes whose parent == file
    std::vector<NodeId> OfKind(NodeKind k) const;
    // Merge another scene (multi-file batch), remapping node ids.
    void Append(const ImportScene& other);
};

// Settings schema (drives the dialog generically)
enum class FieldType : u8 { Bool, Float, Int, Enum, Vec3, String };
struct SettingField {
    std::string key, label, tooltip, category;   // category = "Common", "Meshes", ...
    FieldType type;
    f64 min = 0, max = 0, step = 0;              // numeric ranges (0/0 = unbounded)
    std::vector<std::string> enumValues;         // Enum labels
    std::string visibleIfKey;                    // show only when this Bool key is true
};
class SettingsBlock {                            // typed key/value store + schema
public:
    void Define(SettingField field, Json defaultValue);   // Json = existing Serialization JSON value type
    const std::vector<SettingField>& Fields() const;
    bool GetBool(std::string_view) const; f64 GetFloat(std::string_view) const;
    i64 GetInt(std::string_view) const; std::string GetString(std::string_view) const;
    Math::Vec3 GetVec3(std::string_view) const;
    void Set(std::string_view key, Json value);  // validates type + clamps range
    Json ToJson() const; void FromJson(const Json&); // unknown keys ignored, missing = default
    void ResetToDefaults();
};

// .meta sidecar written next to every imported asset
struct ImportMetadata {
    AssetId guid;                         // stable across reimports
    AssetType type;
    std::string sourceFile;               // project-relative if inside project, else absolute
    std::string sourceHash;               // FNV-1a 64 of file bytes, hex
    i64 sourceMtime = 0;
    std::string importer;                 // "Luma.Import.Model" etc.
    u32 importerVersion = 1;
    std::string sourceNodeName;           // which node of the source file
    Json settings;                        // SettingsBlock::ToJson of the pipelines used
};
bool WriteMetadata(const std::filesystem::path& assetFile, const ImportMetadata&, std::string* err);
std::optional<ImportMetadata> ReadMetadata(const std::filesystem::path& assetFile);
std::filesystem::path MetadataPathFor(const std::filesystem::path& assetFile); // "<file>.meta"
std::string HashFile(const std::filesystem::path&);
}
```
(Use the engine's existing JSON type from `Engine/Serialization` — check its header for
the actual class name and use it where this plan says `Json`.)

- [x] **Step 1: Failing tests** (`[import][scene]`): AddNode/Children/OfKind; Append remaps
  `meshMaterials`/`TextureRef` ids; SettingsBlock defaults, Set clamps a Float to
  `[min,max]`, wrong type rejected (value unchanged), JSON round-trip, unknown key
  ignored; Metadata write→read round-trip; `MetadataPathFor("a/T_X.png") ==
  "a/T_X.png.meta"`; `HashFile` stable for the same bytes and different after a 1-byte
  change.
- [x] **Step 2:** Run → compile failure. **Step 3:** Implement. **Step 4:** Pass; suite green.
- [x] **Step 5:** Commit `feat(asset): import scene, settings schema, metadata (T-0003)`.

## Task 4 — Model translator (glTF, GLB, FBX, OBJ)

**Files:** `Engine/Asset/include/Luma/Asset/Import/Translators/ModelTranslator.h`,
`src/Import/Translators/ModelTranslator.cpp`, `.../AxisConversion.{h,cpp}`;
tests `Tests/Asset/Import/ModelTranslatorTests.cpp`.

**Interfaces:**
```cpp
namespace Luma::Import {
class ITranslator {
public:
    virtual ~ITranslator() = default;
    virtual std::string_view Name() const = 0;                 // "Luma.Import.Model"
    virtual bool CanTranslate(const std::filesystem::path&) const = 0;
    virtual bool Translate(const std::filesystem::path& file, ImportScene& out,
                           std::string* error) = 0;          // adds File node + children
};

struct ModelPayloadOptions {           // from the Meshes pipeline settings
    f32 uniformScale = 1.0f;
    Math::Vec3 offsetTranslation{0,0,0}, offsetRotationDeg{0,0,0};
    bool importNormals = true;          // false = always compute
    f32 smoothingAngleDeg = 66.0f;      // when computing normals
    bool computeTangents = true;
    bool flipUVs = false;               // extra flip on top of convention fix
    bool combineMeshes = false;
    enum class Pivot : u8 { Source, BoundsCenter, BoundsBottom } pivot = Pivot::Source;
    enum class AxisOverride : u8 { Auto, YUp, ZUp } axis = AxisOverride::Auto;
};

class ModelTranslator final : public ITranslator {
public:
    // ITranslator ... extensions: gltf glb fbx obj
    // Payload: build engine meshes for the given mesh nodes of a translated file.
    // combineMeshes → one LumaMeshData with one section per material; otherwise one per node.
    std::vector<LumaMeshData> BuildMeshes(const std::filesystem::path& file,
                                          const std::vector<std::string>& meshNodeNames,
                                          const ModelPayloadOptions&, std::string* error);
};

// Pure: source axis/unit description → transform into Luma (Y-up, -Z front, metres).
struct SourceAxes { int up = 1, upSign = 1, front = 2, frontSign = 1, coord = 0, coordSign = 1;
                    f64 unitScaleCm = 100.0; };    // FBX semantics; glTF/OBJ: Y-up metres
Math::Mat4 SourceToLuma(const SourceAxes&);
SourceAxes AxesFromAssimpMetadata(const aiScene&); // defaults when keys absent
}
```

**Our reference for `SourceToLuma`** (write it yourself from this description):
FBX stores three axes — *up*, *front* (parity), *coord* (right) — each an index 0/1/2
(X/Y/Z) and a sign. Build a 3×3 matrix whose **columns** are the Luma basis vectors
expressed in source space: Luma +X = source `coord` axis × `coordSign`; Luma +Y =
source `up` axis × `upSign`; Luma +Z (towards viewer) = source `front` axis ×
`frontSign` × −1 (FBX front points *into* the screen for −Z conventions — verify
with the fixture: the glTF and FBX bounds must match). The source→Luma rotation is the
**transpose** of that matrix. Scale = `unitScaleCm / 100` (cm→m). Return
`Scale * Rotation` as a 4×4. Then (separately, in `BuildMeshes`) apply the user
offset transform and uniform scale.

Assimp usage notes:
- Read flags: `aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
  aiProcess_SortByPType | aiProcess_ValidateDataStructure`
  `| aiProcess_GenUVCoords | aiProcess_FlipUVs` (Luma UV origin is top-left, like
  glTF/D3D; OBJ/FBX are bottom-left — Assimp's flip makes all formats top-left; verify
  with the fixture UVs).
- When `importNormals` is false or the mesh has none: `aiProcess_GenSmoothNormals`
  with `importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, angle)` and
  `aiProcess_RemoveComponent` (`AI_CONFIG_PP_RVC_FLAGS = aiComponent_NORMALS`) to drop
  imported ones first.
- Tangents: `aiProcess_CalcTangentSpace`; Luma bitangent convention = `cross(N,T) * w`
  points image-up (see `LumaVertex` + existing `MeshImporter.cpp` conversion — reuse its
  vertex conversion code, then delete `MeshImporter` in Task 12).
- Materials: glTF PBR via `AI_MATKEY_BASE_COLOR`, `AI_MATKEY_METALLIC_FACTOR`,
  `AI_MATKEY_ROUGHNESS_FACTOR`, textures `aiTextureType_BASE_COLOR` /
  `aiTextureType_DIFFUSE`, `aiTextureType_NORMALS`, `aiTextureType_GLTF_METALLIC_ROUGHNESS`
  (or `aiTextureType_UNKNOWN` per Assimp version), `aiTextureType_AMBIENT_OCCLUSION` /
  `aiTextureType_LIGHTMAP`, `aiTextureType_EMISSIVE`, `aiTextureType_OPACITY`.
  Legacy (OBJ/FBX): `AI_MATKEY_COLOR_DIFFUSE` → baseColor; shininess `Ns` →
  `roughness = sqrt(2 / (Ns + 2))` clamped [0.04, 1]; `Ks` luminance > 0.5 and no
  diffuse texture → metallic 1 (document this heuristic in a code comment);
  `aiTextureType_HEIGHT` in OBJ `bump`/`norm` → normal map.
- Embedded textures: `scene->GetEmbeddedTexture(path)`; compressed (`mHeight == 0`)
  → copy `pcData` bytes, extension from `achFormatHint`; uncompressed → encode PNG with
  `stb_image_write` (already available via the stb fetch).
- External textures: resolve relative to the source file; if missing, also try the
  file's folder `textures/`, `Textures/`, `images/`; still missing → Texture node with
  `error = "Texture '<name>' not found next to <file>"`.
- A texture used by several materials becomes **one** Texture node (dedupe by resolved
  path / embedded index).

- [ ] **Step 1: Convention test first** (`[import][model]`): "Luma is Y-up right-handed":
  assert with `Math::LookAt` / `Math::Perspective` (from `Engine/Math`) that a point at
  +Y projects above the screen centre and a camera at +Z looking at the origin sees +X
  on the right. If this fails, stop and report (Blocked).
- [ ] **Step 2: Failing tests:**
  - `robot.gltf` and `robot.glb`: File node + 2 Mesh (Body 24 v / 12 tris, Arm), 2
    Material (Paint: baseColorTex + normalTex set, metallic 0, roughness 0.6; Metal:
    baseColor 0.8, metallic 1, roughness 0.3, no textures), 2 Texture nodes
    (T_Paint_D, T_Paint_N; GLB ones embedded with `textureEmbeddedExt == "png"`;
    normal map flagged `textureIsNormalMap` + `textureIsLinear`).
  - `robot.obj`: same counts/names; Paint has both textures (diffuse + normal).
  - `robot.fbx`: same names; `BuildMeshes` world bounds of Body ≈ glTF's
    (min (−0.5, 0, −0.5), max (0.5, 1, 0.5)) within 1e-3 → proves cm/Z-up conversion.
  - `SourceToLuma` pure tests: identity for Y-up/metres; Z-up maps source (0,0,1) to
    Luma (0,1,0); cm scale 100 → 0.01 factor (i.e. `unitScaleCm = 1` → 0.01).
  - `BuildMeshes(combineMeshes=true)` → 1 mesh, 2 sections, 48 vertices; false → 2
    meshes; `pivot=BoundsBottom` puts min Y at 0; UV of Body vertex 0 matches the glTF
    UV exactly (top-left origin) for all three formats.
  - Missing external texture (copy robot.gltf to a temp dir without PNGs) → Texture
    node has an error, translation still succeeds.
- [ ] **Step 3:** Run → fail. **Step 4:** Implement. **Step 5:** Pass; suite green.
- [ ] **Step 6:** Commit `feat(import): model translator for gltf/glb/fbx/obj (T-0003)`.

## Task 5 — Image translator (PNG, JPEG, TGA, BMP, PSD, HDR, EXR)

**Files:** `.../Import/Translators/ImageTranslator.{h,cpp}`, `.../Import/ImageDecode.{h,cpp}`;
tests `Tests/Asset/Import/ImageTranslatorTests.cpp`.

```cpp
namespace Luma::Import {
struct DecodedImage {
    u32 width = 0, height = 0, channels = 0;   // channels after decode: 1..4
    bool isFloat = false;                       // HDR/EXR
    std::vector<u8> pixels8;                    // when !isFloat, RGBA8 (always 4 ch)
    std::vector<f32> pixelsF;                   // when isFloat, RGBA32F (always 4 ch)
};
std::optional<DecodedImage> DecodeImageFile(const std::filesystem::path&, std::string* err);
std::optional<DecodedImage> DecodeImageMemory(std::span<const u8>, std::string_view ext, std::string* err);
class ImageTranslator final : public ITranslator { /* one File node + one Texture node */ };
}
```
stb: `stbi_load` (png/jpg/tga/bmp/psd composite), `stbi_loadf` (hdr). EXR: tinyexr
`LoadEXR(&rgba, &w, &h, path, &err)` (frees with `free`). Texture node colour-space
hint: `IsTextureSRGB()` from `TextureColorSpace.h` (HDR/EXR → linear).

- [ ] **Step 1: Failing tests** (`[import][image]`): for each fixture image: size 32×16,
  pixel (3, 2) == (24, 32, 128) (JPEG within ±6 per channel), alpha (3,2) == 24 for
  `img_rgba.png`/`img.tga`, 255 for the others; `img.hdr`/`img.exr` 4×2 float pixel
  (0,0) == (2.0, 0.5, 0.25) ±1e-3 (HDR within ±2% — RGBE quantisation); translator
  gives one Texture node named after the file stem; corrupt file (write 10 random
  bytes to a temp `.png`) → `Translate` returns false with a message.
- [ ] **Steps 2-4:** fail → implement → pass; suite green.
- [ ] **Step 5:** Commit `feat(import): image translator (T-0003)`.

## Task 6 — Audio module + audio translator (WAV, MP3)

**Files:** new module `Engine/Audio/` (`include/Luma/Audio/AudioDecode.h`,
`AudioPreview.h`, `src/*.cpp`, `CMakeLists.txt` → `Luma::Audio`, links
`luma_miniaudio`), `Engine/Asset/.../Import/Translators/AudioTranslator.{h,cpp}`
(links `Luma::Audio`); tests `Tests/Audio/AudioDecodeTests.cpp`,
`Tests/Asset/Import/AudioTranslatorTests.cpp`.

```cpp
namespace Luma::Audio {
struct SoundInfo { u32 channels = 0, sampleRate = 0; u64 frames = 0; f64 seconds = 0.0; };
std::optional<SoundInfo> ProbeSound(const std::filesystem::path&, std::string* err);
// Min/max peaks per column over all channels, normalised to [-1, 1]; `columns` buckets.
struct WaveformPeaks { std::vector<f32> min, max; };
std::optional<WaveformPeaks> ComputePeaks(const std::filesystem::path&, u32 columns, std::string* err);

class AudioPreview {             // editor preview playback (one sound at a time)
public:
    bool Init(std::string* err);    // creates the miniaudio engine on the default device
    void Shutdown();
    bool Play(const std::filesystem::path&, f32 volume = 1.0f, f32 pitch = 1.0f, bool loop = false);
    void Stop();
    bool IsPlaying() const;
    const std::filesystem::path& Current() const;
};
}
```
miniaudio: decode with `ma_decoder_init_file` + `ma_decoder_get_length_in_pcm_frames`
+ `ma_decoder_read_pcm_frames` (request `ma_format_f32` output); preview with
`ma_engine_init` / `ma_sound_init_from_file` / `ma_sound_start` / `ma_sound_stop` /
`ma_sound_set_volume` / `ma_sound_set_pitch` / `ma_sound_set_looping`. `Init` failing
(no audio device) must not crash — preview is then unavailable and a single
`LUMA_USER_WARN("Audio", ...)` explains it.

**Peaks algorithm (ours):** frames per column = `ceil(frames / columns)`; for each
column read that many frames, track min/max of every sample of every channel; empty
trailing columns = 0.

- [ ] **Step 1: Failing tests** (`[import][audio]`): `tone_mono.wav` → 1 ch, 44100 Hz,
  seconds 1.0 ±0.01; `tone_stereo.mp3` → 2 ch, 44100 Hz, seconds 1.0 ±0.06 (encoder
  padding); `ComputePeaks(wav, 64)` → 64 columns, every max in [0.9, 1.0] and min in
  [−1.0, −0.9] (full-scale sine); corrupt file → nullopt + message; translator gives one
  Sound node with the probed info. Do **not** create an audio device in tests.
- [ ] **Steps 2-4:** fail → implement → pass; suite green.
- [ ] **Step 5:** Commit `feat(audio): decode/probe/peaks + preview; audio translator (T-0003)`.

## Task 7 — Pipelines (settings → asset requests)

**Files:** `.../Import/Pipelines/{CommonPipeline,MeshPipeline,MaterialPipeline,TexturePipeline,AudioPipeline,ImportPlan}.{h,cpp}`;
tests `Tests/Asset/Import/PipelineTests.cpp`.

```cpp
namespace Luma::Import {
enum class ConflictPolicy : u8 { Overwrite, Skip, KeepBoth };
struct AssetRequest {                      // a factory node
    AssetType type;                        // Mesh, Material, Texture, Sound
    std::filesystem::path destFile;        // final file in the project
    std::vector<NodeId> sources;           // nodes this asset is built from
    bool exists = false;                   // destination already exists
    ConflictPolicy policy = ConflictPolicy::KeepBoth;
};
struct ImportPlan {                        // output of all pipelines
    std::vector<AssetRequest> requests;
    std::vector<std::string> warnings;
};
class PipelineStack {                      // the dialog edits these settings
public:
    PipelineStack();                       // defines every block's fields + defaults
    SettingsBlock& Common(); SettingsBlock& Meshes(); SettingsBlock& Materials();
    SettingsBlock& Textures(); SettingsBlock& Audio();
    std::vector<SettingsBlock*> Blocks();  // in display order
    ImportPlan BuildPlan(const ImportScene&, const std::filesystem::path& destFolder) const;
    Json ToJson() const; void FromJson(const Json&);
};
std::string SanitizeAssetName(std::string_view);   // [A-Za-z0-9_], spaces→_, collapse, trim, never empty ("Asset")
}
```
Settings to define (key — label — type/range — default):
- Common: `prefixMesh` "Mesh Prefix" String "SM_"; `prefixMaterial` "M_"; `prefixTexture`
  "T_"; `prefixSound` "S_"; `conflict` "If Asset Exists" Enum [Overwrite, Skip, Keep Both]
  = Keep Both; `subfolders` "Create Sub-folders (Meshes/Materials/Textures)" Bool false.
- Meshes: `importMeshes` Bool true; `combineMeshes` Bool false; `uniformScale` Float
  [0.0001, 1000] 1; `offsetTranslation` Vec3 0; `offsetRotation` Vec3 0 (degrees);
  `axis` Enum [Auto, Y Up, Z Up] Auto; `importNormals` Bool true; `smoothingAngle`
  Float [0, 180] 66 (visibleIf `!importNormals` — implement visibleIf as "key is
  false" when the key starts with `!`); `computeTangents` Bool true; `flipUVs` Bool
  false; `pivot` Enum [Source, Bounds Center, Bounds Bottom] Source.
- Materials: `materialMode` Enum [Create New, Use Existing, Do Not Import] Create New.
- Textures: `importTextures` Bool true; `colorSpace` Enum [Auto, sRGB, Linear] Auto;
  `generateMips` Bool true; `maxSize` Enum [None, 256, 512, 1024, 2048, 4096, 8192] None;
  `filter` Enum [Linear, Nearest] Linear; `address` Enum [Wrap, Clamp, Mirror] Wrap.
- Audio: `loop` Bool false; `volume` Float [0, 4] 1; `pitch` Float [0.1, 4] 1.

Naming: mesh `prefixMesh + Sanitize(node.name)` (combined: file stem); material
`prefixMaterial + Sanitize(name)`; texture `prefixTexture + Sanitize(stem)` unless the
stem already starts with the prefix (`T_Paint_D` stays `T_Paint_D`); sound likewise.
Extensions: `.lmesh`, `.lmat`, texture keeps its source extension (embedded → its
`textureEmbeddedExt`; PSD → `.png`), sound keeps `.wav`/`.mp3`.
Rules: disabled nodes produce no request; a Texture used by an enabled Material or
enabled on its own produces exactly one request; `materialMode == Use Existing` →
material requests are replaced by lookups of `<dest>/<name>.lmat` (warning if absent →
falls back to Create New); `Do Not Import` → no material/texture requests from models
(standalone texture files still import). Conflicts: `exists` set when `destFile`
exists; KeepBoth → `_1`, `_2`, … first free name; Skip → request kept with
`policy=Skip` (factory does nothing, summary says skipped).

- [ ] **Step 1: Failing tests** (`[import][pipeline]`): plan for `robot.gltf` → 2 meshes,
  2 materials, 2 textures with expected names/paths; combine → 1 mesh `SM_robot`;
  disable Arm → its material (Metal, not used elsewhere) gone, Paint kept; disable
  T_Paint_N → Paint still requested, texture gone; Do Not Import materials → only
  meshes; KeepBoth with an existing `SM_Body.lmesh` → `SM_Body_1.lmesh`; Skip → policy
  Skip; Sanitize cases (`"Robot Arm (L).001"` → `"Robot_Arm_L_001"`, `""` → `"Asset"`);
  Set out-of-range scale clamps; stack JSON round-trip.
- [ ] **Steps 2-4:** fail → implement → pass; suite green.
- [ ] **Step 5:** Commit `feat(import): pipelines and import plan (T-0003)`.

## Task 8 — Factories

**Files:** `.../Import/Factories/{MeshFactory,MaterialFactory,TextureFactory,SoundFactory,FactoryCommon}.{h,cpp}`;
tests `Tests/Asset/Import/FactoryTests.cpp`.

```cpp
namespace Luma::Import {
struct FactoryContext {
    const ImportScene& scene;
    const PipelineStack& settings;
    Material::MaterialLibrary* materials;      // public API only
    const AssetPathResolver& paths;            // file <-> /Game/ virtual paths
    std::function<bool()> cancelled;
};
struct FactoryResult { bool ok = false; bool skipped = false; std::filesystem::path file; std::string error; };
FactoryResult CreateAsset(const AssetRequest&, const FactoryContext&);   // dispatches by type
}
```
Rules:
- **Atomic writes:** write to `<dest>.tmp`, then `std::filesystem::rename` over the
  destination; on cancel/error delete the temp. `.meta` written last, same way.
- **Overwrite keeps the GUID:** if `<dest>.meta` exists, reuse its `guid`.
- Mesh: `ModelTranslator::BuildMeshes` → `WriteMesh()`; section `materialName` = the
  created material's **virtual path** (e.g. `/Game/Robot/M_Paint`) so the editor can
  assign it.
- Material: start from `Material::Material::MakeDefault()`, set `name`, base colour
  constant / texture (virtual path of the created texture), metallic, roughness,
  specular, normal (texture), emissive, AO, opacity mask (blend mode Masked when
  `opacityMaskClip > 0`), two-sided; metallic-roughness texture → Metallic input
  channel B and Roughness input channel G with the same texture. Write through
  `MaterialLibrary::SetMaterial(virtualPath, m)` + `Save(virtualPath)`. Read
  `Engine/Material/include/Luma/Material/{Material,MaterialTypes}.h` for field names.
- Texture: external → copy; embedded → write bytes; PSD → decode + write PNG
  (`stbi_write_png`); `maxSize` → downscale (box filter, keep aspect) and write PNG;
  colour-space Auto → keep `IsTextureSRGB` behaviour, forced → recorded in `.meta`
  (`settings.colorSpace`) for the renderer (the lead wires renderer support).
- Sound: copy file; `.meta` settings carry loop/volume/pitch + probed info.

- [ ] **Step 1: Failing tests** (`[import][factory]`, temp project dir with
  `Assets/`): import `robot.glb` plan → files `SM_Body.lmesh`, `SM_Arm.lmesh`,
  `M_Paint.lmat`, `M_Metal.lmat`, `T_Paint_D.png`, `T_Paint_N.png` + `.meta` each;
  `ReadMesh(SM_Body)` 24 vertices, section material `/Game/.../M_Paint`; M_Paint's
  BaseColor texture == the T_Paint_D virtual path, Normal texture set, roughness 0.6;
  overwrite keeps the GUID; Skip leaves the old file byte-identical; cancel flag set
  before the first write → no files and no `.tmp` left; PSD import writes a PNG; maxSize
  256 on a 64 px texture leaves it unchanged, on a generated 1024 px texture yields 256.
- [ ] **Steps 2-4:** fail → implement → pass; suite green.
- [ ] **Step 5:** Commit `feat(import): asset factories (T-0003)`.

## Task 9 — Import session (async, progress, cancel, reimport)

**Files:** `.../Import/ImportSession.{h,cpp}`, `.../Import/TranslatorRegistry.{h,cpp}`;
tests `Tests/Asset/Import/ImportSessionTests.cpp`.

```cpp
namespace Luma::Import {
class TranslatorRegistry {               // all translators; extension → translator
public:
    static TranslatorRegistry CreateDefault();          // model, image, audio
    ITranslator* Find(const std::filesystem::path&) const;
    std::vector<std::string> SupportedExtensions() const;  // for the file picker filter
};
struct ImportSummary { u32 created = 0, overwritten = 0, skipped = 0, failed = 0;
                       std::vector<std::string> messages; std::vector<std::filesystem::path> files; };
class ImportSession {
public:
    // Phase 1 (sync, fast): translate files into a scene for the dialog.
    bool Translate(const std::vector<std::filesystem::path>& files, std::string* err);
    ImportScene& Scene(); PipelineStack& Settings();
    ImportPlan Plan(const std::filesystem::path& destFolder) const;
    // Phase 2 (async): run factories on a worker thread.
    void Start(const ImportPlan&, const FactoryContext& base);
    f32 Progress() const;                  // 0..1
    bool Running() const; void Cancel();
    std::optional<ImportSummary> TakeResult();   // once, after completion (poll on main thread)
    // Reimport: rebuilds scene + settings from an asset's .meta.
    static std::unique_ptr<ImportSession> ForReimport(const std::filesystem::path& assetFile, std::string* err);
};
bool SourceChanged(const std::filesystem::path& assetFile);  // hash/mtime vs .meta
}
```
Note: `MaterialLibrary` is not thread-safe — material requests are executed on the main
thread when `TakeResult()` completes the session (factories for meshes/textures/sounds
run on the worker; materials are queued and created in `TakeResult` before returning).
Document this in the header.

- [ ] **Step 1: Failing tests** (`[import][session]`): translate 3 files (glb + wav +
  png) → scene has 3 File nodes; Start + wait (poll with timeout 10 s) → summary counts;
  Progress reaches 1; Cancel during a run of 50 copies of the PNG → stops early, no
  `.tmp` files, summary reports cancellation; `ForReimport(SM_Body.lmesh)` restores the
  stored settings (e.g. uniformScale 2 set before the first import); `SourceChanged`
  false right after import, true after appending a byte to the source copy.
- [ ] **Steps 2-4:** fail → implement → pass; suite green.
- [ ] **Step 5:** Commit `feat(import): import session (T-0003)`.

## Task 10 — Registry and asset-type updates

**Files:** `Engine/Asset/include/Luma/Asset/AssetType.h`, `src/AssetScanner.cpp`,
`src/AssetType.cpp`; tests in `Tests/Asset/AssetScannerTests.cpp`.

- [ ] **Step 1: Failing tests:** `AssetTypeFromExtension` maps `psd`, `hdr`, `exr` →
  Texture; the scanner skips `*.meta` and `*.tmp` files; source-only model formats
  (`gltf`, `glb`, `fbx`, `obj`, `bin`, `mtl`) inside a project's `Assets/` are reported
  as `AssetType::Unknown` (they are import sources, not engine assets — only `.lmesh`
  is a Mesh). Update any existing test that asserted the old mapping and note it in
  Deviations.
- [ ] **Steps 2-4:** fail → implement → pass; suite green.
- [ ] **Step 5:** Commit `feat(asset): asset types and scanner rules for imports (T-0003)`.

## Task 11 — Thumbnails (sounds, new image formats)

**Files:** `Engine/Asset/include/Luma/Asset/SoundThumbnailRenderer.h`,
`src/SoundThumbnailRenderer.cpp` (links `Luma::Audio`), `src/TextureThumbnailRenderer.cpp`;
tests `Tests/Asset/ThumbnailImportTests.cpp`.

- Sound thumbnail (our design): 128×128 RGBA, background = theme `surface2`-like
  (`#262626`), centre line `#3A3A3A`, waveform filled between per-column min/max
  (`ComputePeaks(file, 128)`), colour `#2D9CDB` (Luma blue accent for audio), 4 px
  margin, duration text not drawn (the tile shows it). Register with
  `ThumbnailManager::RegisterRenderer(AssetType::Sound, ...)` wherever the texture
  renderer is registered (search `RegisterRenderer`).
- Textures: decode through `DecodeImageFile` so PSD/HDR/EXR work; float images
  tone-mapped `c / (1 + c)` then sRGB-encoded.
- [ ] **Step 1: Failing tests** (`[import][thumbnail]`): sound thumbnail of
  `tone_mono.wav` → file written, 128×128, centre column has accent-coloured pixels
  above and below the centre line, identical bytes on a second render (deterministic);
  HDR/EXR/PSD thumbnails written with expected size; HDR pixel ≈ tone-mapped value
  (2.0/(3.0)) sRGB-encoded ±3.
- [ ] **Steps 2-4:** fail → implement → pass; suite green.
- [ ] **Step 5:** Commit `feat(asset): sound waveform and HDR/EXR/PSD thumbnails (T-0003)`.

## Task 12 — Import window (editor panel)

**Files:** `Editor/panels/ImportDialog/{include/Luma/Editor/Panels/ImportDialog.h,src/ImportDialog.cpp,CMakeLists.txt}`
(module `Luma::PanelImportDialog`); tests `Tests/Editor/ImportDialogTests.cpp`
(Slate harness: `Tests/Slate/SlateTestHarness.h`).

```cpp
namespace Luma::Editor::Panels {
class ImportDialog {
public:
    // Opens for a translated session (owned by the caller until Close).
    void Open(Import::ImportSession* session, std::filesystem::path destFolder,
              std::string destVirtualPath, bool reimport);
    bool IsOpen() const;
    enum class Action { None, Import, ImportAll, Cancel };
    // Draws the modal window; returns the user's choice on the frame it happens.
    Action Draw(Slate::Context& ui, f32 screenW, f32 screenH);
    void LoadPresets(const std::filesystem::path& projectConfigDir);   // Config/ImportPresets.json
    void SavePreset(const std::string& name);
};
}
```
Layout (use only existing Slate widgets: `BeginModal`/`EndModal`, `TreeNode`,
tri-state `Checkbox` with `CheckState`, `VerticalScroll`, `Combo`, `SpinBox`,
`VectorField`, `EditableText`, `Button`, `CollapsingHeader`, `Tooltip`,
`PropertyRow`):
- Modal 960×620 (clamped to 90% of the screen), title "Import Content" (or "Reimport
  <name>").
- Header row: "Destination" + the virtual path (read-only text), "Preset" `Combo`
  (Default + saved presets + "Save Preset…" which opens a name field).
- Left 38%: scrollable tree. Per File node: tri-state checkbox (Checked / Unchecked /
  Undetermined when children differ), file icon, name, child groups "Meshes (n)",
  "Materials (n)", "Textures (n)", "Sounds (n)" each with a tri-state checkbox, then
  items. Toggling a parent sets all children; toggling children updates the parent's
  state. Items with `error` are drawn in the theme `error` colour, unchecked, their
  checkbox disabled, tooltip = error. Selecting an item highlights it and scrolls the
  settings to its category.
- Right: settings generated from `PipelineStack::Blocks()` — one `CollapsingHeader`
  per category, one `PropertyRow` per visible field: Bool→Checkbox, Float→SpinBox
  (min/max/step from the schema), Int→SpinBoxInt, Enum→Combo, Vec3→VectorField,
  String→EditableText; tooltips from the schema; hide categories with no matching
  content (no meshes → hide Meshes/Materials; no sounds → hide Audio).
- Conflict strip (only when `Plan()` has `exists` requests): "N assets already exist"
  + `Combo` bound to `Common.conflict`.
- Footer: "Reset to Defaults" (left), right-aligned "Import", "Import All" (only when
  the session has more than one File node of the same type — applies settings to all,
  closes), "Cancel". Enter = Import, Escape = Cancel.
- Remember last-used settings per project in `Config/ImportPresets.json` under the key
  `"__last"`; presets file format: `{ "presets": { "<name>": <PipelineStack JSON> }, "__last": {...} }`.

- [ ] **Step 1: Failing tests** (`[import][dialog]`): open with a `robot.gltf` session;
  clicking the "Meshes (2)" checkbox unchecks Body and Arm and their scene nodes'
  `enabled` flags; un-checking only Arm makes the File checkbox Undetermined; changing
  the uniform-scale SpinBox (drag) changes `Settings().Meshes().GetFloat("uniformScale")`;
  a sound-only session hides the Meshes category; Escape → `Action::Cancel`; clicking
  Import → `Action::Import`; preset save → `Config/ImportPresets.json` contains it and
  reloads.
- [ ] **Steps 2-4:** fail → implement → pass; suite green.
- [ ] **Step 5:** Commit `feat(editor): Import window (T-0003)`.

## Task 13 — Editor wiring, cleanup, manual verification

**Files:** `Editor/LumaEditor/EditorScreen.{h,cpp}` (import entry points only),
`Editor/LumaEditor/main.cpp` (AudioPreview init/shutdown), `Editor/panels/ContentBrowser/**`,
`Editor/panels/PanelContext.h` (new callbacks only), `Engine/Asset/**` (deletions).

- [ ] **Step 1:** `ImportAssetsInto(folder)`: file picker filter built from
  `TranslatorRegistry::SupportedExtensions()` ("All Supported" + one entry per family),
  then `ImportSession::Translate` → `ImportDialog::Open`. `HandleDroppedFiles`: same
  flow (folders dropped → their supported files, recursively). Unsupported files →
  `LUMA_USER_WARN("Import", "'x.abc' is not a supported import format")`.
- [ ] **Step 2:** Dialog `Import` → `ImportSession::Start` with a `FactoryContext` built from
  the editor's `MaterialLibrary` and `AssetPathResolver`; a progress bar
  (`Slate::Context::ProgressBar`) with a Cancel button in the Content Browser toolbar while
  running; on `TakeResult`: `m_assetRegistry.Scan()`, invalidate thumbnails of created
  files (`ThumbnailManager::DirtyThumbnail` + `ContentBrowserPanel::InvalidateThumbnail`),
  `LUMA_USER_INFO("Import", "Imported N assets into /Game/... (K skipped, F failed)")` and
  one `LUMA_USER_ERROR` per failure message.
- [ ] **Step 3:** Content Browser context menu on an imported asset (has `.meta`):
  "Reimport" (→ `ForReimport` → dialog in reimport mode, conflict policy Overwrite) and,
  when `SourceChanged`, a small badge on the tile + "Source changed — Reimport" entry.
- [ ] **Step 4:** Sound tiles: hover shows a play/stop button (bottom-left of the tile) →
  `AudioPreview::Play/Stop`; the tile caption shows the duration (`m:ss`). Double-click
  a sound = play. `AudioPreview` is created in `main.cpp` after the window and shut down
  before exit; pass it to panels through `PanelContext` (new member
  `Audio::AudioPreview* audioPreview`).
- [ ] **Step 5:** Delete the replaced code once nothing references it:
  `AssetImportManager`, `ImportDialog` (Engine/Asset), `BatchImportManager`, `Factory`,
  `ImportValidator`, `MeshImporter`, `TextureImporter` (and their headers/tests), after
  moving any still-useful test coverage to the new tests. Keep `LumaMesh`,
  `AssetMetadata` only if still used (otherwise delete). Build + full suite.
- [ ] **Step 6: Manual verification** in a scratch copy of `Tests/Fixtures/MaterialShowcase`
  (never in `TestProject`):
  1. Import button → pick `robot.glb` → window shows Robot with Meshes (2) / Materials (2)
     / Textures (2) → Import → assets appear with a 3D thumbnail (mesh), images, material
     thumbnails.
  2. Drag `robot.fbx` + `robot.obj` + `tone_stereo.mp3` + all `img.*` from Explorer onto
     the Content Browser → one window, Import All → everything appears; the FBX robot
     is the same size as the glTF one when both are dropped into the viewport.
  3. Assign `SM_Body` to an actor (Inspector) → renders with M_Paint.
  4. Hover the sound tile → play button plays; waveform thumbnail visible.
  5. Reimport `SM_Body` with scale 2 → mesh doubles, scene reference intact.
  6. `Saved/Logs/Editor.log`: 0 `VUID` / `validation error`.
  Write what you saw in *Result*.
- [ ] **Step 7:** Commit `feat(editor): import window wiring, reimport, sound preview (T-0003)`.

## Task 14 — Docs

- [ ] Update `GOAL.md` (asset import section: formats, window, reimport, thumbnails,
  audio preview; open items: skeletal/animation, LODs, collision, BCn, HDR/EXR as
  material textures — lead). Commit `docs: asset import (T-0003)`.

## Acceptance criteria

- [ ] Every test named above exists, was seen failing first, and passes.
- [ ] Whole suite green; manual verification (Task 13 Step 6) written up in *Result*.
- [ ] No stubs / unused settings; deleted legacy code has no remaining references.

## Out of scope

Skeletal meshes, animation, morph targets, LODs, collision, import-into-level, BCn
compression, DDS/KTX, the game-facing audio engine, renderer support for forced
colour space / HDR-EXR material textures (lead).

## Questions

## Result

### Deviations (Task 1)

- Root `project()` also enables C. Assimp and miniaudio are C, and Ninja never received `CMAKE_C_COMPILE_OBJECT` when C was enabled only from a helper function.
- `ThirdParty/miniz.h` includes `miniz_common.h`, `miniz_tdef.h`, `miniz_tinfl.h`, and `miniz_zip.h`, none of which are in the tree, and there is no `miniz.c` (no new downloads). `tinyexr_impl.cpp` sets `TINYEXR_USE_MINIZ` to 0 and uses stb zlib (`TINYEXR_USE_STB_ZLIB`) from `ThirdParty/stb`. stb is included after `tinyexr.h` so `stbi_zlib_compress` stays `extern "C"`.
- `[asset]` after the Assimp 6 switch: `All tests passed (134 assertions in 37 test cases)`. No existing test needed a code change.

### Task 2

`ImportFixtureGen` loaded `robot.gltf` and wrote `Tests/Fixtures/Import/robot.fbx`. Reloading that FBX with Assimp reported meshes=2, materials Paint and Metal, nodes RootNode / Body / Arm. The exporter did write the global settings: UpAxis=2, UpAxisSign=1, FrontAxis=1, FrontAxisSign=-1, CoordAxis=0, CoordAxisSign=1, UnitScaleFactor=1 (float, centimetres).

### Task 3

`Json` in the plan is `Luma::SerialValue`. `[import][scene]` passed (41 assertions in 4 cases). Whole suite: `All tests passed (22799 assertions in 419 test cases)`. The first test build failed to link (headers only, no sources), then the implementation linked and passed.

## Review
