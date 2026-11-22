#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Luma/GraphEditor/GraphMath.h"
#include "Luma/GraphEditor/Selection.h"

using namespace Luma;
using namespace Luma::GraphEditor;
using Catch::Approx;

TEST_CASE("View transform round-trips graph and screen space", "[grapheditor]") {
    ViewTransform v;
    v.offset = {100, 50};
    v.SetLevel(12);  // 1.5x
    const Vec2 origin{10, 20};
    const Vec2 s = v.ToScreen({120, 60}, origin);
    CHECK(s.x == Approx(10 + 20 * 1.5f));
    CHECK(s.y == Approx(20 + 10 * 1.5f));
    const Vec2 g = v.ToGraph(s, origin);
    CHECK(g.x == Approx(120));
    CHECK(g.y == Approx(60));
}

TEST_CASE("Zooming keeps the point under the cursor fixed and clamps", "[grapheditor]") {
    ViewTransform v;
    const Vec2 origin{0, 0}, cursor{300, 200};
    const Vec2 before = v.ToGraph(cursor, origin);
    v.ZoomAt(-3, cursor, origin);
    CHECK(v.Zoom() < 1.0f);
    const Vec2 after = v.ToGraph(cursor, origin);
    CHECK(after.x == Approx(before.x));
    CHECK(after.y == Approx(before.y));
    v.ZoomAt(100, cursor, origin);
    CHECK(v.Zoom() == 2.0f);
    v.ZoomAt(-100, cursor, origin);
    CHECK(v.Zoom() == 0.1f);
}

TEST_CASE("Panning moves by screen pixels at any zoom", "[grapheditor]") {
    ViewTransform v;
    v.SetLevel(14);  // 2x
    v.Pan({20, -10});
    CHECK(v.offset.x == Approx(-10));
    CHECK(v.offset.y == Approx(5));
}

TEST_CASE("Framing fits a rect without zooming past 1:1", "[grapheditor]") {
    ViewTransform v;
    v.Frame({0, 0, 2000, 1000}, {800, 600});
    CHECK(v.Zoom() <= 0.4f);
    const Vec2 centre = v.ToScreen({1000, 500}, {0, 0});
    CHECK(centre.x == Approx(400));
    CHECK(centre.y == Approx(300));
    v.Frame({0, 0, 10, 10}, {800, 600});
    CHECK(v.Zoom() == 1.0f);
}

TEST_CASE("Node layout places pins in rows and hit-tests them", "[grapheditor]") {
    NodeView n;
    n.position = {100, 100};
    n.title = "Multiply";
    n.inputs = {{"A"}, {"B"}};
    n.outputs = {{"Out"}};
    const NodeMetrics m;
    const NodeGeometry g = LayoutNode(n, m);
    REQUIRE(g.inputs.size() == 2);
    REQUIRE(g.outputs.size() == 1);
    CHECK(g.inputs[0].x == Approx(100));
    CHECK(g.outputs[0].x == Approx(g.body.Right()));
    CHECK(g.inputs[1].y == Approx(g.inputs[0].y + m.pinRowH));
    CHECK(g.body.h == Approx(m.headerH + 2 * m.pinRowH + m.padBottom));
    CHECK(g.body.w >= m.minWidth);

    auto hit = HitPin(n, g, {g.inputs[1].x + 3, g.inputs[1].y - 2}, m);
    REQUIRE(hit);
    CHECK(hit->dir == PinDir::Input);
    CHECK(hit->index == 1);
    CHECK_FALSE(HitPin(n, g, {g.body.x + g.body.w / 2, g.body.y + 5}, m));
    CHECK(HitNode(n, g, {g.body.x + 10, g.body.y + 5}, m));
}

TEST_CASE("Reroute nodes are small with centred pins; comments hit on their title", "[grapheditor]") {
    NodeView r;
    r.kind = NodeKind::Reroute;
    r.position = {0, 0};
    r.inputs = {{""}};
    r.outputs = {{""}};
    const NodeGeometry g = LayoutNode(r);
    CHECK(g.body.w == Approx(16));
    CHECK(g.inputs[0].y == Approx(g.outputs[0].y));

    NodeView c;
    c.kind = NodeKind::Comment;
    c.position = {0, 0};
    c.size = {300, 200};
    const NodeGeometry cg = LayoutNode(c);
    CHECK(cg.body.w == Approx(300));
    CHECK(HitNode(c, cg, {50, 10}));
    CHECK_FALSE(HitNode(c, cg, {150, 150}));
}

TEST_CASE("Wires leave outputs to the right and measure distance", "[grapheditor]") {
    const Wire w = MakeWire({0, 0}, {200, 100});
    CHECK(w.c0.x > 0);
    CHECK(w.c1.x < 200);
    CHECK(w.At(0).x == Approx(0));
    CHECK(w.At(1).y == Approx(100));
    CHECK(DistanceToWire(w, w.At(0.5f)) < 1.0f);
    CHECK(DistanceToWire(w, {100, -200}) > 100.0f);
    // Backwards wires still bulge outward.
    const Wire back = MakeWire({200, 0}, {0, 0});
    CHECK(back.c0.x > 200);
}

TEST_CASE("Rect helpers", "[grapheditor]") {
    const Rect r = RectFromCorners({10, 50}, {0, 0});
    CHECK(r.x == 0);
    CHECK(r.h == 50);
    CHECK(Overlaps({0, 0, 10, 10}, {5, 5, 10, 10}));
    CHECK_FALSE(Overlaps({0, 0, 10, 10}, {20, 0, 5, 5}));
    CHECK(ContainsRect({0, 0, 100, 100}, {10, 10, 5, 5}));
    CHECK_FALSE(ContainsRect({0, 0, 100, 100}, {90, 10, 20, 5}));
}

TEST_CASE("Marquee operations follow UE's modifiers", "[grapheditor]") {
    CHECK(MarqueeOpFromModifiers(false, false, false) == MarqueeOp::Replace);
    CHECK(MarqueeOpFromModifiers(false, true, false) == MarqueeOp::Add);
    CHECK(MarqueeOpFromModifiers(false, false, true) == MarqueeOp::Remove);
    CHECK(MarqueeOpFromModifiers(true, false, false) == MarqueeOp::Invert);

    const std::set<NodeId> before{1, 2};
    Selection s;
    s.ApplyMarquee({2, 3}, MarqueeOp::Replace, before);
    CHECK(s.Nodes() == std::set<NodeId>{2, 3});
    s.ApplyMarquee({2, 3}, MarqueeOp::Add, before);
    CHECK(s.Nodes() == std::set<NodeId>{1, 2, 3});
    s.ApplyMarquee({2, 3}, MarqueeOp::Remove, before);
    CHECK(s.Nodes() == std::set<NodeId>{1});
    s.ApplyMarquee({2, 3}, MarqueeOp::Invert, before);
    CHECK(s.Nodes() == std::set<NodeId>{1, 3});
    s.Toggle(1);
    CHECK_FALSE(s.Has(1));
    NodeView alive;
    alive.id = 3;
    s.Prune({alive});
    CHECK(s.Nodes() == std::set<NodeId>{3});
}

TEST_CASE("Undo stack restores before/after states and caps its depth", "[grapheditor]") {
    UndoStack u(2);
    CHECK_FALSE(u.CanUndo());
    u.Push("a", 0, 1);
    u.Push("b", 1, 2);
    CHECK(u.UndoLabel() == "b");
    const std::any* s = u.Undo();
    REQUIRE(s);
    CHECK(std::any_cast<int>(*s) == 1);
    CHECK(u.CanRedo());
    CHECK(std::any_cast<int>(*u.Redo()) == 2);
    CHECK_FALSE(u.CanRedo());
    u.Undo();
    u.Push("c", 1, 5);  // drops the redo branch
    CHECK_FALSE(u.CanRedo());
    u.Push("d", 5, 6);  // cap 2: "a" falls off
    CHECK(u.Size() == 2);
    u.Undo();
    u.Undo();
    CHECK_FALSE(u.CanUndo());
    CHECK(u.Undo() == nullptr);
}
