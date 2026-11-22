#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "Luma/Editor/Panels/Inspector.h"
#include "Luma/Editor/Panels/StaticMeshSlot.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Slate/Context.h"

using namespace Luma;
using Luma::Editor::Panels::AssignStaticMesh;
using Luma::Editor::Panels::InspectorPanel;
using Luma::Editor::Panels::MeshSlotInfo;
using Luma::Editor::Panels::PanelContext;
using Luma::Editor::Panels::UseSelectedAssetForMesh;
using Luma::Slate::Context;
using Luma::Slate::Rect;

namespace {

class NullRenderer final : public Renderer {
public:
    void OnResize(u32, u32) override {}
    void SetClearColor(const ClearColor&) override {}
    bool BeginFrame() override { return true; }
    void EndFrame() override {}
    void DrawUI(const UIDrawData&) override {}
    TextureHandle CreateTexture(u32, u32, const void*) override { return 1; }
    void DestroyTexture(TextureHandle) override {}
    void CaptureFrame(const std::string&) override {}
    void WaitIdle() override {}
};

bool IsMesh(const std::string& path) { return path.find("/SM_") != std::string::npos; }

struct InspectorHarness {
    NullRenderer renderer;
    Context ui;
    InspectorPanel inspector;
    PanelContext ctx;
    Scene scene;
    Entity actor = kNullEntity;

    InspectorHarness() {
        Luma::Slate::Typography typo;
        typo.uiRegular = "C:/Windows/Fonts/segoeui.ttf";
        typo.uiMedium = typo.uiSemiBold = typo.uiBold = typo.mono = typo.uiRegular;
        ui.Init(renderer, typo);
        actor = scene.CreateEntity("Rock");
        scene.Registry().emplace<MeshRendererComponent>(actor);
        ctx.scene = &scene;
        ctx.selected = &actor;
        ctx.listMeshes = [] { return std::vector<std::string>{"/Game/Props/SM_Rock"}; };
    }
    MeshRendererComponent& Mesh() { return scene.Registry().get<MeshRendererComponent>(actor); }

    // Drags a payload of `type` from off-panel onto the centre of `target`.
    void DropOn(const std::string& type, const std::string& data, const Rect& target) {
        const Rect source{1000, 600, 40, 40};
        auto frame = [&] {
            ui.BeginFrame(1280, 720, 0.0166f);
            Luma::Slate::DragPayload payload;
            payload.type = type;
            payload.data = data;
            payload.label = "tile";
            ui.BeginDrag(Context::ID("test.tile"), source, payload);
            inspector.Draw(ui, {0, 0, 400, 720}, ctx);
            ui.EndFrame();
        };
        ui.OnMouseMove(1010, 610);
        ui.OnMouseButton(0, true);
        frame();
        ui.OnMouseMove(target.x + target.w * 0.5f, target.y + target.h * 0.5f);
        frame();
        ui.OnMouseButton(0, false);
        frame();
    }
    void Frame() {
        ui.BeginFrame(1280, 720, 0.0166f);
        inspector.Draw(ui, {0, 0, 400, 720}, ctx);
        ui.EndFrame();
    }
};

}  // namespace

TEST_CASE("Assigning a static mesh keeps the material overrides", "[inspector][mesh]") {
    MeshRendererComponent mr;
    mr.materials = {"/Game/M_A", "", "/Game/M_C"};
    AssignStaticMesh(mr, "/Game/Props/SM_Rock");
    CHECK(mr.mesh == "/Game/Props/SM_Rock");
    CHECK(mr.UsesMeshAsset());
    CHECK(mr.materials.size() == 3);
    AssignStaticMesh(mr, "");
    CHECK_FALSE(mr.UsesMeshAsset());
    CHECK(mr.materials.size() == 3);
}

TEST_CASE("Use Selected assigns only mesh assets", "[inspector][mesh]") {
    MeshRendererComponent mr;
    CHECK_FALSE(UseSelectedAssetForMesh(mr, "/Game/Materials/M_Wood", IsMesh));
    CHECK(mr.mesh.empty());
    CHECK_FALSE(UseSelectedAssetForMesh(mr, "", IsMesh));
    CHECK(UseSelectedAssetForMesh(mr, "/Game/Props/SM_Rock", IsMesh));
    CHECK(mr.mesh == "/Game/Props/SM_Rock");
}

TEST_CASE("Dropping a mesh on the Static Mesh row assigns it", "[inspector][mesh]") {
    InspectorHarness h;
    h.Frame();
    const Rect row = h.inspector.StaticMeshRowRect();
    REQUIRE(row.w > 0.0f);
    h.DropOn("asset/material", "/Game/Materials/M_Wood", row);
    CHECK(h.Mesh().mesh.empty());
    h.DropOn("asset/mesh", "/Game/Props/SM_Rock", row);
    CHECK(h.Mesh().mesh == "/Game/Props/SM_Rock");
}

TEST_CASE("Material slots follow the mesh's sections", "[inspector][mesh]") {
    InspectorHarness h;
    h.Mesh().mesh = "/Game/Props/SM_Rock";
    h.ctx.meshSections = [](Entity) {
        return std::vector<MeshSlotInfo>{{"Paint", "/Game/M_Paint"}, {"Metal", ""}};
    };
    h.Frame();
    CHECK(h.inspector.MaterialSlotRects().size() == 2);
    // Dropping a material on the second slot overrides only that slot.
    const Rect second = h.inspector.MaterialSlotRects()[1];
    h.DropOn("asset/material", "/Game/M_Chrome", second);
    REQUIRE(h.Mesh().materials.size() == 2);
    CHECK(h.Mesh().materials[0].empty());
    CHECK(h.Mesh().materials[1] == "/Game/M_Chrome");
}
