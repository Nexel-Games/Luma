#include <catch2/catch_test_macros.hpp>

#include "../Slate/SlateTestHarness.h"
#include "FakeGraphModel.h"
#include "Luma/GraphEditor/GraphEditorWidget.h"

using namespace Luma;
using namespace Luma::GraphEditor;
using Luma::GraphEditor::Test::FakeGraphModel;

namespace {
// Widget with an identity view (graph space == screen space).
struct WidgetFixture {
    FakeGraphModel model;
    NodeId a = model.Make(0, 1, {0, 0});
    NodeId b = model.Make(1, 0, {300, 0});
    GraphController controller{model};
    GraphEditorWidget widget{controller};
    Slate::Test::Harness h;
    WidgetFixture() {
        h.Frame([&] { Draw(); });
        widget.View() = ViewTransform{};
    }
    void Draw() { widget.Draw(h.ctx, {0, 0, 800, 600}); }
    auto Fn() {
        return [this] { Draw(); };
    }
};
}  // namespace

TEST_CASE("Dragging from an output pin onto an input links them", "[grapheditor][widget]") {
    WidgetFixture f;
    // Output anchor of `a`: (110, 26 + 11); input of `b`: (300, 37).
    f.h.Drag(110, 37, 190, 0, f.Fn());
    REQUIRE(f.model.links.size() == 1);
    CHECK(f.model.links[0].to.node == f.b);
}

TEST_CASE("Dragging a node moves it as one undo step", "[grapheditor][widget]") {
    WidgetFixture f;
    f.h.Drag(50, 10, 20, 5, f.Fn());
    CHECK(f.model.nodes[f.a].view.position.x == 20);
    CHECK(f.controller.Sel().Has(f.a));
    CHECK(f.controller.History().Size() == 1);
}

TEST_CASE("Marquee selects the nodes it touches", "[grapheditor][widget]") {
    WidgetFixture f;
    f.h.Drag(600, 300, -650, -310, f.Fn());
    CHECK(f.controller.Sel().Nodes().size() == 2);
}

TEST_CASE("Right-click on empty canvas opens the searchable node menu", "[grapheditor][widget]") {
    WidgetFixture f;
    f.h.ctx.OnMouseMove(500, 400);
    f.h.ctx.OnMouseButton(1, true);
    f.h.Frame(f.Fn());
    f.h.ctx.OnMouseButton(1, false);
    f.h.Frame(f.Fn());
    CHECK(f.h.ctx.AnyMenuOpen());
}

TEST_CASE("Action menus nest categories and sort submenus first", "[grapheditor][widget]") {
    std::string picked;
    const auto menu = BuildActionMenu({{"x", "", "Zed", "", ""}, {"m", "Math|Trig", "Sine", "", ""}, {"a", "Math", "Add", "", ""}},
                                      [&](const std::string& id) { picked = id; });
    REQUIRE(menu.size() == 2);
    CHECK(menu[0].label == "Math");
    REQUIRE(menu[0].children.size() == 2);
    CHECK(menu[0].children[0].label == "Trig");
    CHECK(menu[0].children[1].label == "Add");
    menu[0].children[0].children[0].action();
    CHECK(picked == "m");
}
