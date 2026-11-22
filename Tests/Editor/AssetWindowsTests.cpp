#include <catch2/catch_test_macros.hpp>

#include <string>

#include "../Slate/SlateTestHarness.h"
#include "Luma/Editor/AssetWindows.h"

using namespace Luma;
using Editor::AssetWindowDesc;
using Editor::AssetWindowManager;
using Editor::AssetWindowState;

namespace {
const Slate::Rect kWork{0, 60, 1000, 700};

AssetWindowDesc Desc(const std::string& id, bool* closed = nullptr, bool allowClose = true) {
    AssetWindowDesc d;
    d.id = id;
    d.title = id;
    d.draw = [](Slate::Context&, const Slate::Rect&) {};
    d.requestClose = [allowClose] { return allowClose; };
    d.onClosed = [closed] {
        if (closed) *closed = true;
    };
    return d;
}
}  // namespace

TEST_CASE("Asset windows open floating, centred, and refocus instead of duplicating", "[editor][windows]") {
    AssetWindowManager m;
    REQUIRE(m.Open(Desc("a"), kWork));
    CHECK(m.State("a") == AssetWindowState::Floating);
    const Slate::Rect r = m.FloatRect("a");
    CHECK(r.w == 800.0f);
    CHECK(r.x == 100.0f);
    CHECK(r.y > kWork.y);
    REQUIRE(m.Open(Desc("b"), kWork));
    CHECK(m.Ids().back() == "b");
    CHECK_FALSE(m.Open(Desc("a"), kWork));  // already open: focused
    CHECK(m.Ids().back() == "a");
    CHECK(m.HomeActive());
}

TEST_CASE("Docking puts a window in the row and selects it; Home goes back", "[editor][windows]") {
    AssetWindowManager m;
    m.Open(Desc("a"), kWork);
    m.Dock("a");
    CHECK(m.State("a") == AssetWindowState::Docked);
    CHECK(m.RowTabs() == std::vector<std::string>{"a"});
    CHECK(m.ActiveTab() == "a");
    m.ShowHome();
    CHECK(m.HomeActive());
    m.Focus("a");
    CHECK(m.ActiveTab() == "a");
    m.Float("a");
    CHECK(m.State("a") == AssetWindowState::Floating);
    CHECK(m.RowTabs().empty());
    CHECK(m.HomeActive());
}

TEST_CASE("Minimize parks a window in the row; focus restores it", "[editor][windows]") {
    AssetWindowManager m;
    m.Open(Desc("a"), kWork);
    m.Minimize("a");
    CHECK(m.State("a") == AssetWindowState::Minimized);
    CHECK(m.RowTabs() == std::vector<std::string>{"a"});
    CHECK(m.HomeActive());
    m.Focus("a");
    CHECK(m.State("a") == AssetWindowState::Floating);
    CHECK(m.RowTabs().empty());

    // A docked window minimized comes back docked.
    m.Dock("a");
    m.Minimize("a");
    CHECK(m.HomeActive());
    m.Focus("a");
    CHECK(m.State("a") == AssetWindowState::Docked);
    CHECK(m.ActiveTab() == "a");
}

TEST_CASE("Maximize fills the work area and toggles back", "[editor][windows]") {
    AssetWindowManager m;
    m.Open(Desc("a"), kWork);
    const Slate::Rect before = m.FloatRect("a");
    m.ToggleMaximize("a", kWork);
    CHECK(m.Maximized("a"));
    CHECK(m.FloatRect("a").w == kWork.w);
    CHECK(m.FloatRect("a").h == kWork.h);
    m.ToggleMaximize("a", kWork);
    CHECK_FALSE(m.Maximized("a"));
    CHECK(m.FloatRect("a").w == before.w);
}

TEST_CASE("Close asks the window first", "[editor][windows]") {
    AssetWindowManager m;
    bool closedA = false, closedB = false;
    m.Open(Desc("a", &closedA), kWork);
    m.Open(Desc("b", &closedB, /*allowClose=*/false), kWork);
    m.Dock("a");
    m.RequestClose("a");
    CHECK_FALSE(m.Has("a"));
    CHECK(closedA);
    CHECK(m.HomeActive());
    m.RequestClose("b");  // e.g. unsaved changes: a prompt keeps it
    CHECK(m.Has("b"));
    CHECK_FALSE(closedB);
    m.Close("b");
    CHECK_FALSE(m.Has("b"));
    CHECK(closedB);
}

TEST_CASE("Dragging a window's title bar onto the row docks it", "[editor][windows]") {
    AssetWindowManager m;
    m.Open(Desc("a"), kWork);
    Slate::Test::Harness h;
    const Slate::Rect row{0, 32, 800, AssetWindowManager::kRowH};
    auto frame = [&] {
        m.DrawRow(h.ctx, row);
        m.DrawFloating(h.ctx, {0, 60, 800, 540});
    };
    h.Frame(frame);
    const Slate::Rect r = m.FloatRect("a");
    // Press on the title bar (left part, away from the buttons), drag up onto the row.
    const f32 x = r.x + 40.0f, y = r.y + AssetWindowManager::kTitleH * 0.5f;
    h.Drag(x, y, 0.0f, (row.y + row.h * 0.5f) - y, frame);
    CHECK(m.State("a") == AssetWindowState::Docked);
    CHECK(m.ActiveTab() == "a");
}
