#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "Luma/Asset/AssetPath.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/Scene/MaterialMigration.h"
#include "Luma/Scene/SceneSerializer.h"
#include "Luma/Serialization/Json.h"

namespace fs = std::filesystem;
using Luma::AssetPathResolver;
using Luma::Entity;
using Luma::LegacyMaterialValues;
using Luma::MeshRendererComponent;
using Luma::MigrateLegacyMaterials;
using Luma::NameComponent;
using Luma::Scene;
using Luma::SceneSerializer;
using Luma::Material::MaterialLibrary;
using Luma::Material::MaterialParameter;

namespace {

struct MigrationFixture {
    fs::path root = fs::temp_directory_path() / "luma_migration_test";
    AssetPathResolver paths;

    MigrationFixture() {
        fs::remove_all(root);
        fs::create_directories(root / "Assets");
        paths.SetGameRoot(root / "Assets");
        paths.SetEngineRoot(fs::path(LUMA_SOURCE_DIR) / "Content" / "Engine");
    }
    ~MigrationFixture() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

std::string LegacyEntity(const char* name, const char* albedo, float metallic, float roughness) {
    return std::string(R"({"components":{"Name":{"name":")") + name +
           R"("},"Transform":{},"MeshRenderer":{"primitive":0,"albedo":)" + albedo +
           R"(,"metallic":)" + std::to_string(metallic) + R"(,"roughness":)" +
           std::to_string(roughness) + "}}}";
}

const char* kDefault = "[0.82,0.82,0.85]";
const char* kRed = "[0.8,0.1,0.1]";
const char* kBlue = "[0.1,0.2,0.9]";

std::string LegacyScene(const std::vector<std::string>& entities) {
    std::string json = R"({"version":1,"entities":[)";
    for (Luma::usize i = 0; i < entities.size(); ++i) {
        if (i) json += ",";
        json += entities[i];
    }
    return json + "]}";
}

Entity Find(Scene& scene, const std::string& name) {
    auto view = scene.Registry().view<const NameComponent>();
    for (Entity e : view) {
        if (view.get<const NameComponent>(e).name == name) return e;
    }
    return Luma::kNullEntity;
}

const std::vector<std::string>& Slots(Scene& scene, const std::string& name) {
    return scene.Registry().get<MeshRendererComponent>(Find(scene, name)).materials;
}

}  // namespace

TEST_CASE("Legacy scene colors migrate to material instances", "[migration]") {
    MigrationFixture f;
    MaterialLibrary lib(f.paths);
    Scene scene;
    std::vector<std::pair<Entity, LegacyMaterialValues>> legacy;
    const std::string json = LegacyScene({
        LegacyEntity("Floor", kDefault, 0.0f, 0.5f), LegacyEntity("Wall", kDefault, 0.0f, 0.5f),
        LegacyEntity("Pillar", kDefault, 0.0f, 0.5f), LegacyEntity("Red Box", kRed, 0.0f, 0.5f),
        LegacyEntity("Red Ball", kRed, 0.0f, 0.5f), LegacyEntity("Blue", kBlue, 0.5f, 0.3f)});
    REQUIRE(SceneSerializer::LoadFromString(scene, json, nullptr, &legacy));
    CHECK(legacy.size() == 6);

    const auto result = MigrateLegacyMaterials(scene, legacy, lib);
    CHECK(result.created.size() == 2);
    CHECK(result.reused.empty());
    CHECK(Slots(scene, "Floor").empty());
    CHECK(Slots(scene, "Wall").empty());
    CHECK(Slots(scene, "Pillar").empty());

    REQUIRE(Slots(scene, "Red Box").size() == 1);
    CHECK(Slots(scene, "Red Box") == Slots(scene, "Red Ball"));
    CHECK(Slots(scene, "Red Box")[0] == "/Game/Materials/Migrated/MI_Red_Box");
    REQUIRE(Slots(scene, "Blue").size() == 1);

    const auto& red = lib.Resolve(Slots(scene, "Red Box")[0]);
    const auto& redBase = red.inputs[static_cast<Luma::usize>(MaterialParameter::BaseColor)].constant;
    CHECK((redBase.x == 0.8f && redBase.y == 0.1f && redBase.z == 0.1f));
    const auto& blue = lib.Resolve(Slots(scene, "Blue")[0]);
    CHECK(blue.inputs[static_cast<Luma::usize>(MaterialParameter::Metallic)].constant.x == 0.5f);
    CHECK(blue.inputs[static_cast<Luma::usize>(MaterialParameter::Roughness)].constant.x == 0.3f);
    CHECK(blue.rootMaterial == std::string(Luma::Material::kDefaultMaterialPath));

    // Nothing is written until the library saves, and the dirty list (which
    // the unsaved-changes prompt reads) names both new instances.
    CHECK_FALSE(fs::exists(f.root / "Assets" / "Materials"));
    const auto dirty = lib.DirtyAssets();
    CHECK(dirty.size() == 2);
    for (const auto& [path, instance] : result.created) {
        CHECK(std::find(dirty.begin(), dirty.end(), path) != dirty.end());
    }
}

TEST_CASE("Migration reuses instances with the same values", "[migration]") {
    MigrationFixture f;
    std::string redPath;
    {
        MaterialLibrary lib(f.paths);
        Scene scene;
        std::vector<std::pair<Entity, LegacyMaterialValues>> legacy;
        REQUIRE(SceneSerializer::LoadFromString(
            scene, LegacyScene({LegacyEntity("Crate", kRed, 0.0f, 0.5f)}), nullptr, &legacy));
        const auto result = MigrateLegacyMaterials(scene, legacy, lib);
        REQUIRE(result.created.size() == 1);
        redPath = result.created[0].first;
        REQUIRE(lib.Save(redPath));
    }
    // A later session, another scene, the same red: the saved file is reused.
    MaterialLibrary lib(f.paths);
    Scene scene;
    std::vector<std::pair<Entity, LegacyMaterialValues>> legacy;
    REQUIRE(SceneSerializer::LoadFromString(
        scene, LegacyScene({LegacyEntity("Barrel", kRed, 0.0f, 0.5f), LegacyEntity("Sky", kBlue, 0.0f, 0.5f)}),
        nullptr, &legacy));
    const auto result = MigrateLegacyMaterials(scene, legacy, lib);
    CHECK(result.reused == std::vector<std::string>{redPath});
    CHECK(result.created.size() == 1);
    CHECK(Slots(scene, "Barrel") == std::vector<std::string>{redPath});
}

TEST_CASE("Migration names stay unique and sanitized", "[migration]") {
    MigrationFixture f;
    MaterialLibrary lib(f.paths);
    Scene scene;
    std::vector<std::pair<Entity, LegacyMaterialValues>> legacy;
    REQUIRE(SceneSerializer::LoadFromString(
        scene,
        LegacyScene({LegacyEntity("Box: #1", kRed, 0.0f, 0.5f), LegacyEntity("Box: #1", kBlue, 0.0f, 0.5f)}),
        nullptr, &legacy));
    const auto result = MigrateLegacyMaterials(scene, legacy, lib);
    REQUIRE(result.created.size() == 2);
    CHECK(result.created[0].first == "/Game/Materials/Migrated/MI_Box_1");
    CHECK(result.created[1].first != result.created[0].first);
}

TEST_CASE("Scene round trip keeps material slots", "[migration]") {
    Scene scene;
    const Entity e = scene.CreateEntity("Statue");
    auto& mr = scene.Registry().emplace<MeshRendererComponent>(e);
    mr.materials = {"/Game/Materials/M_Stone", "", "/Game/Materials/MI_Gold"};

    const std::string json = SceneSerializer::SaveToString(scene);
    const auto doc = Luma::ParseJson(json);
    REQUIRE(doc.has_value());
    CHECK(doc->Find("version")->AsInt() == SceneSerializer::kVersion);
    CHECK(SceneSerializer::kVersion == 3);
    Scene loaded;
    std::vector<std::pair<Entity, LegacyMaterialValues>> legacy;
    REQUIRE(SceneSerializer::LoadFromString(loaded, json, nullptr, &legacy));
    CHECK(legacy.empty());
    CHECK(Slots(loaded, "Statue") == mr.materials);
}
