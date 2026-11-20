#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <vector>

#include "Luma/Core/LogBuffer.h"

using namespace Luma;

namespace {
LogRecord Rec(LogLevel level, std::string_view cat, std::string_view msg) {
    return LogRecord{level, cat, msg, std::this_thread::get_id(),
                     std::chrono::system_clock::now()};
}
}  // namespace

TEST_CASE("LogBuffer keeps owned copies and fetches incrementally", "[log][buffer]") {
    LogBuffer buf(16);
    {
        std::string transient = "first message";
        buf.Write(Rec(LogLevel::Info, "Editor", transient));
        transient = "overwritten";  // the buffer must own its copy
    }
    buf.Write(Rec(LogLevel::Error, "Vulkan", "second"));

    std::vector<LogEntry> out;
    u64 seq = buf.Fetch(0, out);
    REQUIRE(out.size() == 2);
    CHECK(out[0].message == "first message");
    CHECK(out[0].category == "Editor");
    CHECK(out[1].level == LogLevel::Error);
    CHECK(seq == out[1].sequence);

    // Nothing new -> nothing appended, sequence unchanged.
    out.clear();
    CHECK(buf.Fetch(seq, out) == seq);
    CHECK(out.empty());

    buf.Write(Rec(LogLevel::Warning, "Asset", "third"));
    seq = buf.Fetch(seq, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].message == "third");
}

TEST_CASE("LogBuffer evicts the oldest entries past capacity", "[log][buffer]") {
    LogBuffer buf(3);
    for (int i = 0; i < 5; ++i) {
        std::string m = "m" + std::to_string(i);
        buf.Write(Rec(LogLevel::Info, "T", m));
    }
    CHECK(buf.Size() == 3);
    std::vector<LogEntry> out;
    buf.Fetch(0, out);
    REQUIRE(out.size() == 3);
    CHECK(out.front().message == "m2");
    CHECK(out.back().message == "m4");
    CHECK(buf.LatestSequence() == 5);
}

TEST_CASE("LogBuffer Clear drops entries but sequences keep increasing", "[log][buffer]") {
    LogBuffer buf(8);
    buf.Write(Rec(LogLevel::Info, "T", "a"));
    buf.Clear();
    CHECK(buf.Size() == 0);
    buf.Write(Rec(LogLevel::Info, "T", "b"));
    std::vector<LogEntry> out;
    buf.Fetch(0, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].sequence == 2);
}

TEST_CASE("LogBuffer tolerates concurrent writers", "[log][buffer][threads]") {
    LogBuffer buf(100000);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&buf] {
            for (int i = 0; i < 500; ++i) buf.Write(Rec(LogLevel::Debug, "Thread", "x"));
        });
    }
    for (auto& th : threads) th.join();
    CHECK(buf.Size() == 2000);
    std::vector<LogEntry> out;
    buf.Fetch(0, out);
    REQUIRE(out.size() == 2000);
    for (usize i = 1; i < out.size(); ++i) {
        REQUIRE(out[i].sequence == out[i - 1].sequence + 1);
    }
}
