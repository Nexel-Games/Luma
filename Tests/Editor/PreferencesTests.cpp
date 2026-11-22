#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>

#include "Luma/Editor/Panels/Preferences.h"
#include "../Slate/SlateTestHarness.h"

using namespace Luma;
using namespace Luma::Editor::Panels;

namespace {
struct Profile {
    std::filesystem::path file = std::filesystem::temp_directory_path() /
        ("luma_preferences_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ini");
    ~Profile() {
        std::error_code ec;
        std::filesystem::remove(file, ec);
    }
};
}

TEST_CASE("Preferences validate saved values and merge edits from two windows", "[editor][preferences]") {
    Profile profile;
    std::ofstream(profile.file) << "[Editor]\nStats=1\nCameraSpeed=8oops\nFieldOfView=nan\nScaleStep=-2\n";
    ViewportSettings a, b;
    GizmoSnap sa, sb;
    PreferencesPanel first(a, sa, profile.file), second(b, sb, profile.file);
    CHECK(a.showStats);
    CHECK(a.cameraSpeed == ViewportSettings{}.cameraSpeed);
    CHECK(a.fovYRadians == ViewportSettings{}.fovYRadians);
    CHECK(sa.scaleStep == GizmoSnap{}.scaleStep);
    a.cameraSpeed = 7;
    a.fovYRadians = Math::Radians(75.0f);
    first.SaveChanges();
    sb.translate = false;
    sb.scaleStep = 0.125f;
    second.SaveChanges();
    ViewportSettings after;
    GizmoSnap snap;
    PreferencesPanel reopened(after, snap, profile.file);
    CHECK(after.cameraSpeed == a.cameraSpeed);
    CHECK(after.fovYRadians == a.fovYRadians);
    CHECK(after.showStats);
    CHECK_FALSE(snap.translate);
    CHECK(snap.scaleStep == sb.scaleStep);
}

TEST_CASE("Preferences search across categories edits the live gizmo", "[editor][preferences][slate]") {
    Profile profile;
    ViewportSettings vp;
    GizmoSnap snap;
    PreferencesPanel panel(vp, snap, profile.file);
    Slate::Test::Harness h;
    auto draw = [&] { REQUIRE(panel.Draw(h.ctx, 800, 600)); };
    h.Frame(draw);
    // At 800px the first property checkbox is at (455, 205).
    h.Click(463, 214, draw);
    CHECK_FALSE(vp.showGrid);
    h.Click(380, 66, draw);
    for (char c : std::string("ENABLE ROTATION")) h.ctx.OnText(static_cast<u32>(c));
    h.Frame(draw);
    h.Click(463, 214, draw);
    CHECK_FALSE(snap.rotate);
    CHECK(snap.translate);
    h.Click(69, 144, draw);  // Navigation clears the filter.
    h.Click(69, 114, draw);  // Viewports
    h.Click(463, 214, draw);
    CHECK(vp.showGrid);
    h.ctx.OnKey(256, true);
    h.Frame([&] { CHECK_FALSE(panel.Draw(h.ctx, 800, 600)); });
}

TEST_CASE("The preferences header draws the editor icon texture", "[editor][preferences][slate]") {
    Profile profile;
    ViewportSettings vp;
    GizmoSnap snap;
    PreferencesPanel panel(vp, snap, profile.file);
    Slate::Test::Harness h;
    h.ctx.BeginFrame(800, 600, 0.016f);
    h.ctx.LogoMark({20, 19}, 12);
    CHECK(h.ctx.drawList().vertexCount() == 0);
    const auto icon = Slate::LoadImage(h.renderer, std::string(LUMA_SOURCE_DIR) + "/Content/Editor/luma_icon.png");
    REQUIRE(icon.Valid());
    h.ctx.SetBrandIcon(icon);
    REQUIRE(panel.Draw(h.ctx, 800, 600));
    const auto& data = h.ctx.EndFrame();
    const auto* end = data.commands + data.commandCount;
    const auto* mark = std::find_if(data.commands, end, [&](const UIDrawCommand& cmd) {
        return cmd.texture == icon.texture;
    });
    REQUIRE(mark != end);
    CHECK(mark->indexCount == 6);
}

TEST_CASE("Escape clears a compact search before leaving preferences", "[editor][preferences][slate]") {
    Profile profile;
    ViewportSettings vp;
    GizmoSnap snap;
    PreferencesPanel panel(vp, snap, profile.file);
    Slate::Test::Harness h;
    auto draw = [&] { REQUIRE(panel.Draw(h.ctx, 480, 360)); };
    h.Click(150, 100, draw);
    h.ctx.OnText('z');
    h.Frame(draw);
    h.ctx.OnKey(256, true);
    h.Frame(draw);
    h.ctx.OnKey(256, false);
    h.ctx.OnKey(256, true);
    h.Frame([&] { CHECK_FALSE(panel.Draw(h.ctx, 480, 360)); });
}
