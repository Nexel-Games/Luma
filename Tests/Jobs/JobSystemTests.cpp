// Luma Job System: worker threads, priorities, dependencies, ParallelFor,
// helping waits, main-thread jobs, failure isolation.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "Luma/Jobs/JobSystem.h"

using namespace Luma;
using namespace Luma::Jobs;

TEST_CASE("Scheduled jobs run on worker threads and complete", "[jobs]") {
    JobSystem js(4);
    CHECK(js.WorkerCount() == 4);
    std::atomic<int> sum{0};
    std::vector<JobHandle> handles;
    for (int i = 1; i <= 100; ++i) {
        handles.push_back(js.Schedule({.name = "add"}, [&sum, i] { sum += i; }));
    }
    for (auto& h : handles) js.Wait(h);
    CHECK(sum == 5050);
    for (auto& h : handles) CHECK(h.IsDone());
}

TEST_CASE("A job runs only after its dependencies", "[jobs]") {
    JobSystem js(4);
    std::mutex m;
    std::vector<int> order;
    auto push = [&](int v) { std::lock_guard lock(m); order.push_back(v); };
    JobHandle a = js.Schedule({.name = "a"}, [&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        push(1);
    });
    JobHandle b = js.Schedule({.name = "b"}, [&] { push(2); });
    JobHandle c = js.Schedule({.name = "c", .dependencies = {a, b}}, [&] { push(3); });
    js.Wait(c);
    REQUIRE(order.size() == 3);
    CHECK(order.back() == 3);
}

TEST_CASE("A dependency that already finished doesn't block", "[jobs]") {
    JobSystem js(2);
    JobHandle a = js.Schedule({.name = "a"}, [] {});
    js.Wait(a);
    std::atomic<bool> ran{false};
    JobHandle b = js.Schedule({.name = "b", .dependencies = {a}}, [&] { ran = true; });
    js.Wait(b);
    CHECK(ran);
}

TEST_CASE("ParallelFor covers every index exactly once", "[jobs]") {
    JobSystem js(4);
    std::vector<std::atomic<int>> hits(10007);
    JobHandle h = js.ParallelFor("fill", hits.size(), 64, [&](usize begin, usize end) {
        for (usize i = begin; i < end; ++i) ++hits[i];
    });
    js.Wait(h);
    bool allOnce = true;
    for (auto& v : hits) allOnce = allOnce && v == 1;
    CHECK(allOnce);
}

TEST_CASE("Waiting inside a job helps instead of deadlocking", "[jobs]") {
    JobSystem js(1);  // a single worker: a blocking wait would deadlock
    std::atomic<int> inner{0};
    JobHandle outer = js.Schedule({.name = "outer"}, [&] {
        JobHandle child = js.Schedule({.name = "child"}, [&] { inner = 7; });
        js.Wait(child);
    });
    js.Wait(outer);
    CHECK(inner == 7);
}

TEST_CASE("Higher priority jobs are picked first", "[jobs]") {
    JobSystem js(1);
    std::mutex m;
    std::vector<int> order;
    // Block the single worker so the queue fills up before anything runs.
    std::atomic<bool> release{false};
    JobHandle gate = js.Schedule({.name = "gate"}, [&] {
        while (!release) std::this_thread::yield();
    });
    std::vector<JobHandle> hs;
    hs.push_back(js.Schedule({.name = "bg", .priority = Priority::Background},
                             [&] { std::lock_guard l(m); order.push_back(3); }));
    hs.push_back(js.Schedule({.name = "low", .priority = Priority::Low},
                             [&] { std::lock_guard l(m); order.push_back(2); }));
    hs.push_back(js.Schedule({.name = "high", .priority = Priority::High},
                             [&] { std::lock_guard l(m); order.push_back(0); }));
    hs.push_back(js.Schedule({.name = "normal", .priority = Priority::Normal},
                             [&] { std::lock_guard l(m); order.push_back(1); }));
    release = true;
    // Spin without helping, so only the single worker executes (in priority order).
    while (!gate.IsDone() || !hs.back().IsDone() || !hs[0].IsDone() || !hs[1].IsDone() ||
           !hs[2].IsDone()) {
        std::this_thread::yield();
    }
    CHECK(order == std::vector<int>{0, 1, 2, 3});
}

TEST_CASE("Main-thread jobs run only when the main thread pumps them", "[jobs]") {
    JobSystem js(2);
    const auto mainId = std::this_thread::get_id();
    std::atomic<bool> onMain{false};
    JobHandle work = js.Schedule({.name = "work"}, [] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    });
    JobHandle upload = js.Schedule({.name = "upload", .dependencies = {work}, .mainThread = true},
                                   [&] { onMain = std::this_thread::get_id() == mainId; });
    js.Wait(work);
    CHECK_FALSE(upload.IsDone());
    u32 ran = 0;
    for (int i = 0; i < 100 && !upload.IsDone(); ++i) ran += js.RunMainThreadJobs();
    CHECK(ran == 1);
    CHECK(onMain);
}

TEST_CASE("A throwing job fails without killing the worker", "[jobs]") {
    JobSystem js(1);
    JobHandle bad = js.Schedule({.name = "bad"}, [] { throw std::runtime_error("boom"); });
    js.Wait(bad);
    CHECK(bad.IsDone());
    CHECK(bad.Failed());
    CHECK(bad.Error().find("boom") != std::string::npos);
    std::atomic<bool> ran{false};
    JobHandle good = js.Schedule({.name = "good"}, [&] { ran = true; });
    js.Wait(good);
    CHECK(ran);
}

TEST_CASE("Dependents of a failed job still run and can see the failure", "[jobs]") {
    JobSystem js(2);
    JobHandle bad = js.Schedule({.name = "bad"}, [] { throw std::runtime_error("x"); });
    std::atomic<bool> sawFailure{false};
    JobHandle next = js.Schedule({.name = "next", .dependencies = {bad}},
                                 [&] { sawFailure = bad.Failed(); });
    js.Wait(next);
    CHECK(sawFailure);
}

TEST_CASE("Destruction drains queued jobs", "[jobs]") {
    std::atomic<int> count{0};
    {
        JobSystem js(2);
        for (int i = 0; i < 50; ++i) {
            js.Schedule({.name = "n", .priority = Priority::Background}, [&] { ++count; });
        }
    }
    CHECK(count == 50);
}
