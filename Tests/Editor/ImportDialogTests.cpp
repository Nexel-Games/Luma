#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "Luma/Asset/Import/ImportSession.h"
#include "Luma/Editor/Panels/ImportDialog.h"
#include "Luma/Slate/Types.h"
#include "../Slate/SlateTestHarness.h"

using Catch::Matchers::WithinAbs;
using Luma::f32;
using Luma::Import::ImportNode;
using Luma::Import::ImportSession;
using Luma::Import::NodeKind;
using Luma::Editor::Panels::ImportDialog;
using Luma::Slate::Test::Harness;

namespace {

std::filesystem::path Fixture(const char* name) {
    return std::filesystem::path(LUMA_SOURCE_DIR) / "Tests" / "Fixtures" / "Import" / name;
}

ImportNode* FindNode(ImportSession& session, NodeKind kind, const char* name) {
    for (ImportNode& node : session.Scene().nodes) {
        if (node.kind == kind && node.name == name) return &node;
    }
    return nullptr;
}

// Matches ImportDialog's layout at 1400x900 (modal 960x620, unclamped).
struct Hit {
    f32 x;
    f32 y;
};

Hit TreeCheck(int row, int depth) {
    const f32 contentX = (1400.0f - 960.0f) * 0.5f + 12.0f;
    const f32 contentY = (900.0f - 620.0f) * 0.5f + 32.0f + 12.0f;
    const f32 rowY = contentY + 32.0f + static_cast<f32>(row) * 22.0f;
    const f32 checkX = contentX + static_cast<f32>(depth) * 16.0f;
    return {checkX + 7.0f, rowY + 4.0f + 7.0f};
}

Hit UniformScale() {
    const f32 contentX = (1400.0f - 960.0f) * 0.5f + 12.0f;
    const f32 contentY = (900.0f - 620.0f) * 0.5f + 32.0f + 12.0f;
    const f32 leftW = 936.0f * 0.38f;
    const f32 rightX = contentX + leftW + 8.0f;
    const f32 bodyY = contentY + 32.0f;
    const f32 fieldY = bodyY + 24.0f + 6.0f * 26.0f + 24.0f + 2.0f * 26.0f;
    const f32 fieldX = rightX + 160.0f + 8.0f;
    return {fieldX + 40.0f, fieldY + 2.0f + 11.0f};
}

Hit ImportButton() {
    const f32 contentRight = (1400.0f - 960.0f) * 0.5f + 12.0f + 936.0f;
    const f32 contentBottom = (900.0f - 620.0f) * 0.5f + 32.0f + 12.0f + 528.0f;
    const f32 cancelX = contentRight - 84.0f;
    const f32 importX = cancelX - 8.0f - 84.0f;
    return {importX + 42.0f, contentBottom + 12.0f + 13.0f};
}

class DialogHarness {
public:
    Harness ui;
    ImportDialog dialog;

    template <typename Fn>
    void Frame(Fn&& fn) {
        ui.ctx.BeginFrame(1400.0f, 900.0f, 0.0166f);
        fn();
        ui.ctx.EndFrame();
    }
    template <typename Fn>
    void Click(Hit hit, Fn&& fn) {
        ui.ctx.OnMouseMove(hit.x, hit.y);
        ui.ctx.OnMouseButton(0, true);
        Frame(fn);
        ui.ctx.OnMouseButton(0, false);
        Frame(fn);
    }
    template <typename Fn>
    void Drag(Hit hit, f32 dx, Fn&& fn) {
        ui.ctx.OnMouseMove(hit.x, hit.y);
        ui.ctx.OnMouseButton(0, true);
        Frame(fn);
        ui.ctx.OnMouseMove(hit.x + dx, hit.y);
        Frame(fn);
        ui.ctx.OnMouseButton(0, false);
        Frame(fn);
    }
};

}  // namespace

TEST_CASE("Import dialog toggles the tree, edits scale, and saves presets", "[import][dialog]") {
    std::string error;
    ImportSession session;
    REQUIRE(session.Translate({Fixture("robot.gltf")}, &error));
    ImportNode* body = FindNode(session, NodeKind::Mesh, "Body");
    ImportNode* arm = FindNode(session, NodeKind::Mesh, "Arm");
    REQUIRE(body);
    REQUIRE(arm);

    const auto config = std::filesystem::temp_directory_path() / "luma-import-dialog";
    std::filesystem::remove_all(config);
    std::filesystem::create_directories(config);

    DialogHarness h;
    ImportDialog::Action action = ImportDialog::Action::None;
    auto draw = [&] { action = h.dialog.Draw(h.ui.ctx, 1400.0f, 900.0f); };

    h.dialog.Open(&session, config / "Assets", "/Game/Robots", false);
    h.dialog.LoadPresets(config);
    REQUIRE(h.dialog.IsOpen());
    h.Frame(draw);

    h.Click(TreeCheck(1, 1), draw);
    REQUIRE_FALSE(body->enabled);
    REQUIRE_FALSE(arm->enabled);

    h.Click(TreeCheck(1, 1), draw);
    REQUIRE(body->enabled);
    REQUIRE(arm->enabled);

    h.Click(TreeCheck(2, 2), draw);
    if (arm->enabled) {
        h.Click(TreeCheck(2, 2), draw);
        h.Click(TreeCheck(3, 2), draw);
    }
    REQUIRE(body->enabled);
    REQUIRE_FALSE(arm->enabled);

    h.Click(TreeCheck(0, 0), draw);
    REQUIRE(body->enabled);
    REQUIRE(arm->enabled);

    const double before = session.Settings().Meshes().GetFloat("uniformScale");
    h.Drag(UniformScale(), 50.0f, draw);
    REQUIRE_THAT(session.Settings().Meshes().GetFloat("uniformScale"), WithinAbs(before + 1.0, 0.05));

    session.Settings().Meshes().Set("uniformScale", 4.0);
    h.dialog.SavePreset("Wide");
    session.Settings().Meshes().Set("uniformScale", 1.0);
    h.dialog.LoadPresets(config);
    REQUIRE_THAT(session.Settings().Meshes().GetFloat("uniformScale"), WithinAbs(4.0, 1e-4));

    {
        std::ifstream in(config / "ImportPresets.json");
        std::stringstream text;
        text << in.rdbuf();
        REQUIRE(text.str().find("Wide") != std::string::npos);
    }

    h.Click(ImportButton(), draw);
    REQUIRE(action == ImportDialog::Action::Import);
    REQUIRE_FALSE(h.dialog.IsOpen());
    std::filesystem::remove_all(config);
}

TEST_CASE("Escape cancels the import dialog", "[import][dialog]") {
    std::string error;
    ImportSession session;
    REQUIRE(session.Translate({Fixture("robot.gltf")}, &error));
    DialogHarness h;
    h.dialog.Open(&session, "Assets", "/Game", false);
    ImportDialog::Action action = ImportDialog::Action::None;
    auto draw = [&] { action = h.dialog.Draw(h.ui.ctx, 1400.0f, 900.0f); };
    h.Frame(draw);
    h.ui.ctx.OnKey(256, true);
    h.Frame(draw);
    REQUIRE(action == ImportDialog::Action::Cancel);
    REQUIRE_FALSE(h.dialog.IsOpen());
}

TEST_CASE("A sound-only import hides the Meshes category", "[import][dialog]") {
    std::string error;
    ImportSession session;
    REQUIRE(session.Translate({Fixture("tone_mono.wav")}, &error));
    DialogHarness h;
    h.dialog.Open(&session, "Assets", "/Game", false);
    auto draw = [&] { (void)h.dialog.Draw(h.ui.ctx, 1400.0f, 900.0f); };
    h.Frame(draw);
    h.Drag(UniformScale(), 50.0f, draw);
    REQUIRE_THAT(session.Settings().Meshes().GetFloat("uniformScale"), WithinAbs(1.0, 1e-4));
}
