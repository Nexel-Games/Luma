#include <catch2/catch_test_macros.hpp>

#include "Luma/Editor/Panels/ThumbnailBudget.h"

using Luma::Editor::Panels::ThumbnailBudget;

TEST_CASE("Thumbnail budget: one per frame always, more while time remains", "[editor][thumbnail]") {
    ThumbnailBudget budget(8.0);
    budget.BeginFrame(100.0);
    REQUIRE(budget.TryStart(100.0));   // first of the frame
    REQUIRE(budget.TryStart(104.0));   // still inside 8 ms
    REQUIRE_FALSE(budget.TryStart(109.0));
    REQUIRE_FALSE(budget.TryStart(110.0));
    REQUIRE(budget.StartedThisFrame() == 2);
    REQUIRE(budget.DeferredThisFrame() == 2);

    budget.BeginFrame(200.0);
    REQUIRE(budget.DeferredThisFrame() == 0);
    // Even a frame that is already late gets one.
    REQUIRE(budget.TryStart(250.0));
    REQUIRE_FALSE(budget.TryStart(250.0));
    REQUIRE(budget.StartedThisFrame() == 1);
}
