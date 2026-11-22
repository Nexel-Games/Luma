// Visual smoke test for Luma::MaterialEditors: builds a small graph
// material + instance in a temp project, draws each editor for a few
// frames in a real window and captures a PNG of each.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Luma/Asset/AssetPath.h"
#include "Luma/Core/Log.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/MaterialEditors/MaterialGraphEditor.h"
#include "Luma/MaterialEditors/MaterialInstanceEditor.h"
#include "Luma/Platform/Window.h"
#include "Luma/RHI/VulkanRenderer.h"
#include "Luma/Slate/Context.h"
#include "MaterialPreview.h"

using namespace Luma;
namespace fs = std::filesystem;
using MaterialGraph::NodeId;
using MaterialGraph::PropertyValue;

namespace {

// A shaded sphere on a checker floor, standing in for the GPU preview.
TextureHandle MakeStandInPreview(Renderer& renderer) {
    constexpr u32 W = 512, H = 512;
    std::vector<u8> px(W * H * 4);
    for (u32 y = 0; y < H; ++y) {
        for (u32 x = 0; x < W; ++x) {
            const f32 u = (x + 0.5f) / W * 2.0f - 1.0f, v = (y + 0.5f) / H * 2.0f - 1.0f;
            f32 r = 0.16f, g = 0.17f, b = 0.19f;
            if (v > 0.45f) r = g = b = 0.25f + 0.1f * std::fmod(std::floor(u * 6) + std::floor(v * 6) + 20, 2.0f);
            const f32 d = u * u + (v + 0.1f) * (v + 0.1f);
            if (d < 0.36f) {
                const f32 nz = std::sqrt(0.36f - d) / 0.6f;
                const f32 lit = 0.15f + 0.85f * std::max(0.0f, nz * 0.8f - u * 0.3f - (v + 0.1f) * 0.4f);
                r = 0.85f * lit, g = 0.25f * lit, b = 0.18f * lit;
            }
            u8* p = &px[(y * W + x) * 4];
            p[0] = static_cast<u8>(std::min(r, 1.0f) * 255);
            p[1] = static_cast<u8>(std::min(g, 1.0f) * 255);
            p[2] = static_cast<u8>(std::min(b, 1.0f) * 255);
            p[3] = 255;
        }
    }
    return renderer.CreateTexture(W, H, px.data());
}

Material::GraphMaterial MakeMaterial() {
    Material::GraphMaterial m;
    m.name = "M_Brick";
    auto& g = m.graph;
    std::string err;
    auto node = [&](const char* type, f32 x, f32 y) { return g.AddNode(type, {x, y}); };
    const NodeId out = g.OutputNode();
    g.Find(out)->position = {400, 0};
    const NodeId uv = node("TextureCoordinate", -700, 40);
    const NodeId tex = node("TextureSample", -420, 0);
    g.Find(tex)->properties["texture"] = PropertyValue("/Engine/Textures/GridBox/GridBox_BaseColor");
    const NodeId tint = node("VectorParameter", -420, 260);
    g.Find(tint)->properties["name"] = PropertyValue("Tint");
    g.Find(tint)->properties["group"] = PropertyValue("Surface");
    g.Find(tint)->properties["default"] = PropertyValue(Math::Vec4{0.9f, 0.35f, 0.2f, 1.0f});
    const NodeId mul = node("Multiply", -120, 60);
    const NodeId rough = node("ScalarParameter", -120, 260);
    g.Find(rough)->properties["name"] = PropertyValue("Roughness");
    g.Find(rough)->properties["group"] = PropertyValue("Surface");
    g.Find(rough)->properties["default"] = PropertyValue(0.6);
    const NodeId metal = node("ScalarParameter", -120, 380);
    g.Find(metal)->properties["name"] = PropertyValue("Metallic");
    const NodeId knot = node("Reroute", 200, 300);
    g.Connect({uv, 0}, {tex, 0}, &err);
    g.Connect({tex, 0}, {mul, 0}, &err);
    g.Connect({tint, 0}, {mul, 1}, &err);
    g.Connect({mul, 0}, {out, 0}, &err);
    g.Connect({rough, 0}, {knot, 0}, &err);
    g.Connect({knot, 0}, {out, 3}, &err);
    g.Connect({metal, 0}, {out, 1}, &err);
    g.AddComment({-740, -60}, {580, 420}, "Base colour");
    return m;
}

}  // namespace

int main(int argc, char** argv) {
    Log::Init(LogLevel::Info);
    const std::string instancePng = argc > 1 ? argv[1] : "material_instance_editor.png";
    const std::string graphPng = argc > 2 ? argv[2] : "material_graph_editor.png";

    const fs::path root = fs::temp_directory_path() / "luma_material_editor_smoke";
    fs::remove_all(root);
    fs::create_directories(root / "Assets" / "Materials");
    AssetPathResolver paths;
    paths.SetGameRoot(root / "Assets");
    paths.SetEngineRoot(fs::path(LUMA_SOURCE_DIR) / "Content" / "Engine");
    Material::MaterialLibrary library(paths);
    library.SetGraphMaterial("/Game/Materials/M_Brick", MakeMaterial());
    library.SaveGraphAsset("/Game/Materials/M_Brick");
    Material::GraphMaterialInstance mi;
    mi.name = "MI_Brick_Red";
    mi.parent = "/Game/Materials/M_Brick";
    Material::ParameterOverride o;
    o.name = "Roughness";
    o.scalar = 0.35f;
    mi.Set(o);
    library.SetGraphInstance("/Game/Materials/MI_Brick_Red", mi);
    library.SaveGraphAsset("/Game/Materials/MI_Brick_Red");

    WindowProps props;
    props.title = "Material Editor Smoke";
    props.width = 1360;
    props.height = 820;
    std::unique_ptr<Window> window = Window::Create(props);
    RendererConfig rc;
    rc.appName = "Material Editor Smoke";
    std::unique_ptr<Renderer> renderer = CreateVulkanRenderer(*window, rc);
    Slate::Context ui;
    Slate::Typography& type = ui.theme().type;
    type.uiRegular = type.uiMedium = type.uiSemiBold = type.uiBold = "C:/Windows/Fonts/segoeui.ttf";
    type.mono = "C:/Windows/Fonts/consola.ttf";
    if (!ui.Init(*renderer, type, window->ContentScale())) {
        std::fprintf(stderr, "font init failed\n");
        return 1;
    }
    const TextureHandle standIn = MakeStandInPreview(*renderer);
    // The real preview scene when the Vulkan material renderer exists.
    std::unique_ptr<MaterialPreview> gpuPreview;
    if (void* r = renderer->GetVulkanMaterialDeferredRenderer()) {
        gpuPreview = std::make_unique<MaterialPreview>(*static_cast<Rendering::VulkanDeferredRenderer*>(r), library);
    }

    MaterialEditors::MaterialEditorServices services;
    // Reference adapter for the editor hookup: PreviewRequest -> MaterialPreview::Render.
    services.renderPreview = [&](const MaterialEditors::PreviewRequest& q) -> TextureHandle {
        if (!gpuPreview) return standIn;
        const int shape = std::min(static_cast<int>(q.shape), 3);
        return gpuPreview->Render(q.materialPath, static_cast<Luma::PreviewShape>(shape), q.yaw, q.pitch, q.width,
                                  q.height, q.sky, q.floor);
    };
    services.listMaterials = [] {
        return std::vector<std::string>{"/Game/Materials/M_Brick", "/Game/Materials/MI_Brick_Red"};
    };
    services.listMeshes = [] { return std::vector<std::string>{"/Game/Meshes/SM_Chair"}; };
    MaterialEditors::MaterialInstanceEditor instanceEditor("/Game/Materials/MI_Brick_Red", library, services);
    MaterialEditors::MaterialGraphEditor graphEditor("/Game/Materials/M_Brick", library, services);
    graphEditor.SetPaletteVisible(true);  // show the palette drawer
    // Select the Tint node so Details shows node properties.
    for (const auto& n : graphEditor.Model().Material().graph.Nodes())
        if (n.type == "VectorParameter") graphEditor.Controller().SelectOnly(n.id);

    const int kFrames = 20;
    for (int pass = 0; pass < 2; ++pass) {
        for (int frame = 0; frame <= kFrames; ++frame) {
            window->PollEvents();
            if (!renderer->BeginFrame()) continue;
            const f32 w = static_cast<f32>(window->Width()), h = static_cast<f32>(window->Height());
            ui.BeginFrame(w, h, 1.0f / 60.0f);
            if (pass == 0) instanceEditor.Draw(ui, {0, 0, w, h});
            else graphEditor.Draw(ui, {0, 0, w, h});
            renderer->DrawUI(ui.EndFrame());
            if (frame == kFrames) renderer->CaptureFrame(pass == 0 ? instancePng : graphPng);
            renderer->EndFrame();
        }
    }
    renderer->WaitIdle();
    std::printf("captured %s and %s\n", instancePng.c_str(), graphPng.c_str());
    std::error_code ec;
    fs::remove_all(root, ec);
    return 0;
}
