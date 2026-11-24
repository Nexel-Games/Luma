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

TEST_CASE("Inspector sanitises out-of-range camera values", "[inspector]") {
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
    auto& cam = f.scene.Registry().emplace<CameraComponent>(f.actor);
    cam.fovYDegrees = 0.0f;  // e.g. a hand-edited scene file
    cam.nearZ = 0.0f;
    cam.farZ = 0.0f;
    ui.BeginFrame(1280, 720, 0.0166f);
    inspector.Draw(ui, {0, 0, 400, 720}, ctx);
    ui.EndFrame();
    CHECK(cam.fovYDegrees >= 5.0f);
    CHECK(cam.nearZ >= 0.001f);
    CHECK(cam.farZ > cam.nearZ);
}

TEST_CASE("Inspector field ids differ per selected entity", "[inspector]") {
    // A value typed for one actor must never land on another actor drawn
    // with the same row: the row ids mix in the entity.
    CHECK(InspectorPanel::RowId("insp.lint", static_cast<Entity>(1)) !=
          InspectorPanel::RowId("insp.lint", static_cast<Entity>(2)));
    CHECK(InspectorPanel::RowId("insp.lint", static_cast<Entity>(1)) ==
          InspectorPanel::RowId("insp.lint", static_cast<Entity>(1)));
}

// --- Material parameters under a slot ------------------------------------------

#include "Luma/Editor/Panels/SlotParameters.h"
#include "Luma/Material/GraphAssets.h"

TEST_CASE("A slot's parameters edit its instance, its shared material, or nothing", "[inspector][material]") {
    using Luma::Editor::Panels::SlotParameterMode;
    using Luma::Editor::Panels::SlotParameterModeFor;
    const fs::path root = fs::temp_directory_path() / "luma_slot_parameter_test";
    fs::remove_all(root);
    fs::create_directories(root / "Assets");
    AssetPathResolver paths;
    paths.SetGameRoot(root / "Assets");
    paths.SetEngineRoot(fs::path(LUMA_SOURCE_DIR) / "Content" / "Engine");
    MaterialLibrary lib(paths);
    lib.SetGraphMaterial("/Game/M_Water", Material::GraphMaterial::MakeDefault());
    Material::GraphMaterialInstance mi;
    mi.parent = "/Game/M_Water";
    lib.SetGraphInstance("/Game/MI_Water", mi);

    CHECK(SlotParameterModeFor("/Game/MI_Water", lib) == SlotParameterMode::Instance);
    CHECK(SlotParameterModeFor("/Game/M_Water", lib) == SlotParameterMode::SharedMaterial);
    // Engine content is never edited from a level object.
    CHECK(SlotParameterModeFor(std::string(Material::kDefaultMaterialPath), lib) == SlotParameterMode::ReadOnly);
    CHECK(SlotParameterModeFor("/Game/Missing", lib) == SlotParameterMode::None);
    CHECK(SlotParameterModeFor("", lib) == SlotParameterMode::None);
    std::error_code ec;
    fs::remove_all(root, ec);
}

#include "Luma/Editor/Panels/AssetInspector.h"

TEST_CASE("Asset info formats sizes and file times for the Inspector", "[inspector][asset]") {
    using Luma::Editor::Panels::FormatAssetSize;
    using Luma::Editor::Panels::FormatAssetTime;
    CHECK(FormatAssetSize(0) == "0 bytes");
    CHECK(FormatAssetSize(900) == "900 bytes");
    CHECK(FormatAssetSize(1536) == "1.5 KB");
    CHECK(FormatAssetSize(5u * 1024u * 1024u) == "5.0 MB");
    CHECK(FormatAssetSize(3ull * 1024u * 1024u * 1024u) == "3.0 GB");
    // 2026-01-02 03:04:05 UTC as a Windows FILETIME (100 ns since 1601).
    const i64 unixSeconds = 1767323045;
    const i64 filetime = (unixSeconds + 11644473600ll) * 10000000ll;
    const std::string text = FormatAssetTime(filetime);
    CHECK(text.size() == 16);  // "YYYY-MM-DD HH:MM" in local time
    CHECK(text.rfind("2026-01-0", 0) == 0);
    CHECK(FormatAssetTime(0) == "Unknown");
}

#include "Luma/Asset/LumaMesh.h"
#include "Luma/AssetImport/ImportMetadata.h"

TEST_CASE("The asset Inspector writes mesh section materials and placement transforms", "[inspector][asset]") {
    using Luma::Editor::Panels::SetAssetPlacement;
    using Luma::Editor::Panels::SetMeshSectionMaterial;
    const fs::path root = fs::temp_directory_path() / "luma_asset_inspector_write_test";
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path meshFile = root / "SM_Box.lmesh";
    LumaMeshData mesh;
    mesh.name = "SM_Box";
    mesh.vertices.resize(3);
    mesh.indices = {0, 1, 2};
    LumaSubmesh a{0, 3, 0, 3, "/Game/M_Old", {}};
    mesh.submeshes = {a, a};
    REQUIRE(LumaMeshIO::WriteMesh(meshFile, mesh));

    std::string error;
    REQUIRE(SetMeshSectionMaterial(meshFile, 1, "/Game/M_New", &error));
    const auto reread = LumaMeshIO::ReadMesh(meshFile);
    REQUIRE(reread);
    CHECK(reread->submeshes[0].materialName == "/Game/M_Old");
    CHECK(reread->submeshes[1].materialName == "/Game/M_New");
    CHECK(reread->indices.size() == 3);  // geometry untouched
    CHECK_FALSE(SetMeshSectionMaterial(meshFile, 5, "/Game/M_New", &error));
    CHECK_FALSE(error.empty());

    // No .meta yet: one is created with the asset's id and type.
    Import::PlacementTransform p;
    p.rotation = {0.0f, 45.0f, 0.0f};
    p.scale = {2.0f, 2.0f, 2.0f};
    const AssetId id = MakeAssetIdFromKey("SM_Box");
    REQUIRE(SetAssetPlacement(meshFile, id, AssetType::Mesh, p, &error));
    auto meta = Import::ReadMetadata(meshFile);
    REQUIRE(meta);
    CHECK(meta->guid == id);
    CHECK(meta->type == AssetType::Mesh);
    CHECK(meta->placement.rotation.y == 45.0f);
    // An existing .meta keeps its other fields.
    meta->sourceFile = "C:/src/box.fbx";
    REQUIRE(Import::WriteMetadata(meshFile, *meta, &error));
    p.scale = {1.0f, 1.0f, 1.0f};
    REQUIRE(SetAssetPlacement(meshFile, id, AssetType::Mesh, p, &error));
    meta = Import::ReadMetadata(meshFile);
    CHECK(meta->sourceFile == "C:/src/box.fbx");
    CHECK(meta->placement.scale.x == 1.0f);
    std::error_code ec;
    fs::remove_all(root, ec);
}
