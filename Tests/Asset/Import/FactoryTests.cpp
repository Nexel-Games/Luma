#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "Luma/Asset/AssetPath.h"
#include "Luma/AssetImport/Factories/FactoryCommon.h"
#include "Luma/Asset/ImageDecode.h"
#include "Luma/AssetImport/ImportMetadata.h"
#include "Luma/AssetImport/Translators/ImageTranslator.h"
#include "Luma/AssetImport/Translators/ModelTranslator.h"
#include "Luma/Asset/LumaMesh.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/Material/MaterialTypes.h"

using Catch::Approx;
using namespace Luma;
using namespace Luma::Import;

namespace {

std::filesystem::path Fixture(const char* name) {
    return std::filesystem::path(LUMA_SOURCE_DIR) / "Tests" / "Fixtures" / "Import" / name;
}

struct Project {
    std::filesystem::path root;
    std::filesystem::path assets;
    AssetPathResolver paths;
    Material::MaterialLibrary materials;

    explicit Project(const char* name)
        : root(std::filesystem::temp_directory_path() / name),
          assets(root / "Assets"),
          materials(paths) {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(assets);
        paths.SetGameRoot(assets);
    }

    ~Project() { std::filesystem::remove_all(root); }

    FactoryContext Context(const ImportScene& scene, const PipelineStack& settings, bool cancel = false) {
        return FactoryContext{scene, settings, &materials, paths, [cancel] { return cancel; }};
    }
};

std::string ReadBytes(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBmp(const std::filesystem::path& file, int width, int height) {
    const int row = width * 3;
    const int pixels = row * height;
    std::vector<std::uint8_t> data(54 + static_cast<size_t>(pixels), 0);
    data[0] = 'B';
    data[1] = 'M';
    const auto store = [&](int offset, std::uint32_t value) {
        data[offset] = static_cast<std::uint8_t>(value);
        data[offset + 1] = static_cast<std::uint8_t>(value >> 8);
        data[offset + 2] = static_cast<std::uint8_t>(value >> 16);
        data[offset + 3] = static_cast<std::uint8_t>(value >> 24);
    };
    store(2, static_cast<std::uint32_t>(data.size()));
    store(10, 54);
    store(14, 40);
    store(18, static_cast<std::uint32_t>(width));
    store(22, static_cast<std::uint32_t>(height));
    data[26] = 1;
    data[28] = 24;
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

const AssetRequest* FindFile(const ImportPlan& plan, const char* name) {
    for (const AssetRequest& request : plan.requests) {
        if (request.destFile.filename() == name) {
            return &request;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("Robot GLB import writes meshes, materials, and textures", "[import][factory]") {
    Project project("luma-factory-robot");
    ImportScene scene;
    std::string error;
    REQUIRE(ModelTranslator{}.Translate(Fixture("robot.glb"), scene, &error));
    PipelineStack settings;
    settings.Textures().Set("maxSize", "256");
    const ImportPlan plan = settings.BuildPlan(scene, project.assets);
    FactoryContext context = project.Context(scene, settings);

    for (const AssetRequest& request : plan.requests) {
        const FactoryResult result = CreateAsset(request, context);
        INFO(result.error);
        REQUIRE(result.ok);
        REQUIRE(std::filesystem::exists(result.file));
        REQUIRE(std::filesystem::exists(MetadataPathFor(result.file)));
    }

    for (const char* name : {"SM_Body.lmesh", "SM_Arm.lmesh", "M_Paint.lmat", "M_Metal.lmat", "T_Paint_D.png",
                             "T_Paint_N.png"}) {
        REQUIRE(std::filesystem::exists(project.assets / name));
        REQUIRE(std::filesystem::exists(project.assets / (std::string(name) + ".meta")));
    }

    const auto body = LumaMeshIO::ReadMesh(project.assets / "SM_Body.lmesh");
    REQUIRE(body);
    REQUIRE(body->vertices.size() == 24);
    REQUIRE_FALSE(body->submeshes.empty());
    REQUIRE(body->submeshes[0].materialName == "/Game/M_Paint");

    const Material::Material* paint = project.materials.GetMaterial("/Game/M_Paint");
    REQUIRE(paint != nullptr);
    REQUIRE(paint->Input(Material::MaterialParameter::BaseColor).texture == "/Game/T_Paint_D");
    REQUIRE(paint->Input(Material::MaterialParameter::Normal).texture == "/Game/T_Paint_N");
    REQUIRE(paint->Input(Material::MaterialParameter::Roughness).constant.x == Approx(0.6f));

    const auto diffuse = DecodeImageFile(project.assets / "T_Paint_D.png", &error);
    REQUIRE(diffuse);
    REQUIRE(diffuse->width == 64);
    REQUIRE(diffuse->height == 64);

    const AssetRequest* bodyRequest = FindFile(plan, "SM_Body.lmesh");
    REQUIRE(bodyRequest != nullptr);
    const auto first = ReadMetadata(bodyRequest->destFile);
    REQUIRE(first);
    AssetRequest again = *bodyRequest;
    again.policy = ConflictPolicy::Overwrite;
    again.exists = true;
    const FactoryResult overwritten = CreateAsset(again, context);
    REQUIRE(overwritten.ok);
    const auto second = ReadMetadata(bodyRequest->destFile);
    REQUIRE(second);
    REQUIRE(second->guid == first->guid);

    std::ofstream oldFile(project.assets / "SM_Arm.lmesh", std::ios::binary | std::ios::trunc);
    oldFile << "keep-me";
    oldFile.close();
    AssetRequest skip = *FindFile(plan, "SM_Arm.lmesh");
    skip.policy = ConflictPolicy::Skip;
    skip.exists = true;
    const FactoryResult skipped = CreateAsset(skip, context);
    REQUIRE(skipped.ok);
    REQUIRE(skipped.skipped);
    REQUIRE(ReadBytes(project.assets / "SM_Arm.lmesh") == "keep-me");
}

TEST_CASE("A cancelled import leaves no files or temp files", "[import][factory]") {
    Project project("luma-factory-cancel");
    ImportScene scene;
    std::string error;
    REQUIRE(ModelTranslator{}.Translate(Fixture("robot.glb"), scene, &error));
    PipelineStack settings;
    const ImportPlan plan = settings.BuildPlan(scene, project.assets);
    FactoryContext context = project.Context(scene, settings, true);
    for (const AssetRequest& request : plan.requests) {
        const FactoryResult result = CreateAsset(request, context);
        REQUIRE_FALSE(result.ok);
    }
    for (const auto& entry : std::filesystem::directory_iterator(project.assets)) {
        const std::string name = entry.path().filename().string();
        REQUIRE(name.find(".tmp") == std::string::npos);
        REQUIRE(entry.path().extension() != ".lmesh");
        REQUIRE(entry.path().extension() != ".lmat");
        REQUIRE(entry.path().extension() != ".png");
    }
}

TEST_CASE("PSD imports as PNG and maxSize downscales only when needed", "[import][factory]") {
    Project project("luma-factory-images");
    std::string error;
    PipelineStack settings;
    settings.Textures().Set("maxSize", "256");

    ImportScene psd;
    REQUIRE(ImageTranslator{}.Translate(Fixture("img.psd"), psd, &error));
    FactoryContext context = project.Context(psd, settings);
    const ImportPlan psdPlan = settings.BuildPlan(psd, project.assets);
    REQUIRE(psdPlan.requests.size() == 1);
    const FactoryResult psdResult = CreateAsset(psdPlan.requests[0], context);
    INFO(psdResult.error);
    REQUIRE(psdResult.ok);
    REQUIRE(psdResult.file.extension() == ".png");
    const auto decoded = DecodeImageFile(psdResult.file, &error);
    REQUIRE(decoded);
    REQUIRE(decoded->width == 32);
    REQUIRE(decoded->height == 16);

    WriteBmp(project.assets / "big.bmp", 1024, 1024);
    ImportScene big;
    REQUIRE(ImageTranslator{}.Translate(project.assets / "big.bmp", big, &error));
    const ImportPlan bigPlan = settings.BuildPlan(big, project.assets);
    REQUIRE(bigPlan.requests.size() == 1);
    const FactoryResult bigResult = CreateAsset(bigPlan.requests[0], project.Context(big, settings));
    INFO(bigResult.error);
    REQUIRE(bigResult.ok);
    const auto scaled = DecodeImageFile(bigResult.file, &error);
    REQUIRE(scaled);
    REQUIRE(scaled->width == 256);
    REQUIRE(scaled->height == 256);
}
