#pragma once

#include <algorithm>
#include <array>
#include <map>
#include <sstream>

#include "Luma/GraphEditor/GraphModel.h"

// A minimal IGraphModel for controller / widget tests. Pin "types" are
// ints: a link is legal when the types match or either is 0 (any). Nodes
// spawned by action "T<in>x<out>" get <in> inputs and <out> outputs of type 1;
// action "Tex" makes a node with one output of type 2.
namespace Luma::GraphEditor::Test {

class FakeGraphModel final : public IGraphModel {
public:
    struct FNode {
        NodeView view;
        std::vector<int> inTypes, outTypes;
    };
    std::map<NodeId, FNode> nodes;
    std::vector<LinkView> links;
    NodeId next = 1;

    NodeId Make(int ins, int outs, Vec2 pos = {0, 0}, int type = 1, NodeKind kind = NodeKind::Node) {
        FNode n;
        n.view.id = next++;
        n.view.kind = kind;
        n.view.position = pos;
        n.view.title = "N" + std::to_string(n.view.id);
        for (int i = 0; i < ins; ++i) {
            n.view.inputs.push_back({"in" + std::to_string(i)});
            n.inTypes.push_back(type);
        }
        for (int i = 0; i < outs; ++i) {
            n.view.outputs.push_back({"out" + std::to_string(i)});
            n.outTypes.push_back(type);
        }
        nodes[n.view.id] = n;
        return n.view.id;
    }

    std::vector<NodeView> Nodes() const override {
        std::vector<NodeView> out;
        for (auto& [id, n] : nodes) out.push_back(n.view);
        return out;
    }
    std::vector<LinkView> Links() const override { return links; }
    ConnectCheck CanConnect(PinKey o, PinKey i) const override {
        auto on = nodes.find(o.node), in = nodes.find(i.node);
        if (on == nodes.end() || in == nodes.end() || o.node == i.node) return {ConnectResponse::Disallow, "same node"};
        const int a = on->second.outTypes.at(o.index), b = in->second.inTypes.at(i.index);
        if (a != b && a != 0 && b != 0) return {ConnectResponse::Disallow, "type mismatch"};
        for (const LinkView& l : links)
            if (l.to == i) return {ConnectResponse::Replace, "Replace existing link"};
        return {ConnectResponse::Make, ""};
    }
    bool Connect(PinKey o, PinKey i) override {
        if (!CanConnect(o, i).Allowed()) return false;
        BreakLinks(i);
        links.push_back({o, i});
        return true;
    }
    void BreakLinks(PinKey p) override {
        std::erase_if(links, [&](const LinkView& l) { return p.dir == PinDir::Input ? l.to == p : l.from == p; });
    }
    void SetPosition(NodeId id, Vec2 p) override { nodes[id].view.position = p; }
    void SetSize(NodeId id, Vec2 s) override { nodes[id].view.size = s; }
    bool RemoveNode(NodeId id) override {
        auto it = nodes.find(id);
        if (it == nodes.end() || !it->second.view.removable) return false;
        nodes.erase(it);
        std::erase_if(links, [&](const LinkView& l) { return l.from.node == id || l.to.node == id; });
        return true;
    }
    std::vector<GraphAction> Actions(const PinKey*) const override {
        return {{"T2x1", "Math", "Add", "", ""}, {"Tex", "Texture", "Texture Sample", "", "image"}};
    }
    NodeId Spawn(const std::string& id, Vec2 pos) override {
        if (id == "Tex") return Make(0, 1, pos, 2);
        if (id.size() == 4 && id[0] == 'T') return Make(id[1] - '0', id[3] - '0', pos);
        return 0;
    }
    NodeId AddReroute(Vec2 pos) override { return Make(1, 1, pos, 0, NodeKind::Reroute); }
    NodeId AddComment(Vec2 pos, Vec2 size, std::string text) override {
        const NodeId id = Make(0, 0, pos, 0, NodeKind::Comment);
        nodes[id].view.size = size;
        nodes[id].view.title = std::move(text);
        return id;
    }
    struct Snap {
        std::map<NodeId, FNode> nodes;
        std::vector<LinkView> links;
        NodeId next;
    };
    std::any Snapshot() const override { return Snap{nodes, links, next}; }
    void Restore(const std::any& s) override {
        const Snap& snap = std::any_cast<const Snap&>(s);
        nodes = snap.nodes;
        links = snap.links;
        next = snap.next;
    }
    // Text: "id ins outs x y" per line, then "L fromId fromPin toId toPin".
    std::string Copy(const std::vector<NodeId>& ids) const override {
        std::ostringstream o;
        for (NodeId id : ids) {
            const FNode& n = nodes.at(id);
            o << "N " << id << ' ' << n.inTypes.size() << ' ' << n.outTypes.size() << ' ' << n.view.position.x
              << ' ' << n.view.position.y << '\n';
        }
        for (const LinkView& l : links) {
            if (std::count(ids.begin(), ids.end(), l.from.node) && std::count(ids.begin(), ids.end(), l.to.node))
                o << "L " << l.from.node << ' ' << l.from.index << ' ' << l.to.node << ' ' << l.to.index << '\n';
        }
        return o.str();
    }
    std::vector<NodeId> Paste(const std::string& text, Vec2 topLeft) override {
        std::istringstream in(text);
        std::string tag;
        struct Item {
            NodeId old;
            int ins, outs;
            Vec2 pos;
        };
        std::vector<Item> items;
        std::vector<std::array<NodeId, 4>> ls;
        while (in >> tag) {
            if (tag == "N") {
                Item it;
                in >> it.old >> it.ins >> it.outs >> it.pos.x >> it.pos.y;
                items.push_back(it);
            } else {
                std::array<NodeId, 4> l;
                in >> l[0] >> l[1] >> l[2] >> l[3];
                ls.push_back(l);
            }
        }
        if (items.empty()) return {};
        Vec2 mn{1e9f, 1e9f};
        for (auto& it : items) mn = {std::min(mn.x, it.pos.x), std::min(mn.y, it.pos.y)};
        std::map<NodeId, NodeId> remap;
        std::vector<NodeId> out;
        for (auto& it : items) {
            const NodeId id = Make(it.ins, it.outs, {it.pos.x - mn.x + topLeft.x, it.pos.y - mn.y + topLeft.y});
            remap[it.old] = id;
            out.push_back(id);
        }
        for (auto& l : ls)
            Connect({remap[l[0]], PinDir::Output, static_cast<u32>(l[1])},
                    {remap[l[2]], PinDir::Input, static_cast<u32>(l[3])});
        return out;
    }
};

}  // namespace Luma::GraphEditor::Test
