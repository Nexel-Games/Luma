#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "Luma/Asset/AssetPath.h"
#include "Luma/Editor/Panels/Inspector.h"
#include "Luma/Editor/Panels/MaterialSlots.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Slate/Context.h"

namespace fs = std::filesystem;
using namespace Luma;
using Luma::Editor::Panels::AssignMaterialSlot;
using Luma::Editor::Panels::CreateInstanceForSlot;
using Luma::Editor::Panels::UseSelectedAssetForSlot;
using Luma::Editor::Panels::InspectorPanel;
using Luma::Editor::Panels::PanelContext;
using Luma::Material::MaterialLibrary;
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

struct SlotFixture {
    fs::path root = fs::temp_directory_path() / "luma_inspector_material_test";
    AssetPathResolver paths;
    std::unique_ptr<MaterialLibrary> lib;
    Scene scene;
    Entity actor = kNullEntity;

    SlotFixture() {
        fs::remove_all(root);
        fs::create_directories(root / "Assets" / "Materials");
        paths.SetGameRoot(root / "Assets");
        paths.SetEngineRoot(fs::path(LUMA_SOURCE_DIR) / "Content" / "Engine");
        lib = std::make_unique<MaterialLibrary>(paths);
        Luma::Material::Material wood = Luma::Material::Material::MakeDefault();
        wood.name = "M_Wood";
        lib->SetMaterial("/Game/Materials/M_Wood", wood);
        REQUIRE(lib->Save("/Game/Materials/M_Wood"));
        actor = scene.CreateEntity("Crate");
        scene.Registry().emplace<MeshRendererComponent>(actor);
    }
    ~SlotFixture() {
        lib.reset();
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    MeshRendererComponent& Mesh() { return scene.Registry().get<MeshRendererComponent>(actor); }
};

}  // namespace

TEST_CASE("Assigning a slot writes the path and keeps other slots", "[inspector][materials]") {
    SlotFixture f;
    AssignMaterialSlot(f.Mesh(), 2, "/Game/Materials/M_Wood");
    REQUIRE(f.Mesh().materials.size() == 3);
    CHECK(f.Mesh().materials[0].empty());
    CHECK(f.Mesh().materials[2] == "/Game/Materials/M_Wood");
    AssignMaterialSlot(f.Mesh(), 0, "/Game/Materials/M_Stone");
    CHECK(f.Mesh().materials[0] == "/Game/Materials/M_Stone");
    CHECK(f.Mesh().materials[2] == "/Game/Materials/M_Wood");
}

TEST_CASE("Create Instance makes a saved child of the slot's material", "[inspector][materials]") {
    SlotFixture f;
    AssignMaterialSlot(f.Mesh(), 0, "/Game/Materials/M_Wood");
    std::string error;
    const std::string inst = CreateInstanceForSlot(f.Mesh(), 0, *f.lib, &error);
    REQUIRE_FALSE(inst.empty());
    CHECK(inst == "/Game/Materials/MI_Wood");
    CHECK(f.Mesh().materials[0] == inst);
    CHECK(fs::exists(f.root / "Assets" / "Materials" / "MI_Wood.lmatinst"));
    CHECK(f.lib->GetInstance(inst)->parent == "/Game/Materials/M_Wood");
    CHECK_FALSE(f.lib->IsDirty(inst));

    // An empty slot (Default Material) gets its instance under /Game.
    const std::string fromDefault = CreateInstanceForSlot(f.Mesh(), 1, *f.lib, &error);
    CHECK(fromDefault == "/Game/Materials/MI_DefaultMaterial");
    CHECK(f.lib->GetInstance(fromDefault)->parent == Luma::Material::kDefaultMaterialPath);
}

TEST_CASE("Dropping a material on a slot row assigns that slot", "[inspector][materials]") {
    SlotFixture f;
    NullRenderer renderer;
    Context ui;
    Luma::Slate::Typography typo;
    typo.uiRegular = "C:/Windows/Fonts/segoeui.ttf";
    typo.uiMedium = typo.uiSemiBold = typo.uiBold = typo.mono = typo.uiRegular;
    ui.Init(renderer, typo);
    InspectorPanel inspector;
    PanelContext ctx;
    ctx.scene = &f.scene;
    ctx.selected = &f.actor;
    ctx.materials = f.lib.get();
    ctx.sectionCount = [](Entity) { return 3u; };
    const Rect source{1000, 600, 40, 40};
    auto frame = [&] {
        ui.BeginFrame(1280, 720, 0.0166f);
        Luma::Slate::DragPayload payload;
        payload.type = "asset/material";
        payload.data = "/Game/Materials/M_Wood";
        payload.label = "M_Wood";
        ui.BeginDrag(Context::ID("test.tile"), source, payload);
        inspector.Draw(ui, {0, 0, 400, 720}, ctx);
        ui.EndFrame();
    };
    frame();
    REQUIRE(inspector.MaterialSlotRects().size() == 3);
    const Rect row = inspector.MaterialSlotRects()[1];
    ui.OnMouseMove(1010, 610);
    ui.OnMouseButton(0, true);
    frame();
    ui.OnMouseMove(row.x + row.w * 0.5f, row.y + row.h * 0.5f);
    frame();
    ui.OnMouseButton(0, false);
    frame();
    REQUIRE(f.Mesh().materials.size() >= 2);
    CHECK(f.Mesh().materials[1] == "/Game/Materials/M_Wood");
    CHECK(f.Mesh().materials[0].empty());
}

TEST_CASE("Use Selected assigns a material", "[inspector][materials]") {
    SlotFixture f;
    CHECK(UseSelectedAssetForSlot(f.Mesh(), 1, "/Game/Materials/M_Wood", *f.lib));
    REQUIRE(f.Mesh().materials.size() == 2);
    CHECK(f.Mesh().materials[1] == "/Game/Materials/M_Wood");
}

TEST_CASE("Use Selected assigns a material instance", "[inspector][materials]") {
    SlotFixture f;
    AssignMaterialSlot(f.Mesh(), 0, "/Game/Materials/M_Wood");
    std::string error;
    const std::string inst = CreateInstanceForSlot(f.Mesh(), 0, *f.lib, &error);
    REQUIRE_FALSE(inst.empty());
    CHECK(UseSelectedAssetForSlot(f.Mesh(), 2, inst, *f.lib));
    CHECK(f.Mesh().materials[2] == inst);
}

TEST_CASE("Use Selected rejects non-material assets", "[inspector][materials]") {
    SlotFixture f;
    AssignMaterialSlot(f.Mesh(), 0, "/Game/Materials/M_Wood");
    CHECK_FALSE(UseSelectedAssetForSlot(f.Mesh(), 0, "/Game/Textures/T_Wood_diff", *f.lib));
    CHECK_FALSE(UseSelectedAssetForSlot(f.Mesh(), 0, "", *f.lib));
    CHECK_FALSE(UseSelectedAssetForSlot(f.Mesh(), 0, "/Game/Materials/M_Missing", *f.lib));
    CHECK(f.Mesh().materials[0] == "/Game/Materials/M_Wood");
}
