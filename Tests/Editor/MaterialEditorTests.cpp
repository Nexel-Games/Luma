#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "Luma/Asset/AssetPath.h"
#include "Luma/Editor/Panels/MaterialEditModel.h"
#include "Luma/Material/MaterialLibrary.h"

namespace fs = std::filesystem;
using Luma::AssetPathResolver;
using Luma::Editor::Panels::MaterialEditModel;
using Luma::Material::BlendMode;
using Luma::Material::MaterialInstance;
using Luma::Material::MaterialLibrary;
using Luma::Material::MaterialParameter;
using MaterialAsset = Luma::Material::Material;

namespace {

struct EditorFixture {
    fs::path root = fs::temp_directory_path() / "luma_material_editor_test";
    AssetPathResolver paths;
    std::unique_ptr<MaterialLibrary> lib;

    EditorFixture() {
        fs::remove_all(root);
        fs::create_directories(root / "Assets" / "Materials");
        paths.SetGameRoot(root / "Assets");
        paths.SetEngineRoot(fs::path(LUMA_SOURCE_DIR) / "Content" / "Engine");
        lib = std::make_unique<MaterialLibrary>(paths);
        MaterialAsset wood = MaterialAsset::MakeDefault();
        wood.name = "M_Wood";
        wood.inputs[static_cast<size_t>(MaterialParameter::Roughness)].constant = {0.7f, 0.7f, 0.7f, 0.7f};
        lib->SetMaterial("/Game/Materials/M_Wood", wood);
        REQUIRE(lib->Save("/Game/Materials/M_Wood"));
        MaterialInstance inst;
        inst.name = "MI_Wood";
        inst.parent = "/Game/Materials/M_Wood";
        lib->SetInstance("/Game/Materials/MI_Wood", inst);
        REQUIRE(lib->Save("/Game/Materials/MI_Wood"));
    }
    ~EditorFixture() {
        lib.reset();
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

float Roughness(MaterialLibrary& lib, const char* path) {
    return lib.Resolve(path).inputs[static_cast<size_t>(MaterialParameter::Roughness)].constant.x;
}

}  // namespace

TEST_CASE("Editing a material input marks it dirty and re-resolves", "[materialeditor]") {
    EditorFixture f;
    MaterialEditModel model("/Game/Materials/M_Wood", *f.lib);
    REQUIRE(model.Valid());
    CHECK_FALSE(model.IsInstance());
    CHECK_FALSE(model.IsDirty());
    const auto before = f.lib->Version("/Game/Materials/M_Wood");

    auto input = model.EffectiveInput(MaterialParameter::Roughness);
    input.constant = {0.2f, 0.2f, 0.2f, 0.2f};
    model.SetInput(MaterialParameter::Roughness, input);

    CHECK(model.IsDirty());
    CHECK(f.lib->Version("/Game/Materials/M_Wood") > before);
    CHECK(Roughness(*f.lib, "/Game/Materials/M_Wood") == 0.2f);
    // Children follow their parent.
    CHECK(Roughness(*f.lib, "/Game/Materials/MI_Wood") == 0.2f);
}

TEST_CASE("Instance overrides start from and fall back to the parent", "[materialeditor]") {
    EditorFixture f;
    MaterialEditModel model("/Game/Materials/MI_Wood", *f.lib);
    REQUIRE(model.IsInstance());
    CHECK_FALSE(model.IsOverridden(MaterialParameter::Roughness));
    CHECK(model.EffectiveInput(MaterialParameter::Roughness).constant.x == 0.7f);

    auto input = model.EffectiveInput(MaterialParameter::Roughness);
    input.constant = {0.1f, 0.1f, 0.1f, 0.1f};
    model.SetInput(MaterialParameter::Roughness, input);  // implies an override
    CHECK(model.IsOverridden(MaterialParameter::Roughness));
    CHECK(Roughness(*f.lib, "/Game/Materials/MI_Wood") == 0.1f);

    model.SetOverride(MaterialParameter::Roughness, false);
    CHECK_FALSE(model.IsOverridden(MaterialParameter::Roughness));
    CHECK(Roughness(*f.lib, "/Game/Materials/MI_Wood") == 0.7f);

    model.SetOverride(MaterialParameter::Roughness, true);  // re-check: inherited value
    CHECK(Roughness(*f.lib, "/Game/Materials/MI_Wood") == 0.7f);
}

TEST_CASE("Instance setting overrides", "[materialeditor]") {
    EditorFixture f;
    MaterialEditModel model("/Game/Materials/MI_Wood", *f.lib);
    CHECK_FALSE(model.IsSettingOverridden(MaterialEditModel::Setting::BlendMode));
    model.SetBlendMode(BlendMode::Masked);
    CHECK(model.IsSettingOverridden(MaterialEditModel::Setting::BlendMode));
    CHECK(f.lib->Resolve("/Game/Materials/MI_Wood").settings.blendMode == BlendMode::Masked);
    model.SetSettingOverride(MaterialEditModel::Setting::BlendMode, false);
    CHECK(f.lib->Resolve("/Game/Materials/MI_Wood").settings.blendMode == BlendMode::Opaque);
}

TEST_CASE("A cyclic parent is refused", "[materialeditor]") {
    EditorFixture f;
    MaterialInstance child;
    child.name = "MI_Child";
    child.parent = "/Game/Materials/MI_Wood";
    f.lib->SetInstance("/Game/Materials/MI_Child", child);

    MaterialEditModel model("/Game/Materials/MI_Wood", *f.lib);
    std::string error;
    CHECK_FALSE(model.SetParent("/Game/Materials/MI_Child", &error));
    CHECK_FALSE(error.empty());
    CHECK(model.Parent() == "/Game/Materials/M_Wood");
    CHECK_FALSE(model.SetParent("/Game/Materials/MI_Wood", &error));  // itself
    CHECK(model.SetParent(std::string(Luma::Material::kDefaultMaterialPath), &error));
    CHECK(model.Parent() == Luma::Material::kDefaultMaterialPath);
}

TEST_CASE("Save writes the asset and clears dirty", "[materialeditor]") {
    EditorFixture f;
    MaterialEditModel model("/Game/Materials/M_Wood", *f.lib);
    auto input = model.EffectiveInput(MaterialParameter::Metallic);
    input.constant = {1, 1, 1, 1};
    model.SetInput(MaterialParameter::Metallic, input);
    REQUIRE(model.IsDirty());
    const auto file = f.root / "Assets" / "Materials" / "M_Wood.lmat";
    const auto before = fs::last_write_time(file);
    std::string error;
    REQUIRE(model.Save(&error));
    CHECK_FALSE(model.IsDirty());
    MaterialLibrary fresh(f.paths);
    CHECK(fresh.Resolve("/Game/Materials/M_Wood")
              .inputs[static_cast<size_t>(MaterialParameter::Metallic)].constant.x == 1.0f);
    CHECK(fs::last_write_time(file) >= before);
}

TEST_CASE("Unknown assets give an invalid model", "[materialeditor]") {
    EditorFixture f;
    MaterialEditModel model("/Game/Materials/M_Missing", *f.lib);
    CHECK_FALSE(model.Valid());
}

TEST_CASE("Editing keeps references to textures that are missing", "[materialeditor]") {
    EditorFixture f;
    MaterialAsset m = MaterialAsset::MakeDefault();
    m.name = "M_Moved";
    m.inputs[static_cast<size_t>(MaterialParameter::BaseColor)].texture = "/Game/Textures/T_Gone";
    f.lib->SetMaterial("/Game/Materials/M_Moved", m);
    MaterialEditModel model("/Game/Materials/M_Moved", *f.lib);
    CHECK(model.EffectiveInput(MaterialParameter::BaseColor).texture == "/Game/Textures/T_Gone");
    auto rough = model.EffectiveInput(MaterialParameter::Roughness);
    rough.constant = {0.9f, 0.9f, 0.9f, 0.9f};
    model.SetInput(MaterialParameter::Roughness, rough);
    CHECK(f.lib->GetMaterial("/Game/Materials/M_Moved")
              ->inputs[static_cast<size_t>(MaterialParameter::BaseColor)].texture ==
          "/Game/Textures/T_Gone");
    CHECK(model.IsTextureMissing(MaterialParameter::BaseColor));
}
