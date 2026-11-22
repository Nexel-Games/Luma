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
        instances = {inst};
        view.instances = instances.data();
        view.instanceCount = 1;

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
        const usize i = (static_cast<usize>(h / 2) * w + w / 2) * 4;
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
