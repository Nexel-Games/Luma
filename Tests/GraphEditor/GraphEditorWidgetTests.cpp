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
    void Draw() {
        widget.Draw(h.ctx, {0, 0, 800, 600});
        widget.DrawOverlays(h.ctx);
    }
    auto Fn() {
        return [this] { Draw(); };
    }
};
}  // namespace

TEST_CASE("Dragging from an output pin onto an input links them", "[grapheditor][widget]") {
    WidgetFixture f;
    const auto nodes = f.model.Nodes();
    const auto ga = LayoutNode(nodes[0], f.controller.Metrics());
    const auto gb = LayoutNode(nodes[1], f.controller.Metrics());
    const Vec2 from = ga.outputs[0], to = gb.inputs[0];
    f.h.Drag(from.x, from.y, to.x - from.x, to.y - from.y, f.Fn());
    REQUIRE(f.model.links.size() == 1);
    CHECK(f.model.links[0].to.node == f.b);
}

TEST_CASE("Dragging a node moves it as one undo step", "[grapheditor][widget]") {
    WidgetFixture f;
    f.h.Drag(30, 8, 20, 5, f.Fn());
    CHECK(f.model.nodes[f.a].view.position.x == 20);
    CHECK(f.controller.Sel().Has(f.a));
    CHECK(f.controller.History().Size() == 1);
}

TEST_CASE("Marquee selects the nodes it touches", "[grapheditor][widget]") {
    WidgetFixture f;
    f.h.Drag(600, 300, -650, -310, f.Fn());
    CHECK(f.controller.Sel().Nodes().size() == 2);
}

TEST_CASE("Right-click on empty canvas pops the palette; Enter adds at the click", "[grapheditor][widget]") {
    WidgetFixture f;
    f.h.ctx.OnMouseMove(500, 300);
    f.h.ctx.OnMouseButton(1, true);
    f.h.Frame(f.Fn());
    f.h.ctx.OnMouseButton(1, false);
    f.h.Frame(f.Fn());
    REQUIRE(f.widget.PaletteOpen());
    f.h.Frame(f.Fn());  // search box takes focus
    for (char c : std::string("image")) f.h.ctx.OnText(static_cast<u32>(c));
    f.h.Frame(f.Fn());
    f.h.ctx.OnKey(257, true);  // Enter
    f.h.Frame(f.Fn());
    f.h.ctx.OnKey(257, false);
    CHECK_FALSE(f.widget.PaletteOpen());
    REQUIRE(f.model.nodes.size() == 3);
    const auto& added = f.model.nodes.rbegin()->second.view;
    CHECK(added.title == "N3");
    CHECK(added.position.x == 500);
    CHECK(added.position.y == 300);
}

TEST_CASE("Escape or a click outside closes the palette", "[grapheditor][widget]") {
    WidgetFixture f;
    f.h.ctx.OnMouseMove(100, 400);
    f.h.ctx.OnMouseButton(1, true);
    f.h.Frame(f.Fn());
    f.h.ctx.OnMouseButton(1, false);
    f.h.Frame(f.Fn());
    REQUIRE(f.widget.PaletteOpen());
    f.h.Click(780, 20, f.Fn());
    CHECK_FALSE(f.widget.PaletteOpen());
    CHECK(f.model.nodes.size() == 2);
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
