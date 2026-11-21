#include "Luma/MaterialGraph/Graph.h"

#include <algorithm>
#include <functional>
#include <unordered_map>

namespace Luma::MaterialGraph {

// --- PropertyValue ---------------------------------------------------------

bool PropertyValue::AsBool() const {
    if (auto* b = std::get_if<bool>(&m_value)) return *b;
    if (auto* i = std::get_if<i64>(&m_value)) return *i != 0;
    if (auto* f = std::get_if<f64>(&m_value)) return *f != 0.0;
    return false;
}

i64 PropertyValue::AsInt() const {
    if (auto* i = std::get_if<i64>(&m_value)) return *i;
    if (auto* f = std::get_if<f64>(&m_value)) return static_cast<i64>(*f);
    if (auto* b = std::get_if<bool>(&m_value)) return *b ? 1 : 0;
    return 0;
}

f64 PropertyValue::AsFloat() const {
    if (auto* f = std::get_if<f64>(&m_value)) return *f;
    if (auto* i = std::get_if<i64>(&m_value)) return static_cast<f64>(*i);
    if (auto* b = std::get_if<bool>(&m_value)) return *b ? 1.0 : 0.0;
    if (auto* v = std::get_if<Math::Vec4>(&m_value)) return v->x;
    return 0.0;
}

Math::Vec4 PropertyValue::AsVec4() const {
    if (auto* v = std::get_if<Math::Vec4>(&m_value)) return *v;
    const f32 s = static_cast<f32>(AsFloat());
    return {s, s, s, s};
}

std::string PropertyValue::AsString() const {
    if (auto* s = std::get_if<std::string>(&m_value)) return *s;
    return {};
}

bool PropertyValue::operator==(const PropertyValue& o) const {
    if (m_value.index() != o.m_value.index()) return false;
    if (auto* v = std::get_if<Math::Vec4>(&m_value)) {
        const auto& w = std::get<Math::Vec4>(o.m_value);
        return v->x == w.x && v->y == w.y && v->z == w.z && v->w == w.w;
    }
    if (auto* b = std::get_if<bool>(&m_value)) return *b == std::get<bool>(o.m_value);
    if (auto* i = std::get_if<i64>(&m_value)) return *i == std::get<i64>(o.m_value);
    if (auto* f = std::get_if<f64>(&m_value)) return *f == std::get<f64>(o.m_value);
    return std::get<std::string>(m_value) == std::get<std::string>(o.m_value);
}

// --- Graph -----------------------------------------------------------------

Graph::Graph() { m_output = AddNode(kOutputType, {0.0f, 0.0f}); }

NodeId Graph::AddNode(std::string type, Math::Vec2 position) {
    Node n;
    n.id = m_nextId++;
    n.type = std::move(type);
    n.position = position;
    m_nodes.push_back(std::move(n));
    return m_nodes.back().id;
}

bool Graph::AddNodeWithId(NodeId id, std::string type, Math::Vec2 position) {
    if (id == 0 || Find(id)) return false;
    if (type == kOutputType) {
        // Deserialization replaces the constructor's output node.
        if (Node* out = Find(m_output); out && m_output != id) {
            const NodeId old = m_output;
            out->id = id;
            out->position = position;
            for (Link& l : m_links) {
                if (l.to.node == old) l.to.node = id;
            }
            m_output = id;
            m_nextId = std::max(m_nextId, id + 1);
            return true;
        }
    }
    Node n;
    n.id = id;
    n.type = std::move(type);
    n.position = position;
    m_nodes.push_back(std::move(n));
    m_nextId = std::max(m_nextId, id + 1);
    return true;
}

bool Graph::RemoveNode(NodeId id) {
    if (id == m_output) return false;
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [id](const Node& n) { return n.id == id; });
    if (it == m_nodes.end()) return false;
    m_nodes.erase(it);
    std::erase_if(m_links, [id](const Link& l) { return l.from.node == id || l.to.node == id; });
    return true;
}

Node* Graph::Find(NodeId id) {
    for (Node& n : m_nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

const Node* Graph::Find(NodeId id) const { return const_cast<Graph*>(this)->Find(id); }

bool Graph::Connect(PinRef from, PinRef to, std::string* error) {
    auto fail = [error](std::string msg) {
        if (error) *error = std::move(msg);
        return false;
    };
    if (!Find(from.node)) return fail("unknown source node " + std::to_string(from.node));
    if (!Find(to.node)) return fail("unknown target node " + std::to_string(to.node));
    if (from.node == to.node) return fail("a node can't be linked to itself");
    Disconnect(to);
    m_links.push_back({from, to});
    return true;
}

void Graph::Disconnect(PinRef to) {
    std::erase_if(m_links, [to](const Link& l) { return l.to == to; });
}

std::optional<PinRef> Graph::Source(PinRef to) const {
    for (const Link& l : m_links) {
        if (l.to == to) return l.from;
    }
    return std::nullopt;
}

u64 Graph::AddComment(Math::Vec2 position, Math::Vec2 size, std::string text) {
    Comment c;
    c.id = m_nextCommentId++;
    c.position = position;
    c.size = size;
    c.text = std::move(text);
    m_comments.push_back(std::move(c));
    return m_comments.back().id;
}

std::vector<NodeId> Graph::FindCycle() const {
    // Edges point from a consumer to the nodes feeding it (input direction);
    // a DFS with an explicit colour map finds the first back edge.
    std::unordered_map<NodeId, std::vector<NodeId>> feeds;
    for (const Link& l : m_links) feeds[l.to.node].push_back(l.from.node);
    enum class Mark : u8 { White, Grey, Black };
    std::unordered_map<NodeId, Mark> mark;
    std::vector<NodeId> stack;
    std::vector<NodeId> cycle;
    std::function<bool(NodeId)> visit = [&](NodeId n) -> bool {
        mark[n] = Mark::Grey;
        stack.push_back(n);
        for (NodeId m : feeds[n]) {
            const Mark mk = mark.count(m) ? mark[m] : Mark::White;
            if (mk == Mark::Grey) {
                auto it = std::find(stack.begin(), stack.end(), m);
                cycle.assign(it, stack.end());
                return true;
            }
            if (mk == Mark::White && visit(m)) return true;
        }
        stack.pop_back();
        mark[n] = Mark::Black;
        return false;
    };
    for (const Node& n : m_nodes) {
        if ((mark.count(n.id) ? mark[n.id] : Mark::White) == Mark::White && visit(n.id)) break;
    }
    return cycle;
}

}  // namespace Luma::MaterialGraph
