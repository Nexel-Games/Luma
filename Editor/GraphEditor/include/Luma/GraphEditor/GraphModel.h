#pragma once

#include <any>
#include <string>
#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/Slate/Types.h"

// The abstract graph a GraphEditor edits (Unreal's UEdGraph + UEdGraphSchema
// folded into one interface). The editor knows nothing about what the nodes
// mean: an adapter (e.g. the material graph) exposes nodes, typed pins and
// links as views, decides which links are legal (CanConnect) and which
// nodes can be created (Actions), and snapshots itself for undo.

namespace Luma::GraphEditor {

using Slate::Color;
using Slate::Rect;
using Slate::Vec2;
using NodeId = u64;

enum class PinDir : u8 { Input, Output };

struct PinKey {
    NodeId node = 0;
    PinDir dir = PinDir::Input;
    u32 index = 0;
    bool operator==(const PinKey&) const = default;
};

struct PinView {
    std::string name;
    Color color = Color::RGB(200, 200, 200);  // type colour
    std::string tooltip;
    std::string inlineValue;  // shown in a value box beside the pin ("0.5", "0, 0, 1")
    bool dimmed = false;      // unused by the current settings (drawn faded)
};

enum class NodeKind : u8 { Node, Reroute, Comment };

struct NodeView {
    NodeId id = 0;
    NodeKind kind = NodeKind::Node;
    std::string title;       // comments: their text
    std::string subtitle;    // e.g. a parameter name
    Vec2 position{0, 0};     // graph space, top-left
    Vec2 size{0, 0};         // comments only
    Color headerColor = Color::RGB(60, 60, 60);
    std::vector<PinView> inputs, outputs;
    bool removable = true;   // e.g. the material output node is not
    std::string error;       // compile error shown on the node
    bool hasPreview = false; // a colour swatch under the header (vector parameters)
    Color preview = Color::RGB(0, 0, 0);
};

struct LinkView {
    PinKey from;  // output pin
    PinKey to;    // input pin
    Color color = Color::RGB(200, 200, 200);
};

// UE's ECanCreateConnectionResponse, reduced to what the editor acts on.
enum class ConnectResponse : u8 { Make, Replace, Disallow };

struct ConnectCheck {
    ConnectResponse response = ConnectResponse::Disallow;
    std::string message;  // shown while dragging ("Replace existing link", why not)
    bool Allowed() const { return response != ConnectResponse::Disallow; }
};

// One entry of the context / palette menu.
struct GraphAction {
    std::string id;        // passed back to Spawn
    std::string category;  // "Math", "Parameters|Texture" ('|' nests)
    std::string label;
    std::string tooltip;
    std::string keywords;  // extra search terms
};

class IGraphModel {
public:
    virtual ~IGraphModel() = default;

    virtual std::vector<NodeView> Nodes() const = 0;
    virtual std::vector<LinkView> Links() const = 0;

    // `output` is an output pin, `input` an input pin (the controller orders them).
    virtual ConnectCheck CanConnect(PinKey output, PinKey input) const = 0;
    virtual bool Connect(PinKey output, PinKey input) = 0;
    // Input: removes its link. Output: removes every link from it.
    virtual void BreakLinks(PinKey pin) = 0;

    virtual void SetPosition(NodeId node, Vec2 position) = 0;
    virtual void SetSize(NodeId /*comment*/, Vec2 /*size*/) {}
    virtual void SetCommentText(NodeId /*comment*/, std::string /*text*/) {}
    virtual bool RemoveNode(NodeId node) = 0;

    // Creatable nodes; `fromPin` (may be null) filters for a drag-off.
    virtual std::vector<GraphAction> Actions(const PinKey* fromPin) const = 0;
    virtual NodeId Spawn(const std::string& actionId, Vec2 position) = 0;  // 0 on failure
    virtual NodeId AddReroute(Vec2 position) = 0;
    virtual NodeId AddComment(Vec2 position, Vec2 size, std::string text) = 0;
    // Commits a value typed into a pin's inline value box ("0.5", "0, 0, 1").
    virtual bool SetInlineValue(PinKey /*pin*/, const std::string& /*text*/) { return false; }

    // Whole-graph state for undo / redo.
    virtual std::any Snapshot() const = 0;
    virtual void Restore(const std::any& snapshot) = 0;
    // Clipboard text of `nodes` (and the links among them); Paste places
    // the copied block's top-left at `topLeft` and returns the new ids.
    virtual std::string Copy(const std::vector<NodeId>& nodes) const = 0;
    virtual std::vector<NodeId> Paste(const std::string& text, Vec2 topLeft) = 0;
};

}  // namespace Luma::GraphEditor
