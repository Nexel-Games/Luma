#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <filesystem>
#include <fstream>

#include "Luma/AssetImport/Pipelines/ImportPlan.h"
#include "Luma/AssetImport/Translators/ImageTranslator.h"
#include "Luma/AssetImport/Translators/ModelTranslator.h"

using Catch::Approx;
using namespace Luma;
using namespace Luma::Import;

namespace {

std::filesystem::path Fixture(const char* name) {
    return std::filesystem::path(LUMA_SOURCE_DIR) / "Tests" / "Fixtures" / "Import" / name;
}

const std::filesystem::path kDest{"E:/LumaImport"};

ImportScene Robot() {
    ImportScene scene;
    std::string error;
    REQUIRE(ModelTranslator{}.Translate(Fixture("robot.gltf"), scene, &error));
    return scene;
}

int Count(const ImportPlan& plan, AssetType type) {
    int count = 0;
    for (const AssetRequest& request : plan.requests) {
        if (request.type == type) {
            ++count;
        }
    }
    return count;
}

bool HasFile(const ImportPlan& plan, const char* name) {
    for (const AssetRequest& request : plan.requests) {
        if (request.destFile.filename() == name) {
            return true;
        }
    }
    return false;
}

ImportNode* FindNamed(ImportScene& scene, const std::string& name) {
    for (ImportNode& node : scene.nodes) {
        if (node.name == name) {
            return &node;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("SanitizeAssetName keeps letters, digits, and single underscores", "[import][pipeline]") {
    REQUIRE(SanitizeAssetName("Robot Arm (L).001") == "Robot_Arm_L_001");
    REQUIRE(SanitizeAssetName("") == "Asset");
    REQUIRE(SanitizeAssetName("___") == "Asset");
    REQUIRE(SanitizeAssetName("Body") == "Body");
}

TEST_CASE("Robot glTF plans meshes, materials, and textures", "[import][pipeline]") {
    const ImportScene scene = Robot();
    PipelineStack stack;
    const ImportPlan plan = stack.BuildPlan(scene, kDest);

    REQUIRE(Count(plan, AssetType::Mesh) == 2);
    REQUIRE(Count(plan, AssetType::Material) == 2);
    REQUIRE(Count(plan, AssetType::Texture) == 2);
    REQUIRE(HasFile(plan, "SM_Body.lmesh"));
    REQUIRE(HasFile(plan, "SM_Arm.lmesh"));
    REQUIRE(HasFile(plan, "M_Paint.lmat"));
    REQUIRE(HasFile(plan, "M_Metal.lmat"));
    REQUIRE(HasFile(plan, "T_Paint_D.png"));
    REQUIRE(HasFile(plan, "T_Paint_N.png"));

    stack.Meshes().Set("combineMeshes", true);
    const ImportPlan combined = stack.BuildPlan(scene, kDest);
    REQUIRE(Count(combined, AssetType::Mesh) == 1);
    REQUIRE(HasFile(combined, "SM_robot.lmesh"));
    REQUIRE(Count(combined, AssetType::Material) == 2);
    REQUIRE(Count(combined, AssetType::Texture) == 2);
}

TEST_CASE("Disabling a node drops only what nothing else uses", "[import][pipeline]") {
    ImportScene scene = Robot();
    ImportNode* arm = FindNamed(scene, "Arm");
    REQUIRE(arm);
    arm->enabled = false;
    PipelineStack stack;
    ImportPlan plan = stack.BuildPlan(scene, kDest);
    REQUIRE_FALSE(HasFile(plan, "SM_Arm.lmesh"));
    REQUIRE(HasFile(plan, "SM_Body.lmesh"));
    REQUIRE(HasFile(plan, "M_Paint.lmat"));
    REQUIRE_FALSE(HasFile(plan, "M_Metal.lmat"));

    scene = Robot();
    ImportNode* normal = FindNamed(scene, "T_Paint_N");
    REQUIRE(normal);
    normal->enabled = false;
    plan = stack.BuildPlan(scene, kDest);
    REQUIRE(HasFile(plan, "M_Paint.lmat"));
    REQUIRE_FALSE(HasFile(plan, "T_Paint_N.png"));
    REQUIRE(HasFile(plan, "T_Paint_D.png"));

    stack.Materials().Set("materialMode", "Do Not Import");
    plan = stack.BuildPlan(Robot(), kDest);
    REQUIRE(Count(plan, AssetType::Mesh) == 2);
    REQUIRE(Count(plan, AssetType::Material) == 0);
    REQUIRE(Count(plan, AssetType::Texture) == 0);

    ImportScene image;
    std::string error;
    REQUIRE(ImageTranslator{}.Translate(Fixture("img_rgba.png"), image, &error));
    plan = stack.BuildPlan(image, kDest);
    REQUIRE(Count(plan, AssetType::Texture) == 1);
    REQUIRE(HasFile(plan, "T_img_rgba.png"));
}

TEST_CASE("Conflicts rename, skip, or report the existing file", "[import][pipeline]") {
    const auto temp = std::filesystem::temp_directory_path() / "luma-import-plan";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);
    {
        std::ofstream existing(temp / "SM_Body.lmesh", std::ios::binary);
        existing << "mesh";
    }
    const ImportScene scene = Robot();

    PipelineStack keepBoth;
    const ImportPlan renamed = keepBoth.BuildPlan(scene, temp);
    REQUIRE(HasFile(renamed, "SM_Body_1.lmesh"));
    REQUIRE_FALSE(HasFile(renamed, "SM_Body.lmesh"));

    PipelineStack skip;
    skip.Common().Set("conflict", "Skip");
    const ImportPlan skipped = skip.BuildPlan(scene, temp);
    bool sawSkip = false;
    for (const AssetRequest& request : skipped.requests) {
        if (request.destFile.filename() == "SM_Body.lmesh") {
            REQUIRE(request.policy == ConflictPolicy::Skip);
            REQUIRE(request.exists);
            sawSkip = true;
        }
    }
    REQUIRE(sawSkip);
    std::filesystem::remove_all(temp);
}

TEST_CASE("Pipeline settings clamp and round-trip through JSON", "[import][pipeline]") {
    PipelineStack stack;
    bool sawVisibleIf = false;
    for (const SettingField& field : stack.Meshes().Fields()) {
        if (field.key == "smoothingAngle") {
            REQUIRE(field.visibleIfKey == "!importNormals");
            sawVisibleIf = true;
        }
    }
    REQUIRE(sawVisibleIf);

    stack.Meshes().Set("uniformScale", 10000.0);
    REQUIRE(stack.Meshes().GetFloat("uniformScale") == Approx(1000.0));
    stack.Meshes().Set("uniformScale", -5.0);
    REQUIRE(stack.Meshes().GetFloat("uniformScale") == Approx(0.0001));

    stack.Common().Set("prefixMesh", "SK_");
    stack.Meshes().Set("combineMeshes", true);
    const SerialValue json = stack.ToJson();
    PipelineStack loaded;
    loaded.FromJson(json);
    REQUIRE(loaded.Common().GetString("prefixMesh") == "SK_");
    REQUIRE(loaded.Meshes().GetBool("combineMeshes"));
}

TEST_CASE("Sub-folder names are set in the import settings", "[import][pipeline]") {
    const ImportScene scene = Robot();
    PipelineStack stack;
    SettingsBlock& common = stack.Common();

    // The four name fields only show while sub-folders are on.
    const SettingField* meshesField = nullptr;
    for (const SettingField& field : common.Fields()) {
        if (field.key == "folderMeshes") meshesField = &field;
    }
    REQUIRE(meshesField != nullptr);
    REQUIRE_FALSE(common.IsVisible(*meshesField));
    common.Set("subfolders", true);
    REQUIRE(common.IsVisible(*meshesField));

    // Defaults.
    ImportPlan plan = stack.BuildPlan(scene, kDest);
    REQUIRE(HasFile(plan, "SM_Body.lmesh"));
    for (const AssetRequest& request : plan.requests) {
        const std::string folder = request.destFile.parent_path().filename().string();
        if (request.type == AssetType::Mesh) REQUIRE(folder == "Meshes");
        if (request.type == AssetType::Material) REQUIRE(folder == "Materials");
        if (request.type == AssetType::Texture) REQUIRE(folder == "Textures");
    }

    // Custom names; unsafe characters and path tricks are cleaned; empty falls back.
    common.Set("folderMeshes", std::string("Robot Meshes"));
    common.Set("folderMaterials", std::string("../Mats"));
    common.Set("folderTextures", std::string("   "));
    plan = stack.BuildPlan(scene, kDest);
    for (const AssetRequest& request : plan.requests) {
        REQUIRE(request.destFile.parent_path().parent_path() == kDest);
        const std::string folder = request.destFile.parent_path().filename().string();
        if (request.type == AssetType::Mesh) REQUIRE(folder == "Robot Meshes");
        if (request.type == AssetType::Material) REQUIRE(folder == "Mats");
        if (request.type == AssetType::Texture) REQUIRE(folder == "Textures");
    }
    REQUIRE(SubfolderName("a/b\\c:d*e") == "a_b_c_d_e");
    REQUIRE(SubfolderName("..") == "");
    REQUIRE(SubfolderName("  Rock  ") == "Rock");
}
