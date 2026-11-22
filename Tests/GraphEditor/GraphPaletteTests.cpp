#include <catch2/catch_test_macros.hpp>

#include "Luma/GraphEditor/GraphPalette.h"

using namespace Luma::GraphEditor;

TEST_CASE("Palette filters by every term and groups by top category", "[grapheditor][palette]") {
    const std::vector<GraphAction> actions{{"mul", "Math", "Multiply", "", "times *"},
                                           {"sin", "Math|Trig", "Sine", "", ""},
                                           {"tex", "Texture", "Texture Sample", "", "image"},
                                           {"add", "Math", "Add", "", "plus"}};
    auto all = FilterActions(actions, "");
    REQUIRE(all.size() == 2);
    CHECK(all[0].category == "Math");
    REQUIRE(all[0].actions.size() == 3);
    CHECK(all[0].actions[0].label == "Add");

    auto times = FilterActions(actions, "TIMES");
    REQUIRE(times.size() == 1);
    CHECK(times[0].actions[0].id == "mul");

    CHECK(FilterActions(actions, "trig si").at(0).actions.at(0).id == "sin");
    CHECK(FilterActions(actions, "texture plus").empty());
}
