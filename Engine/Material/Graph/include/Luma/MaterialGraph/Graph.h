#pragma once

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/Math/Math.h"

// Luma material graph — the node-graph data model of a material (Unreal's
// UMaterial expression graph): expression nodes wired output -> input into a
// single Material Output node. Pure data; the expression catalogue lives in
// ExpressionRegistry.h and the compiler in Luma::MaterialCompiler.

namespace Luma::MaterialGraph {

// Value types that flow along links.
enum class ValueType : u8 { Float1, Float2, Float3, Float4, Bool, Texture2D, TextureCube, Any };

using NodeId = u64;

// A node property value (parameter name, default value, enum choice...).
class PropertyValue {
public:
    using Storage = std::variant<bool, i64, f64, Math::Vec4, std::string>;
    PropertyValue() : m_value(f64{0.0}) {}
    explicit PropertyValue(bool v) : m_value(v) {}
    explicit PropertyValue(i64 v) : m_value(v) {}
    explicit PropertyValue(int v) : m_value(i64{v}) {}
    explicit PropertyValue(f64 v) : m_value(v) {}
    explicit PropertyValue(f32 v) : m_value(f64{v}) {}
    explicit PropertyValue(Math::Vec4 v) : m_value(v) {}
    explicit PropertyValue(std::string v) : m_value(std::move(v)) {}
    explicit PropertyValue(const char* v) : m_value(std::string(v)) {}

    bool IsBool() const { return std::holds_alternative<bool>(m_value); }
    bool IsInt() const { return std::holds_alternative<i64>(m_value); }
    bool IsFloat() const { return std::holds_alternative<f64>(m_value); }
    bool IsVec4() const { return std::holds_alternative<Math::Vec4>(m_value); }
    bool IsString() const { return std::holds_alternative<std::string>(m_value); }

    // Lenient accessors: numeric kinds convert; anything else yields 0 / "".
    bool AsBool() const;
    i64 AsInt() const;
    f64 AsFloat() const;
    Math::Vec4 AsVec4() const;          // scalars broadcast to xyzw
    std::string AsString() const;       // numbers are not stringified
    const Storage& Raw() const { return m_value; }
    bool operator==(const PropertyValue& o) const;

private:
    Storage m_value;
};

struct Node {
    NodeId id = 0;
    std::string type;                    // expression type name (ExpressionRegistry)
    Math::Vec2 position{0.0f, 0.0f};     // editor canvas position
    std::map<std::string, PropertyValue> properties;
    std::string comment;                 // per-node comment bubble
};

struct PinRef {
    NodeId node = 0;
    u32 pin = 0;
    bool operator==(const PinRef& o) const { return node == o.node && pin == o.pin; }
};

struct Link {
    PinRef from;  // output pin
    PinRef to;    // input pin
};

struct Comment {
    u64 id = 0;
    Math::Vec2 position{0.0f, 0.0f};
    Math::Vec2 size{200.0f, 120.0f};
    std::string text;
    Math::Vec4 color{0.15f, 0.15f, 0.15f, 0.6f};
};

class Graph {
public:
    static constexpr const char* kOutputType = "MaterialOutput";

    Graph();  // creates the Material Output node

    NodeId AddNode(std::string type, Math::Vec2 position);
    // Adds a node with a specific id (deserialization); fails if taken.
    bool AddNodeWithId(NodeId id, std::string type, Math::Vec2 position);
    bool RemoveNode(NodeId id);  // the output node can't be removed
    Node* Find(NodeId id);
    const Node* Find(NodeId id) const;

    // Output pin `from` -> input pin `to`; replaces any link into `to`.
    // Fails for unknown nodes or a node linked to itself (pin indices are
    // validated against the expression catalogue by the compiler/editor).
    bool Connect(PinRef from, PinRef to, std::string* error);
    void Disconnect(PinRef to);
    std::optional<PinRef> Source(PinRef to) const;

    const std::vector<Node>& Nodes() const { return m_nodes; }
    const std::vector<Link>& Links() const { return m_links; }
    std::vector<Comment>& Comments() { return m_comments; }
    const std::vector<Comment>& Comments() const { return m_comments; }
    u64 AddComment(Math::Vec2 position, Math::Vec2 size, std::string text);

    NodeId OutputNode() const { return m_output; }
    // Empty when acyclic, else the ids of the nodes on one cycle.
    std::vector<NodeId> FindCycle() const;
    NodeId NextId() const { return m_nextId; }

private:
    std::vector<Node> m_nodes;
    std::vector<Link> m_links;
    std::vector<Comment> m_comments;
    NodeId m_output = 0;
    NodeId m_nextId = 1;
    u64 m_nextCommentId = 1;
};

}  // namespace Luma::MaterialGraph
