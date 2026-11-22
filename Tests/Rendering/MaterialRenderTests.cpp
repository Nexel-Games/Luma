// Compiled materials through the real deferred renderer (offscreen 64x64):
// G-buffer output, World Position Offset, masked clipping and parameter-only
// updates, with the validation layer on.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "VulkanTestFixture.h"
#include "Luma/Editor/MaterialGpuCompile.h"
#include "Luma/Material/GraphAssets.h"
#include "Luma/Rendering/Vulkan/VulkanDeferredRenderer.h"
#include "Luma/Renderer/DeferredSceneView.h"
#include "Luma/ShaderCompiler/ShaderCompiler.h"
#include "Vulkan/UI/VulkanUIPass.h"

namespace fs = std::filesystem;
using namespace Luma;
using Luma::Material::GraphMaterial;
using Luma::MaterialGraph::NodeId;
using Luma::MaterialGraph::PinRef;
using Luma::MaterialGraph::PropertyValue;

namespace {

constexpr u32 kSize = 64;
constexpr u32 kBaseColor = 0;
constexpr u32 kOpacityMask = 7;
constexpr u32 kWorldPositionOffset = 10;

NodeId Node(GraphMaterial& m, const char* type) { return m.graph.AddNode(type, {0.0f, 0.0f}); }
void Link(GraphMaterial& m, NodeId from, u32 toPin) {
    std::string err;
    REQUIRE(m.graph.Connect(PinRef{from, 0}, PinRef{m.graph.OutputNode(), toPin}, &err));
}

GraphMaterial TintMaterial(Math::Vec4 tint) {
    GraphMaterial m;
    const NodeId p = Node(m, "VectorParameter");
    m.graph.Find(p)->properties["name"] = PropertyValue(std::string("Tint"));
    m.graph.Find(p)->properties["default"] = PropertyValue(tint);
    Link(m, p, kBaseColor);
    return m;
}

f32 ToSrgb(f32 linear) {
    return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

struct RenderRig {
    VulkanTestFixture f;
    VkCommandPool pool = VK_NULL_HANDLE;
    std::unique_ptr<VulkanUIPass> ui;
    std::unique_ptr<Rendering::VulkanDeferredRenderer> renderer;
    ShaderCompiler::Compiler shaders{LUMA_SHADER_SOURCE_DIR, fs::temp_directory_path() / "luma_material_render_cache", 2};
    SceneView view;
    std::vector<SceneInstance> instances;

    RenderRig() {
        VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        info.queueFamilyIndex = f.QueueFamily();
        vkCreateCommandPool(f.Logical(), &info, nullptr, &pool);
        ui = std::make_unique<VulkanUIPass>(f.Physical(), f.Logical(), pool, f.Queue(), VK_FORMAT_R8G8B8A8_UNORM, 2,
                                            LUMA_SHADER_DIR);
        renderer = std::make_unique<Rendering::VulkanDeferredRenderer>(f.Physical(), f.Logical(), f.Queue(),
                                                                       f.QueueFamily(), *ui, LUMA_SHADER_DIR);
        REQUIRE(renderer->Initialize(kSize, kSize));
        view.viewMode = ViewMode::BaseColor;
        view.sky.enabled = false;
    }
    ~RenderRig() {
        renderer.reset();
        ui.reset();
        if (pool) vkDestroyCommandPool(f.Logical(), pool, nullptr);
    }

    Editor::GpuMaterialBuild Build(const GraphMaterial& m, std::vector<Material::ParameterOverride> overrides = {}) {
        Editor::GpuMaterialBuild b = Editor::BuildGpuMaterial(m, nullptr, overrides, {}, {}, shaders, "Test");
        INFO(b.errors);
        REQUIRE(b.ok);
        return b;
    }

    // A unit cube in front of the camera drawn with `material`; returns the
    // centre pixel (RGBA8).
    std::array<u8, 4> RenderCentre(MaterialHandle material) {
        SceneInstance inst;
        inst.primitive = MeshPrimitive::Cube;
        inst.sectionCount = 1;
        inst.sectionMaterials[0] = material;
        return RenderScene({inst});
    }

    // Renders `scene` from (0, 0, 4) towards the origin; returns pixel (x, y)
    // (default: the centre).
    std::array<u8, 4> RenderScene(std::vector<SceneInstance> scene, u32 x = kSize / 2, u32 y = kSize / 2) {
        instances = std::move(scene);
        view.instances = instances.data();
        view.instanceCount = static_cast<u32>(instances.size());

        Renderer2::DeferredSceneView frame;
        frame.cameraPosition = {0.0f, 0.0f, 4.0f};
        frame.cameraDirection = {0.0f, 0.0f, -1.0f};
        frame.viewMatrix = Math::LookAt({0.0f, 0.0f, 4.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
        frame.projectionMatrix = Math::PerspectiveForViewport(0.9f, kSize, kSize, 0.1f, 100.0f);
        frame.viewProjectionMatrix = frame.projectionMatrix * frame.viewMatrix;
        frame.width = kSize;
        frame.height = kSize;
        frame.sceneData = &view;
        frame.lightingParams = &view.lighting;
        renderer->RenderScene(frame);
        std::vector<u8> rgba;
        u32 w = 0, h = 0;
        REQUIRE(renderer->ReadFinalImage(rgba, w, h));
        const usize i = (static_cast<usize>(y) * w + x) * 4;
        return {rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]};
    }
};

bool Near(u8 actual, f32 expected) { return std::abs(static_cast<f32>(actual) - expected) <= 4.0f; }

}  // namespace

TEST_CASE("A compiled material's base colour reaches the G-buffer", "[vulkan][render]") {
    RenderRig r;
    const Editor::GpuMaterialBuild b = r.Build(TintMaterial({0.25f, 0.5f, 0.75f, 1.0f}));
    const MaterialHandle h = r.renderer->UpdateMaterial(42, b.gpu, b.block);
    const auto px = r.RenderCentre(h);
    CHECK(Near(px[0], ToSrgb(0.25f) * 255.0f));
    CHECK(Near(px[1], ToSrgb(0.5f) * 255.0f));
    CHECK(Near(px[2], ToSrgb(0.75f) * 255.0f));
    CHECK(r.f.ValidationErrors().empty());
}

TEST_CASE("Changing only parameters re-colours without new pipelines", "[vulkan][render]") {
    RenderRig r;
    const GraphMaterial m = TintMaterial({0.25f, 0.5f, 0.75f, 1.0f});
    const Editor::GpuMaterialBuild b = r.Build(m);
    const MaterialHandle h = r.renderer->UpdateMaterial(42, b.gpu, b.block);
    r.RenderCentre(h);
    const usize pipelines = r.renderer->MaterialPipelineCount();

    Material::ParameterOverride o;
    o.name = "Tint";
    o.kind = Material::ParameterOverride::Kind::Vector;
    o.vector = {1.0f, 0.0f, 0.0f, 1.0f};
    const Editor::GpuMaterialBuild red = r.Build(m, {o});
    CHECK(red.gpu.shaderKey == b.gpu.shaderKey);  // same shaders: an instance edit
    r.renderer->UpdateMaterialParameters(h, red.block);
    const auto px = r.RenderCentre(h);
    CHECK(px[0] > 250);
    CHECK(px[1] < 5);
    CHECK(r.renderer->MaterialPipelineCount() == pipelines);
    CHECK(r.f.ValidationErrors().empty());
}

TEST_CASE("World Position Offset moves the surface", "[vulkan][render]") {
    RenderRig r;
    GraphMaterial m = TintMaterial({1.0f, 1.0f, 1.0f, 1.0f});
    const NodeId offset = Node(m, "Constant3Vector");
    m.graph.Find(offset)->properties["value"] = PropertyValue(Math::Vec4{0.0f, 50.0f, 0.0f, 0.0f});
    Link(m, offset, kWorldPositionOffset);
    const Editor::GpuMaterialBuild b = r.Build(m);
    CHECK(b.gpu.usesWPO);
    const auto px = r.RenderCentre(r.renderer->UpdateMaterial(43, b.gpu, b.block));
    CHECK(px[0] < 5);  // the cube moved out of the centre: background
    CHECK(r.f.ValidationErrors().empty());
}

TEST_CASE("A masked material with Opacity Mask 0 writes nothing", "[vulkan][render]") {
    RenderRig r;
    GraphMaterial m = TintMaterial({1.0f, 1.0f, 1.0f, 1.0f});
    m.settings.blendMode = Material::BlendMode::Masked;
    const NodeId zero = Node(m, "Constant");
    m.graph.Find(zero)->properties["value"] = PropertyValue(0.0);
    Link(m, zero, kOpacityMask);
    const Editor::GpuMaterialBuild b = r.Build(m);
    CHECK(b.gpu.HasPass(GpuPass::DepthOnly));
    const auto px = r.RenderCentre(r.renderer->UpdateMaterial(44, b.gpu, b.block));
    CHECK(px[0] < 5);
    CHECK(r.f.ValidationErrors().empty());
}

TEST_CASE("Materials without shaders yet draw nothing", "[vulkan][render]") {
    RenderRig r;
    // The Default Material handle has no shader set until one is uploaded.
    const auto px = r.RenderCentre(kDefaultMaterialHandle);
    CHECK(px[0] < 5);
    CHECK(r.f.ValidationErrors().empty());
}

namespace {

// A lit material: base colour (0.5), roughness, a shading model and its data.
GraphMaterial LitMaterial(Material::ShadingModel model, f32 roughness) {
    GraphMaterial m = TintMaterial({0.5f, 0.5f, 0.5f, 1.0f});
    m.settings.shadingModel = model;
    const NodeId rough = Node(m, "Constant");
    m.graph.Find(rough)->properties["value"] = PropertyValue(static_cast<f64>(roughness));
    Link(m, rough, 3);  // Roughness
    return m;
}

void LinkConstant(GraphMaterial& m, u32 pin, f64 value) {
    const NodeId n = Node(m, "Constant");
    m.graph.Find(n)->properties["value"] = PropertyValue(value);
    Link(m, n, pin);
}

// Direct sunlight only (no sky / IBL / shadows), from `toSun`.
void SunOnly(RenderRig& r, Math::Vec3 toSun) {
    r.view.viewMode = ViewMode::Lit;
    r.view.lighting.sunDirection = toSun;
    r.view.lighting.iblIntensity = 0.0f;
    r.view.lighting.sunShadows = false;
    r.view.sky.enabled = false;
    // Fixed exposure, no bloom: pixels compare across renders.
    r.view.postProcess.autoExposure = false;
    r.view.postProcess.bloomEnabled = false;
}

u32 Brightness(const std::array<u8, 4>& px) { return px[0] + px[1] + px[2]; }

constexpr u32 kSubsurfaceColor = 11;
constexpr u32 kCustomData0 = 12;
constexpr u32 kCustomData1 = 13;
constexpr u32 kOpacity = 6;

}  // namespace

TEST_CASE("Subsurface transmits light from behind", "[vulkan][render][shading]") {
    RenderRig r;
    SunOnly(r, {0.0f, 0.0f, -1.0f});  // behind the cube, facing away from the camera
    const auto lit = r.Build(LitMaterial(Material::ShadingModel::DefaultLit, 0.8f));
    GraphMaterial ss = LitMaterial(Material::ShadingModel::Subsurface, 0.8f);
    LinkConstant(ss, kSubsurfaceColor, 1.0);
    LinkConstant(ss, kOpacity, 0.2);
    const auto sub = r.Build(ss);
    const u32 dark = Brightness(r.RenderCentre(r.renderer->UpdateMaterial(50, lit.gpu, lit.block)));
    const u32 glow = Brightness(r.RenderCentre(r.renderer->UpdateMaterial(51, sub.gpu, sub.block)));
    CHECK(glow > dark + 30);
    CHECK(r.f.ValidationErrors().empty());
}

TEST_CASE("Clear coat adds a second highlight", "[vulkan][render][shading]") {
    RenderRig r;
    SunOnly(r, {0.0f, 0.0f, 1.0f});  // behind the camera: the highlight is centred
    const auto base = r.Build(LitMaterial(Material::ShadingModel::DefaultLit, 0.9f));
    GraphMaterial cc = LitMaterial(Material::ShadingModel::ClearCoat, 0.9f);
    LinkConstant(cc, kCustomData0, 1.0);
    LinkConstant(cc, kCustomData1, 0.3);
    const auto coat = r.Build(cc);
    const u32 matte = Brightness(r.RenderCentre(r.renderer->UpdateMaterial(52, base.gpu, base.block)));
    const u32 glossy = Brightness(r.RenderCentre(r.renderer->UpdateMaterial(53, coat.gpu, coat.block)));
    CHECK(glossy > matte + 30);
    CHECK(r.f.ValidationErrors().empty());
}

TEST_CASE("Two-sided foliage shows its transmission colour from behind", "[vulkan][render][shading]") {
    RenderRig r;
    SunOnly(r, {0.0f, 0.0f, -1.0f});
    const auto lit = r.Build(LitMaterial(Material::ShadingModel::DefaultLit, 0.8f));
    GraphMaterial leaf = LitMaterial(Material::ShadingModel::TwoSidedFoliage, 0.8f);
    const NodeId green = Node(leaf, "Constant3Vector");
    leaf.graph.Find(green)->properties["value"] = PropertyValue(Math::Vec4{0.1f, 1.0f, 0.1f, 0.0f});
    Link(leaf, green, kSubsurfaceColor);
    const auto foliage = r.Build(leaf);
    const auto dark = r.RenderCentre(r.renderer->UpdateMaterial(54, lit.gpu, lit.block));
    const auto px = r.RenderCentre(r.renderer->UpdateMaterial(55, foliage.gpu, foliage.block));
    CHECK(px[1] > dark[1] + 20);
    CHECK(px[1] > px[0]);  // tinted by the transmission colour
    CHECK(r.f.ValidationErrors().empty());
}

namespace {

// An unlit surface material: Emissive = colour; translucent ones get Opacity.
GraphMaterial Emissive(Math::Vec4 color, Material::BlendMode blend, f64 opacity = 1.0) {
    GraphMaterial m;
    m.settings.shadingModel = Material::ShadingModel::Unlit;
    m.settings.blendMode = blend;
    const NodeId c = Node(m, "Constant3Vector");
    m.graph.Find(c)->properties["value"] = PropertyValue(color);
    Link(m, c, 5);  // Emissive Color
    if (blend != Material::BlendMode::Opaque) LinkConstant(m, kOpacity, opacity);
    return m;
}

SceneInstance Cube(MaterialHandle material, f32 z, f32 scale = 1.0f) {
    SceneInstance inst;
    inst.primitive = MeshPrimitive::Cube;
    inst.sectionCount = 1;
    inst.sectionMaterials[0] = material;
    inst.model = Math::Translate({0.0f, 0.0f, z}) * Math::Scale({scale, scale, 0.1f});
    return inst;
}

// Display values equal linear colour (sRGB-encoded): no exposure, bloom or
// tone curve.
void LinearDisplay(RenderRig& r) {
    r.view.viewMode = ViewMode::Lit;
    r.view.sky.enabled = false;
    r.view.lighting.iblIntensity = 0.0f;
    r.view.postProcess.autoExposure = false;
    r.view.postProcess.bloomEnabled = false;
    r.view.postProcess.toneMapper = PostProcessParams::ToneMapper::Linear;
}

MaterialHandle Upload(RenderRig& r, u64 key, const GraphMaterial& m) {
    const auto b = r.Build(m);
    return r.renderer->UpdateMaterial(key, b.gpu, b.block);
}

bool NearLinear(u8 actual, f32 linear) { return Near(actual, ToSrgb(std::clamp(linear, 0.0f, 1.0f)) * 255.0f); }

}  // namespace

TEST_CASE("Translucency blends over the opaque scene", "[vulkan][render][translucency]") {
    RenderRig r;
    LinearDisplay(r);
    const MaterialHandle white = Upload(r, 60, Emissive({1, 1, 1, 0}, Material::BlendMode::Opaque));
    const MaterialHandle red = Upload(r, 61, Emissive({1, 0, 0, 0}, Material::BlendMode::Translucent, 0.5));
    const auto px = r.RenderScene({Cube(white, 0.0f, 2.0f), Cube(red, 1.0f)});
    CHECK(NearLinear(px[0], 1.0f));
    CHECK(NearLinear(px[1], 0.5f));
    CHECK(NearLinear(px[2], 0.5f));
    CHECK(r.f.ValidationErrors().empty());
}

TEST_CASE("Translucent surfaces draw back to front", "[vulkan][render][translucency]") {
    RenderRig r;
    LinearDisplay(r);
    const MaterialHandle white = Upload(r, 60, Emissive({1, 1, 1, 0}, Material::BlendMode::Opaque));
    const MaterialHandle red = Upload(r, 61, Emissive({1, 0, 0, 0}, Material::BlendMode::Translucent, 0.5));
    const MaterialHandle green = Upload(r, 62, Emissive({0, 1, 0, 0}, Material::BlendMode::Translucent, 0.5));
    // Listed near-first both times: only sorting can get them right.
    const auto greenFront = r.RenderScene({Cube(white, 0.0f, 2.0f), Cube(green, 2.0f), Cube(red, 1.0f)});
    CHECK(greenFront[1] > greenFront[0]);
    const auto redFront = r.RenderScene({Cube(white, 0.0f, 2.0f), Cube(red, 2.0f), Cube(green, 1.0f)});
    CHECK(redFront[0] > redFront[1]);
    CHECK(r.f.ValidationErrors().empty());
}

TEST_CASE("Additive adds and Modulate multiplies", "[vulkan][render][translucency]") {
    RenderRig r;
    LinearDisplay(r);
    const MaterialHandle gray = Upload(r, 60, Emissive({0.25f, 0.25f, 0.25f, 0}, Material::BlendMode::Opaque));
    const MaterialHandle add = Upload(r, 63, Emissive({0.5f, 0, 0, 0}, Material::BlendMode::Additive, 1.0));
    const auto added = r.RenderScene({Cube(gray, 0.0f, 2.0f), Cube(add, 1.0f)});
    CHECK(NearLinear(added[0], 0.75f));
    CHECK(NearLinear(added[1], 0.25f));
    const MaterialHandle white = Upload(r, 64, Emissive({1, 1, 1, 0}, Material::BlendMode::Opaque));
    const MaterialHandle mod = Upload(r, 65, Emissive({0.5f, 1, 1, 0}, Material::BlendMode::Modulate, 1.0));
    const auto modulated = r.RenderScene({Cube(white, 0.0f, 2.0f), Cube(mod, 1.0f)});
    CHECK(NearLinear(modulated[0], 0.5f));
    CHECK(NearLinear(modulated[1], 1.0f));
    CHECK(r.f.ValidationErrors().empty());
}

TEST_CASE("Refraction shifts what is seen through the surface", "[vulkan][render][translucency]") {
    RenderRig r;
    LinearDisplay(r);
    const MaterialHandle white = Upload(r, 60, Emissive({1, 1, 1, 0}, Material::BlendMode::Opaque));
    const MaterialHandle black = Upload(r, 66, Emissive({0, 0, 0, 0}, Material::BlendMode::Opaque));
    GraphMaterial glass = Emissive({0, 0, 0, 0}, Material::BlendMode::Translucent, 0.0);
    const MaterialHandle plain = Upload(r, 67, glass);
    glass.settings.refraction = Material::RefractionMethod::PixelOffset;
    LinkConstant(glass, 15, -30.0);  // Refraction: 30 pixels against the normal (inwards)
    const MaterialHandle bent = Upload(r, 68, glass);
    // White left half, black right half; a glass sphere over the seam.
    auto scene = [&](MaterialHandle g) {
        SceneInstance left = Cube(white, 0.0f, 2.0f);
        left.model = Math::Translate({-1.0f, 0.0f, 0.0f}) * Math::Scale({1.0f, 2.0f, 0.1f});
        SceneInstance right = Cube(black, 0.0f, 2.0f);
        right.model = Math::Translate({1.0f, 0.0f, 0.0f}) * Math::Scale({1.0f, 2.0f, 0.1f});
        SceneInstance sphere;
        sphere.primitive = MeshPrimitive::Sphere;
        sphere.sectionCount = 1;
        sphere.sectionMaterials[0] = g;
        sphere.model = Math::Translate({0.0f, 0.0f, 1.0f}) * Math::Scale({0.8f, 0.8f, 0.8f});
        return std::vector<SceneInstance>{left, right, sphere};
    };
    // Just right of the seam (black); refraction pulls the lookup across it (white).
    const u32 x = kSize / 2 + 6, y = kSize / 2;
    const auto straight = r.RenderScene(scene(plain), x, y);
    const auto refracted = r.RenderScene(scene(bent), x, y);
    CHECK(std::abs(static_cast<int>(straight[0]) - static_cast<int>(refracted[0])) > 40);
    CHECK(r.f.ValidationErrors().empty());
}

TEST_CASE("Translucent surfaces receive sun shadows", "[vulkan][render][translucency]") {
    RenderRig r;
    r.view.viewMode = ViewMode::Lit;
    r.view.sky.enabled = false;
    r.view.lighting.iblIntensity = 0.0f;
    r.view.lighting.sunDirection = Math::Normalize(Math::Vec3{0.0f, 0.7f, 0.7f});  // above and in front
    r.view.lighting.sunShadows = true;
    r.view.postProcess.autoExposure = false;
    r.view.postProcess.bloomEnabled = false;
    GraphMaterial litGlass = TintMaterial({0.8f, 0.8f, 0.8f, 1.0f});
    litGlass.settings.blendMode = Material::BlendMode::Translucent;
    LinkConstant(litGlass, kOpacity, 0.9);
    const MaterialHandle glass = Upload(r, 70, litGlass);
    const MaterialHandle wall = Upload(r, 71, TintMaterial({0.2f, 0.2f, 0.2f, 1.0f}));
    // A slab on the sun's path to the glass, outside the camera's view.
    SceneInstance blocker = Cube(wall, 0.0f);
    blocker.model = Math::Translate({0.0f, 2.1f, 2.1f}) * Math::Scale({1.5f, 0.05f, 1.5f});
    const auto lit = r.RenderScene({Cube(glass, 0.0f)});
    const auto shadowed = r.RenderScene({Cube(glass, 0.0f), blocker});
    CHECK(Brightness(shadowed) + 20 < Brightness(lit));
    CHECK(r.f.ValidationErrors().empty());
}

namespace {

// A post-process material: Emissive = f(Post Process Input 0).
GraphMaterial PostProcess(Material::PostProcessLocation where, i32 priority, const char* op) {
    GraphMaterial m;
    m.settings.domain = Material::MaterialDomain::PostProcess;
    m.settings.ppLocation = where;
    m.settings.ppPriority = priority;
    const NodeId input = Node(m, "SceneTexture");
    m.graph.Find(input)->properties["id"] = PropertyValue(i64{8});  // PostProcessInput0
    const NodeId mask = Node(m, "ComponentMask");
    m.graph.Find(mask)->properties["b"] = PropertyValue(true);
    std::string err;
    REQUIRE(m.graph.Connect(PinRef{input, 0}, PinRef{mask, 0}, &err));
    NodeId result = mask;
    if (std::string(op) == "invert") {
        result = Node(m, "OneMinus");
        REQUIRE(m.graph.Connect(PinRef{mask, 0}, PinRef{result, 0}, &err));
    } else {  // "half": x * 0.5
        result = Node(m, "Multiply");
        REQUIRE(m.graph.Connect(PinRef{mask, 0}, PinRef{result, 0}, &err));
        const NodeId half = Node(m, "Constant");
        m.graph.Find(half)->properties["value"] = PropertyValue(0.5);
        REQUIRE(m.graph.Connect(PinRef{half, 0}, PinRef{result, 1}, &err));
    }
    Link(m, result, 5);  // Emissive Color
    return m;
}

}  // namespace

TEST_CASE("A post-process material after tone mapping inverts the image", "[vulkan][render][postprocess]") {
    RenderRig r;
    LinearDisplay(r);
    const MaterialHandle white = Upload(r, 80, Emissive({1, 1, 1, 0}, Material::BlendMode::Opaque));
    const MaterialHandle invert = Upload(r, 81, PostProcess(Material::PostProcessLocation::AfterTonemapping, 0, "invert"));
    const auto plainCorner = r.RenderScene({Cube(white, 0.0f, 2.0f)}, 1, 1);  // the backdrop
    r.view.postProcess.materials = {{invert, 1.0f}};
    const auto centre = r.RenderScene({Cube(white, 0.0f, 2.0f)});  // white -> black
    const auto corner = r.RenderScene({Cube(white, 0.0f, 2.0f)}, 1, 1);
    CHECK(centre[0] < 5);
    // The display image is inverted where it is: 255 - backdrop.
    CHECK(std::abs(static_cast<int>(corner[0]) - (255 - static_cast<int>(plainCorner[0]))) <= 3);
    CHECK(r.f.ValidationErrors().empty());
}

TEST_CASE("Post-process materials run in priority order", "[vulkan][render][postprocess]") {
    RenderRig r;
    LinearDisplay(r);
    const MaterialHandle white = Upload(r, 80, Emissive({1, 1, 1, 0}, Material::BlendMode::Opaque));
    // invert then halve: (1 - 1) * 0.5 = 0;  halve then invert: 1 - 0.5 = 0.5.
    const MaterialHandle invertFirst = Upload(r, 82, PostProcess(Material::PostProcessLocation::AfterTonemapping, 0, "invert"));
    const MaterialHandle halveSecond = Upload(r, 83, PostProcess(Material::PostProcessLocation::AfterTonemapping, 1, "half"));
    r.view.postProcess.materials = {{halveSecond, 1.0f}, {invertFirst, 1.0f}};  // listed out of order
    const auto px = r.RenderScene({Cube(white, 0.0f, 2.0f)});
    CHECK(px[0] < 5);
    CHECK(r.f.ValidationErrors().empty());
}

TEST_CASE("A post-process material with weight 0 is skipped", "[vulkan][render][postprocess]") {
    RenderRig r;
    LinearDisplay(r);
    const MaterialHandle white = Upload(r, 80, Emissive({1, 1, 1, 0}, Material::BlendMode::Opaque));
    const MaterialHandle invert = Upload(r, 81, PostProcess(Material::PostProcessLocation::AfterTonemapping, 0, "invert"));
    r.view.postProcess.materials = {{invert, 0.0f}};
    const auto px = r.RenderScene({Cube(white, 0.0f, 2.0f)});
    CHECK(px[0] > 250);
    CHECK(r.f.ValidationErrors().empty());
}
