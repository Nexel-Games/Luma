// Luma Task Manager: user-visible background operations (progress, status,
// cancellation, failure, completion) reported from any thread.

#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <vector>

#include "Luma/Tasks/TaskManager.h"

using namespace Luma;
using namespace Luma::Tasks;

TEST_CASE("A task reports progress as counts and fraction", "[tasks]") {
    TaskManager tm;
    TaskToken t = tm.Begin({.title = "Compiling Shaders", .unit = "shaders", .total = 200});
    t.Advance(144);
    t.SetStatus("M_Lava");
    auto snap = tm.Snapshot();
    REQUIRE(snap.size() == 1);
    CHECK(snap[0].title == "Compiling Shaders");
    CHECK(snap[0].done == 144);
    CHECK(snap[0].total == 200);
    CHECK(snap[0].Fraction() == 0.72f);
    CHECK(snap[0].status == "M_Lava");
    CHECK(snap[0].CountText() == "144 / 200 shaders");
    CHECK(snap[0].state == TaskState::Running);
    CHECK(tm.RunningCount() == 1);
}

TEST_CASE("A task without a total is indeterminate unless a fraction is set", "[tasks]") {
    TaskManager tm;
    TaskToken t = tm.Begin({.title = "Initializing Rendering Pipeline"});
    CHECK(tm.Snapshot()[0].Indeterminate());
    t.SetFraction(0.9f);
    CHECK_FALSE(tm.Snapshot()[0].Indeterminate());
    CHECK(tm.Snapshot()[0].Fraction() == 0.9f);
}

TEST_CASE("Completion, failure and cancellation end a task", "[tasks]") {
    TaskManager tm;
    TaskToken a = tm.Begin({.title = "A"});
    TaskToken b = tm.Begin({.title = "B"});
    TaskToken c = tm.Begin({.title = "C", .cancellable = true});
    a.Complete();
    b.Fail("disk full");
    CHECK(tm.RequestCancel(c.Id()));
    CHECK(c.CancelRequested());
    c.Complete();  // the worker noticed the request and stopped
    auto snap = tm.Snapshot();
    CHECK(snap[0].state == TaskState::Completed);
    CHECK(snap[1].state == TaskState::Failed);
    CHECK(snap[1].error == "disk full");
    CHECK(snap[2].state == TaskState::Cancelled);
    CHECK(tm.RunningCount() == 0);
}

TEST_CASE("Non-cancellable tasks refuse cancel requests", "[tasks]") {
    TaskManager tm;
    TaskToken t = tm.Begin({.title = "Saving", .cancellable = false});
    CHECK_FALSE(tm.RequestCancel(t.Id()));
    CHECK_FALSE(t.CancelRequested());
}

TEST_CASE("A token that goes out of scope unfinished completes its task", "[tasks]") {
    TaskManager tm;
    {
        TaskToken t = tm.Begin({.title = "Scoped"});
    }
    CHECK(tm.Snapshot()[0].state == TaskState::Completed);
}

TEST_CASE("Finished tasks linger, then are pruned", "[tasks]") {
    TaskManager tm;
    tm.SetLinger(2.0);
    TaskToken t = tm.Begin({.title = "Short"});
    t.Complete();
    tm.Update(1.0);
    CHECK(tm.Snapshot().size() == 1);
    tm.Update(1.5);
    CHECK(tm.Snapshot().empty());
}

TEST_CASE("Failed tasks stay until dismissed", "[tasks]") {
    TaskManager tm;
    tm.SetLinger(1.0);
    TaskToken t = tm.Begin({.title = "Import"});
    t.Fail("bad file");
    tm.Update(10.0);
    REQUIRE(tm.Snapshot().size() == 1);
    tm.Dismiss(tm.Snapshot()[0].id);
    CHECK(tm.Snapshot().empty());
}

TEST_CASE("Child tasks roll up into their parent", "[tasks]") {
    TaskManager tm;
    TaskToken parent = tm.Begin({.title = "Importing", .unit = "files", .total = 2});
    TaskToken child = tm.Begin({.title = "robot.glb", .parent = parent.Id()});
    auto snap = tm.Snapshot();
    REQUIRE(snap.size() == 2);
    CHECK(snap[1].parent == parent.Id());
    CHECK(tm.RunningCount() == 1);  // top-level operations only
}

TEST_CASE("Tokens are safe to update from many threads", "[tasks]") {
    TaskManager tm;
    TaskToken t = tm.Begin({.title = "Parallel", .total = 8000});
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&t] {
            for (int k = 0; k < 1000; ++k) t.Advance(1);
        });
    }
    for (auto& th : threads) th.join();
    CHECK(tm.Snapshot()[0].done == 8000);
}

TEST_CASE("Aggregate progress averages running determinate tasks", "[tasks]") {
    TaskManager tm;
    TaskToken a = tm.Begin({.title = "A", .total = 10});
    TaskToken b = tm.Begin({.title = "B", .total = 10});
    a.Advance(10);
    b.Advance(5);
    CHECK(tm.AggregateFraction() == 0.75f);
}
