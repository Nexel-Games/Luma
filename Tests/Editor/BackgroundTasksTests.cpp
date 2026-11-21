// Background Tasks UI: status-bar indicator + expandable task panel driven by
// the Task Manager.

#include <catch2/catch_test_macros.hpp>

#include "Luma/Editor/Panels/BackgroundTasks.h"
#include "Luma/Tasks/TaskManager.h"
#include "../Slate/SlateTestHarness.h"

using Luma::Editor::Panels::BackgroundTasksPanel;
using Luma::Slate::Rect;
using Luma::Slate::Test::Harness;
using namespace Luma::Tasks;

namespace {
const Rect kStatusBar{0, 576, 800, 24};

void Center(Harness& h, const Rect& r, BackgroundTasksPanel& p, TaskManager& tm) {
    h.Click(r.x + r.w * 0.5f, r.y + r.h * 0.5f, [&] {
        p.DrawStatusIndicator(h.ctx, kStatusBar, tm);
        p.DrawPanel(h.ctx, tm);
    });
}
}  // namespace

TEST_CASE("The indicator is hidden when nothing runs or failed", "[editor][tasks]") {
    Harness h;
    TaskManager tm;
    BackgroundTasksPanel p;
    h.Frame([&] { p.DrawStatusIndicator(h.ctx, kStatusBar, tm); });
    CHECK(p.IndicatorRect().w == 0.0f);
}

TEST_CASE("Clicking the indicator opens and closes the task panel", "[editor][tasks]") {
    Harness h;
    TaskManager tm;
    TaskToken t = tm.Begin({.title = "Compiling Shaders", .unit = "shaders", .total = 200});
    BackgroundTasksPanel p;
    h.Frame([&] { p.DrawStatusIndicator(h.ctx, kStatusBar, tm); });
    REQUIRE(p.IndicatorRect().w > 0.0f);
    CHECK(p.IndicatorText() == "1 Running");
    Center(h, p.IndicatorRect(), p, tm);
    CHECK(p.IsOpen());
    REQUIRE(p.RowCount() == 1);
    Center(h, p.IndicatorRect(), p, tm);
    CHECK_FALSE(p.IsOpen());
}

TEST_CASE("The cancel button requests cancellation of a cancellable task", "[editor][tasks]") {
    Harness h;
    TaskManager tm;
    TaskToken t = tm.Begin({.title = "Importing Textures", .unit = "textures", .total = 40,
                            .cancellable = true});
    BackgroundTasksPanel p;
    p.Open();
    h.Frame([&] {
        p.DrawStatusIndicator(h.ctx, kStatusBar, tm);
        p.DrawPanel(h.ctx, tm);
    });
    const Rect cancel = p.ActionRect(t.Id());
    REQUIRE(cancel.w > 0.0f);
    Center(h, cancel, p, tm);
    CHECK(t.CancelRequested());
}

TEST_CASE("A failed task shows in the indicator and can be dismissed", "[editor][tasks]") {
    Harness h;
    TaskManager tm;
    TaskToken t = tm.Begin({.title = "Import robot.fbx"});
    const auto id = t.Id();
    t.Fail("unsupported FBX version");
    BackgroundTasksPanel p;
    h.Frame([&] { p.DrawStatusIndicator(h.ctx, kStatusBar, tm); });
    CHECK(p.IndicatorText() == "1 Failed");
    p.Open();
    h.Frame([&] {
        p.DrawStatusIndicator(h.ctx, kStatusBar, tm);
        p.DrawPanel(h.ctx, tm);
    });
    const Rect dismiss = p.ActionRect(id);
    REQUIRE(dismiss.w > 0.0f);
    Center(h, dismiss, p, tm);
    CHECK(tm.Snapshot().empty());
}

TEST_CASE("Escape and outside clicks close the panel", "[editor][tasks]") {
    Harness h;
    TaskManager tm;
    TaskToken t = tm.Begin({.title = "Work"});
    BackgroundTasksPanel p;
    p.Open();
    auto draw = [&] {
        p.DrawStatusIndicator(h.ctx, kStatusBar, tm);
        p.DrawPanel(h.ctx, tm);
    };
    h.Frame(draw);
    h.ctx.OnKey(256, true);
    h.ctx.OnKey(256, false);
    h.Frame(draw);
    CHECK_FALSE(p.IsOpen());
    p.Open();
    h.Frame(draw);
    h.Click(10.0f, 10.0f, draw);  // far from the panel and the indicator
    CHECK_FALSE(p.IsOpen());
}

TEST_CASE("Sub-tasks are listed under their parent, not as rows", "[editor][tasks]") {
    Harness h;
    TaskManager tm;
    TaskToken parent = tm.Begin({.title = "Importing", .unit = "files", .total = 3});
    TaskToken child = tm.Begin({.title = "robot.glb", .parent = parent.Id()});
    BackgroundTasksPanel p;
    p.Open();
    h.Frame([&] {
        p.DrawStatusIndicator(h.ctx, kStatusBar, tm);
        p.DrawPanel(h.ctx, tm);
    });
    CHECK(p.RowCount() == 1);
    CHECK(p.IndicatorText() == "1 Running");
}
