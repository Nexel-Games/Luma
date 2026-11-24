#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "Luma/Editor/Panels/Console.h"

using Luma::LogEntry;
using Luma::LogLevel;
using Luma::Editor::Panels::ConsolePanel;

namespace {
LogEntry Entry(LogLevel level, const char* cat, const char* msg) {
    LogEntry e;
    e.level = level;
    e.category = cat;
    e.message = msg;
    e.userFacing = true;
    return e;
}
}  // namespace

TEST_CASE("Output Log filter: severity buckets", "[console]") {
    ConsolePanel::Filter f;  // defaults: errors, warnings, messages on; verbose off
    CHECK(ConsolePanel::Passes(Entry(LogLevel::Error, "A", "x"), f));
    CHECK(ConsolePanel::Passes(Entry(LogLevel::Fatal, "A", "x"), f));
    CHECK(ConsolePanel::Passes(Entry(LogLevel::Warning, "A", "x"), f));
    CHECK(ConsolePanel::Passes(Entry(LogLevel::Info, "A", "x"), f));
    CHECK_FALSE(ConsolePanel::Passes(Entry(LogLevel::Debug, "A", "x"), f));
    CHECK_FALSE(ConsolePanel::Passes(Entry(LogLevel::Trace, "A", "x"), f));
    f.verbose = true;
    f.errors = false;
    CHECK(ConsolePanel::Passes(Entry(LogLevel::Debug, "A", "x"), f));
    CHECK_FALSE(ConsolePanel::Passes(Entry(LogLevel::Error, "A", "x"), f));
}

TEST_CASE("Output Log filter: hidden categories and text search", "[console]") {
    ConsolePanel::Filter f;
    f.hiddenCategories.insert("Vulkan");
    CHECK_FALSE(ConsolePanel::Passes(Entry(LogLevel::Info, "Vulkan", "swapchain"), f));
    CHECK(ConsolePanel::Passes(Entry(LogLevel::Info, "Editor", "swapchain"), f));

    f.hiddenCategories.clear();
    f.text = "SWAP";  // case-insensitive, message
    CHECK(ConsolePanel::Passes(Entry(LogLevel::Info, "Vulkan", "created swapchain"), f));
    CHECK_FALSE(ConsolePanel::Passes(Entry(LogLevel::Info, "Vulkan", "device lost"), f));
    f.text = "vulk";  // matches the category too
    CHECK(ConsolePanel::Passes(Entry(LogLevel::Info, "Vulkan", "device lost"), f));
}

TEST_CASE("Output Log hides engine diagnostics unless enabled", "[console]") {
    ConsolePanel::Filter f;
    LogEntry engine = Entry(LogLevel::Info, "Vulkan", "swapchain 1280x720");
    engine.userFacing = false;
    LogEntry user = Entry(LogLevel::Info, "Scene", "Saved scene 'Main.lscene'");
    CHECK_FALSE(ConsolePanel::Passes(engine, f));  // default: project messages only
    CHECK(ConsolePanel::Passes(user, f));
    f.engineLogs = true;
    CHECK(ConsolePanel::Passes(engine, f));
    CHECK(ConsolePanel::Passes(user, f));
}

TEST_CASE("Output Log footer keeps the newest visible error ahead of later messages", "[console]") {
    auto history = std::make_shared<Luma::LogBuffer>(8);
    ConsolePanel panel;
    panel.SetSource(history);

    auto write = [&](LogLevel level, const char* message, bool userFacing = true) {
        Luma::LogRecord record{level, "Scene", message, {}, {}, userFacing};
        history->Write(record);
    };

    CHECK_FALSE(panel.LatestStatusEntry());
    write(LogLevel::Info, "Scene loaded");
    REQUIRE(panel.LatestStatusEntry());
    CHECK(panel.LatestStatusEntry()->message == "Scene loaded");

    write(LogLevel::Warning, "Missing texture");
    CHECK(panel.LatestStatusEntry()->message == "Missing texture");
    write(LogLevel::Error, "Save failed");
    write(LogLevel::Info, "Selection changed");
    write(LogLevel::Error, "Import failed");
    write(LogLevel::Warning, "Fallback applied");
    CHECK(panel.LatestStatusEntry()->message == "Import failed");

    write(LogLevel::Fatal, "Renderer stopped", false);
    CHECK(panel.LatestStatusEntry()->message == "Import failed");
}
