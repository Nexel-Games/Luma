#include <catch2/catch_test_macros.hpp>

#include "FakeGraphModel.h"
#include "Luma/GraphEditor/GraphController.h"

using namespace Luma;
using namespace Luma::GraphEditor;
using Luma::GraphEditor::Test::FakeGraphModel;

namespace {
PinKey Out(NodeId n, u32 i = 0) { return {n, PinDir::Output, i}; }
PinKey In(NodeId n, u32 i = 0) { return {n, PinDir::Input, i}; }
}  // namespace

TEST_CASE("Links are ordered output->input and validated by the model", "[grapheditor][controller]") {
    FakeGraphModel m;
    const NodeId a = m.Make(0, 1), b = m.Make(2, 1), tex = m.Spawn("Tex", {0, 0});
    GraphController c(m);
    int changes = 0;
    c.onChanged = [&] { ++changes; };

    CHECK(c.TryLink(In(b, 0), Out(a)).response == ConnectResponse::Make);  // dragged from the input
    REQUIRE(m.links.size() == 1);
    CHECK(m.links[0].from == Out(a));
    CHECK(changes == 1);

    CHECK(c.TryLink(Out(tex), In(b, 1)).response == ConnectResponse::Disallow);
    CHECK(m.links.size() == 1);
    CHECK(c.CheckLink(Out(a), Out(b)).response == ConnectResponse::Disallow);  // same direction
    CHECK(c.TryLink(Out(b), In(b, 0)).response == ConnectResponse::Disallow);  // same node

    const NodeId a2 = m.Make(0, 1);
    CHECK(c.TryLink(Out(a2), In(b, 0)).response == ConnectResponse::Replace);
    REQUIRE(m.links.size() == 1);
    CHECK(m.links[0].from == Out(a2));

    REQUIRE(c.Undo());
    CHECK(m.links[0].from == Out(a));
    REQUIRE(c.Redo());
    CHECK(m.links[0].from == Out(a2));
}

TEST_CASE("Delete removes the selection except non-removable nodes, undoably", "[grapheditor][controller]") {
    FakeGraphModel m;
    const NodeId a = m.Make(0, 1), out = m.Make(1, 0);
    m.nodes[out].view.removable = false;
    GraphController c(m);
    REQUIRE(c.TryLink(Out(a), In(out)).Allowed());
    c.Sel().Set({a, out});
    c.DeleteSelection();
    CHECK(m.nodes.count(a) == 0);
    CHECK(m.nodes.count(out) == 1);
    CHECK(m.links.empty());
    CHECK(c.Sel().Nodes() == std::set<NodeId>{out});
    c.Undo();
    CHECK(m.nodes.count(a) == 1);
    CHECK(m.links.size() == 1);
}

TEST_CASE("Moving is one transaction and carries nodes inside a selected comment", "[grapheditor][controller]") {
    FakeGraphModel m;
    const NodeId inside = m.Make(0, 1, {50, 60});
    const NodeId outside = m.Make(0, 1, {900, 900});
    const NodeId comment = m.AddComment({0, 0}, {400, 300}, "group");
    GraphController c(m);
    c.SelectOnly(comment);
    c.BeginMove();
    c.DragSelection({10, 0});
    c.DragSelection({10, 5});
    c.EndMove();
    CHECK(m.nodes[comment].view.position.x == 20);
    CHECK(m.nodes[inside].view.position.x == 70);
    CHECK(m.nodes[inside].view.position.y == 65);
    CHECK(m.nodes[outside].view.position.x == 900);
    CHECK(c.History().Size() == 1);
    c.Undo();
    CHECK(m.nodes[inside].view.position.x == 50);

    // A click without movement records nothing.
    c.BeginMove();
    c.EndMove();
    CHECK(c.History().Size() == 1);
}

TEST_CASE("Copy/paste keeps internal links, offsets and selects the result", "[grapheditor][controller]") {
    FakeGraphModel m;
    const NodeId a = m.Make(0, 1, {100, 100}), b = m.Make(1, 1, {300, 120});
    const NodeId lone = m.Make(1, 0, {600, 100});
    GraphController c(m);
    c.TryLink(Out(a), In(b));
    c.TryLink(Out(b), In(lone));
    c.Sel().Set({a, b});
    const std::string clip = c.CopySelection();
    c.Paste(clip, {1000, 1000});
    REQUIRE(m.nodes.size() == 5);
    CHECK(m.links.size() == 3);  // two originals + the copied internal one
    REQUIRE(c.Sel().Nodes().size() == 2);
    const NodeId first = *c.Sel().Nodes().begin();
    CHECK(m.nodes[first].view.position.x == 1000);
    CHECK(m.nodes[first].view.position.y == 1000);
    c.Undo();
    CHECK(m.nodes.size() == 3);

    c.Sel().Set({a});
    c.Duplicate();
    CHECK(m.nodes.size() == 4);
    CHECK_FALSE(c.Sel().Has(a));

    std::string cut;
    c.CutSelection(cut);
    CHECK_FALSE(cut.empty());
    CHECK(m.nodes.size() == 3);
}

TEST_CASE("Drag-off spawn links the first compatible pin", "[grapheditor][controller]") {
    FakeGraphModel m;
    const NodeId src = m.Make(0, 1);
    GraphController c(m);
    const NodeId added = c.SpawnAction("T2x1", {200, 0}, Out(src));
    REQUIRE(added != 0);
    REQUIRE(m.links.size() == 1);
    CHECK(m.links[0].to == In(added, 0));
    CHECK(c.Sel().Nodes() == std::set<NodeId>{added});
    CHECK(c.History().Size() == 1);
    // From an input: links the new node's output.
    const NodeId sink = m.Make(1, 0);
    const NodeId added2 = c.SpawnAction("T2x1", {0, 0}, In(sink));
    CHECK(m.links.back().from == Out(added2));
    CHECK(c.SpawnAction("nope", {0, 0}) == 0);
}

TEST_CASE("Reroute splits a wire; comments wrap the selection", "[grapheditor][controller]") {
    FakeGraphModel m;
    const NodeId a = m.Make(0, 1, {0, 0}), b = m.Make(1, 0, {400, 0});
    GraphController c(m);
    c.TryLink(Out(a), In(b));
    const NodeId r = c.InsertReroute(m.links[0], {200, 10});
    REQUIRE(r != 0);
    REQUIRE(m.links.size() == 2);
    CHECK(c.History().Size() == 2);

    c.Sel().Set({a, b});
    const NodeId comment = c.AddComment({0, 0});
    const NodeView& cv = m.nodes[comment].view;
    CHECK(cv.kind == NodeKind::Comment);
    CHECK(cv.position.x < 0);
    CHECK(cv.position.x + cv.size.x > 400);
}
