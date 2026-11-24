// The asset Inspector's prefab view: a prefab edited in its own scene saves
// back to the file (object ids kept) and re-reads it when it changes on disk.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "Luma/Editor/Panels/PrefabAssetEditor.h"
#include "Luma/Scene/Components.h"
#include "Luma/Scene/Hierarchy.h"
#include "Luma/Scene/Prefab.h"

using Catch::Approx;
using namespace Luma;
using Luma::Editor::Panels::PrefabAssetEditor;
namespace fs = std::filesystem;

namespace {

const std::string kPath = "/Game/Lamp";

struct PrefabFile {
    fs::path dir;
    fs::path file;
    PrefabFile() {
        dir = fs::temp_directory_path() /
              ("luma_prefab_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(dir);
        file = dir / "Lamp.lprefab";
        Scene s;
        const Entity lamp = s.CreateEntity("Lamp");
        const Entity bulb = s.CreateEntity("Bulb");
        s.Registry().get<TransformComponent>(bulb).position = {0, 2, 0};
        s.Registry().emplace<LightComponent>(bulb).intensity = 5.0f;
        SetParent(s, bulb, lamp);
        std::string err;
        REQUIRE(SavePrefab(CapturePrefab(s, lamp, "Lamp"), file, &err));
    }
    ~PrefabFile() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    PrefabAsset Read() const {
        std::string err;
        auto p = LoadPrefab(file, &err);
        REQUIRE(p);
        return *p;
    }
};

Entity OnlyChild(Scene& s, Entity root) {
    const auto children = ChildrenOf(s, root);
    REQUIRE(children.size() == 1);
    return children[0];
}

}  // namespace

TEST_CASE("Prefab view edits save to the prefab file", "[prefab][editor]") {
    PrefabFile f;
    PrefabAssetEditor editor;
    REQUIRE(editor.Open(f.file, kPath));
    CHECK_FALSE(editor.Dirty());
    std::string err;
    CHECK_FALSE(editor.Commit(&err));  // nothing changed: nothing written

    const Entity bulb = OnlyChild(editor.EditScene(), editor.Root());
    editor.EditScene().Registry().get<LightComponent>(bulb).intensity = 12.0f;
    CHECK(editor.Dirty());
    REQUIRE(editor.Commit(&err));
    CHECK_FALSE(editor.Dirty());

    const PrefabAsset saved = f.Read();
    REQUIRE(saved.objects.size() == 2);
    CHECK(saved.objects[1].components.Find("Light")->Find("intensity")->AsFloat() == Approx(12.0f));
    // The child keeps its offset from the root.
    CHECK(saved.objects[1].components.Find("Transform")->Find("position")->Elements()[1].AsFloat() ==
          Approx(2.0f));
}

TEST_CASE("Objects added in the prefab view keep their ids across saves", "[prefab][editor]") {
    PrefabFile f;
    PrefabAssetEditor editor;
    REQUIRE(editor.Open(f.file, kPath));
    std::string err;
    const Entity added = editor.AddObject(editor.Root());
    REQUIRE(editor.EditScene().IsValid(added));
    CHECK(editor.Picked() == added);
    REQUIRE(editor.Commit(&err));
    const u64 addedId = f.Read().objects.back().id;
    CHECK(addedId == 3);

    // Removing the bulb (id 2) leaves the added object's id alone.
    for (Entity c : ChildrenOf(editor.EditScene(), editor.Root())) {
        if (editor.EditScene().Registry().get<NameComponent>(c).name == "Bulb") editor.RemoveObject(c);
    }
    REQUIRE(editor.Commit(&err));
    const PrefabAsset saved = f.Read();
    REQUIRE(saved.objects.size() == 2);
    CHECK(saved.objects[1].id == addedId);
    CHECK(NextPrefabObjectId(saved) == 4);

    // The root can't be removed.
    editor.RemoveObject(editor.Root());
    CHECK(editor.EditScene().IsValid(editor.Root()));
}

TEST_CASE("The prefab view re-reads a prefab changed on disk", "[prefab][editor]") {
    PrefabFile f;
    PrefabAssetEditor editor;
    REQUIRE(editor.Open(f.file, kPath));
    (void)editor.TakeReloaded();
    REQUIRE(editor.Open(f.file, kPath));
    CHECK_FALSE(editor.TakeReloaded());  // unchanged: the same edit scene

    PrefabAsset changed = f.Read();
    changed.objects[1].components["Light"]["intensity"] = 30.0;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::string err;
    REQUIRE(SavePrefab(changed, f.file, &err));
    fs::last_write_time(f.file, fs::last_write_time(f.file) + std::chrono::seconds(2));
    REQUIRE(editor.Open(f.file, kPath));
    CHECK(editor.TakeReloaded());
    const Entity bulb = OnlyChild(editor.EditScene(), editor.Root());
    CHECK(editor.EditScene().Registry().get<LightComponent>(bulb).intensity == Approx(30.0f));
}

TEST_CASE("An unreadable prefab reports why", "[prefab][editor]") {
    PrefabFile f;
    { std::ofstream(f.file) << "{ not json"; }
    PrefabAssetEditor editor;
    CHECK_FALSE(editor.Open(f.file, kPath));
    CHECK_FALSE(editor.Error().empty());
    std::string err;
    CHECK_FALSE(editor.Commit(&err));
}
