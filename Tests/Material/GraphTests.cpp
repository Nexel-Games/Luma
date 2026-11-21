// Material graph data model: nodes, links, comments, the Material Output
// root, stable ids and cycle detection.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "Luma/MaterialGraph/Graph.h"

using namespace Luma;
using namespace Luma::MaterialGraph;

TEST_CASE("A new graph has exactly one Material Output node", "[graph]") {
    Graph g;
    REQUIRE(g.Nodes().size() == 1);
    CHECK(g.Nodes()[0].type == "MaterialOutput");
    CHECK(g.OutputNode() == g.Nodes()[0].id);
    CHECK(g.Find(g.OutputNode()) != nullptr);
}

TEST_CASE("Node ids increase and are never reused", "[graph]") {
    Graph g;
    const NodeId a = g.AddNode("Constant", {0, 0});
    const NodeId b = g.AddNode("Constant", {10, 0});
    CHECK(b > a);
    CHECK(g.RemoveNode(b));
    const NodeId c = g.AddNode("Constant", {20, 0});
    CHECK(c > b);
    CHECK(g.NextId() > c);
}

TEST_CASE("The Material Output node can't be removed", "[graph]") {
    Graph g;
    CHECK_FALSE(g.RemoveNode(g.OutputNode()));
    CHECK(g.Nodes().size() == 1);
}

TEST_CASE("Connect replaces an existing link into the same input", "[graph]") {
    Graph g;
    const NodeId a = g.AddNode("Constant", {});
    const NodeId b = g.AddNode("Constant", {});
    std::string err;
    REQUIRE(g.Connect({a, 0}, {g.OutputNode(), 0}, &err));
    REQUIRE(g.Connect({b, 0}, {g.OutputNode(), 0}, &err));
    CHECK(g.Links().size() == 1);
    CHECK(g.Source({g.OutputNode(), 0})->node == b);
    g.Disconnect({g.OutputNode(), 0});
    CHECK_FALSE(g.Source({g.OutputNode(), 0}).has_value());
}

TEST_CASE("Removing a node removes its links", "[graph]") {
    Graph g;
    const NodeId a = g.AddNode("Constant", {});
    const NodeId m = g.AddNode("Multiply", {});
    std::string err;
    REQUIRE(g.Connect({a, 0}, {m, 0}, &err));
    REQUIRE(g.Connect({m, 0}, {g.OutputNode(), 3}, &err));
    REQUIRE(g.RemoveNode(m));
    CHECK(g.Links().empty());
}

TEST_CASE("Connecting unknown nodes or a node to itself fails", "[graph]") {
    Graph g;
    const NodeId a = g.AddNode("Add", {});
    std::string err;
    CHECK_FALSE(g.Connect({999, 0}, {a, 0}, &err));
    CHECK_FALSE(err.empty());
    err.clear();
    CHECK_FALSE(g.Connect({a, 0}, {a, 1}, &err));
    CHECK_FALSE(err.empty());
    CHECK_FALSE(g.Connect({a, 0}, {g.OutputNode(), 0}, nullptr) == false);  // valid link works
}

TEST_CASE("FindCycle reports the nodes on a cycle", "[graph]") {
    Graph g;
    const NodeId a = g.AddNode("Add", {});
    const NodeId b = g.AddNode("Add", {});
    const NodeId c = g.AddNode("Add", {});
    std::string err;
    REQUIRE(g.Connect({a, 0}, {b, 0}, &err));
    REQUIRE(g.Connect({b, 0}, {c, 0}, &err));
    CHECK(g.FindCycle().empty());
    REQUIRE(g.Connect({c, 0}, {a, 0}, &err));
    auto cycle = g.FindCycle();
    std::sort(cycle.begin(), cycle.end());
    CHECK(cycle == std::vector<NodeId>{a, b, c});
}

TEST_CASE("Node properties and comments are stored", "[graph]") {
    Graph g;
    const NodeId p = g.AddNode("ScalarParameter", {5, 6});
    Node* n = g.Find(p);
    REQUIRE(n);
    n->properties["name"] = PropertyValue(std::string("Roughness"));
    n->properties["default"] = PropertyValue(0.5);
    n->properties["useSlider"] = PropertyValue(true);
    CHECK(g.Find(p)->properties.at("name").AsString() == "Roughness");
    CHECK(g.Find(p)->properties.at("default").AsFloat() == 0.5);
    CHECK(g.Find(p)->properties.at("useSlider").AsBool());
    CHECK(g.Find(p)->position.x == 5.0f);
    const u64 cid = g.AddComment({0, 0}, {100, 50}, "Wind");
    REQUIRE(g.Comments().size() == 1);
    CHECK(g.Comments()[0].id == cid);
    CHECK(g.Comments()[0].text == "Wind");
}

TEST_CASE("PropertyValue converts between numeric kinds", "[graph]") {
    CHECK(PropertyValue(3.0).AsInt() == 3);
    CHECK(PropertyValue(i64{2}).AsFloat() == 2.0);
    CHECK(PropertyValue(Math::Vec4{1, 2, 3, 4}).AsVec4().z == 3.0f);
    CHECK(PropertyValue(true).AsFloat() == 1.0);
    CHECK(PropertyValue(std::string("x")).AsFloat() == 0.0);
}
