#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "../Slate/SlateTestHarness.h"
#include "Luma/Asset/AssetPath.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/MaterialEditors/MaterialGraphEditor.h"

namespace fs = std::filesystem;
using namespace Luma;
using GraphEditor::PinDir;
using GraphEditor::PinKey;

namespace {
struct Fixture {
    fs::path root = fs::temp_directory_path() / "luma_material_graph_editor_test";
    AssetPathResolver paths;
    std::unique_ptr<Material::MaterialLibrary> lib;
    Fixture() {
        fs::remove_all(root);
        fs::create_directories(root / "Assets" / "Materials");
        paths.SetGameRoot(root / "Assets");
        paths.SetEngineRoot(fs::path(LUMA_SOURCE_DIR) / "Content" / "Engine");
        lib = std::make_unique<Material::MaterialLibrary>(paths);
        Material::GraphMaterial m;
        m.name = "M_Test";
        lib->SetGraphMaterial("/Game/Materials/M_Test", m);
        REQUIRE(lib->SaveGraphAsset("/Game/Materials/M_Test"));
    }
    ~Fixture() {
        lib.reset();
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};
}  // namespace

TEST_CASE("Graph edits stay local until Apply", "[materialeditors][grapheditor-panel]") {
    Fixture f;
    MaterialEditors::MaterialGraphEditor editor("/Game/Materials/M_Test", *f.lib, {});
    REQUIRE(editor.Valid());
    CHECK(editor.Title() == "M_Test");
    const auto output = editor.Model().Material().graph.OutputNode();
    const auto param = editor.Controller().SpawnAction("ScalarParameter", {-300, 0});
    REQUIRE(editor.Controller().TryLink({param, PinDir::Output, 0}, {output, PinDir::Input, 3}).Allowed());
    CHECK(editor.HasUnappliedChanges());
    CHECK(editor.Title() == "M_Test*");
    CHECK(editor.Stats().ok);
    CHECK(editor.Stats().parameters == 1);
    CHECK(f.lib->GetGraphMaterial("/Game/Materials/M_Test")->graph.Nodes().size() == 1);

    editor.Apply();
    CHECK_FALSE(editor.HasUnappliedChanges());
    CHECK(f.lib->GetGraphMaterial("/Game/Materials/M_Test")->graph.Nodes().size() == 2);
    std::string err;
    CHECK(editor.Save(&err));
    CHECK_FALSE(editor.IsDirty());
}

TEST_CASE("Live update applies every change; errors land on nodes", "[materialeditors][grapheditor-panel]") {
    Fixture f;
    MaterialEditors::MaterialGraphEditor editor("/Game/Materials/M_Test", *f.lib, {});
    editor.liveUpdate = true;
    const auto output = editor.Model().Material().graph.OutputNode();
    // Texture Sample with no texture into Base Color compiles; an unknown
    // collection parameter does not.
    const auto bad = editor.Controller().SpawnAction("CollectionParameter", {-300, 0});
    REQUIRE(bad != 0);
    editor.Controller().TryLink({bad, PinDir::Output, 0}, {output, PinDir::Input, 3});
    CHECK_FALSE(editor.HasUnappliedChanges());
    CHECK_FALSE(editor.Stats().ok);
    CHECK_FALSE(editor.Stats().errors.empty());
    bool flagged = false;
    for (const auto& n : editor.Model().Nodes())
        if (n.id == bad) flagged = !n.error.empty();
    CHECK(flagged);
}

TEST_CASE("Material editor draws its UE layout", "[materialeditors][grapheditor-panel]") {
    Fixture f;
    MaterialEditors::MaterialEditorServices s;
    MaterialEditors::PreviewRequest last;
    s.renderPreview = [&](const MaterialEditors::PreviewRequest& r) {
        last = r;
        return TextureHandle{3};
    };
    MaterialEditors::MaterialGraphEditor editor("/Game/Materials/M_Test", *f.lib, s);
    Slate::Test::Harness h;
    for (int i = 0; i < 3; ++i) h.Frame([&] { editor.Draw(h.ctx, {0, 0, 1280, 800}); });
    CHECK(last.materialPath == "/Game/Materials/M_Test");
    CHECK(last.width > 200);
    CHECK(last.width < 400);  // left column
}
