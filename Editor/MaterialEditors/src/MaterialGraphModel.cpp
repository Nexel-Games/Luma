#include "Luma/MaterialEditors/MaterialGraphModel.h"

#include <algorithm>
#include <cstdio>
#include <set>

namespace Luma::MaterialEditors {

using GraphEditor::ConnectCheck;
using GraphEditor::ConnectResponse;
using GraphEditor::GraphAction;
using GraphEditor::LinkView;
using GraphEditor::NodeKind;
using GraphEditor::NodeView;
using GraphEditor::PinDir;
using GraphEditor::PinKey;
using GraphEditor::PinView;
using GraphEditor::Vec2;
using MaterialGraph::ExpressionDesc;
using MaterialGraph::ExpressionRegistry;
using MaterialGraph::ValueType;
using Slate::Color;

namespace {

bool IsFloat(ValueType t) { return t <= ValueType::Float4; }
int Width(ValueType t) { return static_cast<int>(t) + 1; }
bool IsComment(NodeId id) { return (id & kCommentBit) != 0; }
u64 CommentId(NodeId id) { return id & ~kCommentBit; }

Color CategoryColor(const std::string& category) {
    if (category.rfind("Parameters", 0) == 0) return Color::RGB(64, 112, 64);
    if (category.rfind("Constants", 0) == 0) return Color::RGB(58, 92, 58);
    if (category.rfind("Texture", 0) == 0) return Color::RGB(52, 82, 128);
    if (category.rfind("Math", 0) == 0) return Color::RGB(72, 82, 96);
    if (category.rfind("Coordinates", 0) == 0) return Color::RGB(124, 70, 48);
    if (category.rfind("Functions", 0) == 0) return Color::RGB(40, 104, 112);
    return Color::RGB(78, 78, 78);
}

Color ToColor(const Math::Vec4& c) {
    auto c8 = [](f32 x) { return static_cast<u8>(std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return Color::RGBA(c8(c.x), c8(c.y), c8(c.z), c8(c.w));
}

std::string ShortValue(const MaterialGraph::PropertyValue& v) {
    char buf[64];
    if (v.IsFloat()) {
        std::snprintf(buf, sizeof(buf), "%g", v.AsFloat());
        return buf;
    }
    if (v.IsVec4()) {
        const Math::Vec4 x = v.AsVec4();
        std::snprintf(buf, sizeof(buf), "%.2g, %.2g, %.2g", x.x, x.y, x.z);
        return buf;
    }
    return {};
}

}  // namespace

const char* TypeName(ValueType t) {
    switch (t) {
        case ValueType::Float1: return "Float1";
        case ValueType::Float2: return "Float2";
        case ValueType::Float3: return "Float3";
        case ValueType::Float4: return "Float4";
        case ValueType::Bool: return "Bool";
        case ValueType::Texture2D: return "Texture2D";
        case ValueType::TextureCube: return "TextureCube";
        case ValueType::Any: return "Any";
    }
    return "?";
}

Color TypeColor(ValueType t) {
    switch (t) {
        case ValueType::Float1: return Color::RGB(160, 160, 160);
        case ValueType::Float2: return Color::RGB(120, 200, 110);
        case ValueType::Float3: return Color::RGB(225, 205, 90);
        case ValueType::Float4: return Color::RGB(220, 120, 200);
        case ValueType::Bool: return Color::RGB(200, 70, 70);
        case ValueType::Texture2D:
        case ValueType::TextureCube: return Color::RGB(90, 150, 240);
        case ValueType::Any: return Color::RGB(230, 230, 230);
    }
    return Color::RGB(200, 200, 200);
}

bool TypesCompatible(ValueType from, ValueType to, std::string* why) {
    bool ok = false;
    if (from == ValueType::Any || to == ValueType::Any || from == to) {
        ok = true;
    } else if (IsFloat(from) && IsFloat(to)) {
        ok = from == ValueType::Float1 || Width(from) > Width(to);  // broadcast / truncate
    }
    if (!ok && why) *why = std::string(TypeName(from)) + " can't connect to " + TypeName(to);
    return ok;
}

MaterialGraphModel::MaterialGraphModel(Material::GraphMaterial material) : m_material(std::move(material)) {}

const ExpressionDesc* MaterialGraphModel::Desc(MaterialGraph::NodeId id) const {
    const MaterialGraph::Node* n = m_material.graph.Find(id);
    return n ? ExpressionRegistry::Get().Find(n->type) : nullptr;
}

ValueType MaterialGraphModel::PinType(PinKey pin) const {
    const MaterialGraph::Node* n = m_material.graph.Find(pin.node);
    const ExpressionDesc* d = Desc(pin.node);
    if (!n || !d) return ValueType::Any;
    if (n->type == "Reroute") {
        // A knot carries its source's type (walk chains of knots).
        std::set<MaterialGraph::NodeId> seen;
        MaterialGraph::NodeId cur = pin.node;
        while (seen.insert(cur).second) {
            auto src = m_material.graph.Source({cur, 0});
            if (!src) return ValueType::Any;
            const MaterialGraph::Node* s = m_material.graph.Find(src->node);
            if (!s) return ValueType::Any;
            if (s->type != "Reroute") return PinType({src->node, PinDir::Output, src->pin});
            cur = src->node;
        }
        return ValueType::Any;
    }
    const auto& pins = pin.dir == PinDir::Input ? d->inputs : d->outputs;
    return pin.index < pins.size() ? pins[pin.index].type : ValueType::Any;
}

std::vector<NodeView> MaterialGraphModel::Nodes() const {
    std::vector<NodeView> out;
    const ExpressionRegistry& reg = ExpressionRegistry::Get();
    for (const MaterialGraph::Comment& c : m_material.graph.Comments()) {
        NodeView v;
        v.id = kCommentBit | c.id;
        v.kind = NodeKind::Comment;
        v.title = c.text;
        v.position = {c.position.x, c.position.y};
        v.size = {c.size.x, c.size.y};
        v.headerColor = ToColor(c.color);
        out.push_back(std::move(v));
    }
    for (const MaterialGraph::Node& n : m_material.graph.Nodes()) {
        NodeView v;
        v.id = n.id;
        v.position = {n.position.x, n.position.y};
        const ExpressionDesc* d = reg.Find(n.type);
        if (!d) {
            v.title = n.type;
            v.error = "Unknown expression type '" + n.type + "'";
            out.push_back(std::move(v));
            continue;
        }
        const bool isOutput = n.id == m_material.graph.OutputNode();
        v.kind = n.type == "Reroute" ? NodeKind::Reroute : NodeKind::Node;
        v.title = isOutput ? (m_material.name.empty() ? std::string("Material") : m_material.name) : d->displayName;
        v.removable = !isOutput;
        v.headerColor = isOutput ? Color::RGB(110, 80, 50) : CategoryColor(d->category);
        if (auto it = n.properties.find("name"); it != n.properties.end() && it->second.IsString()) {
            v.subtitle = it->second.AsString();
        } else if (n.type.rfind("Constant", 0) == 0) {
            if (auto it2 = n.properties.find("value"); it2 != n.properties.end()) v.subtitle = ShortValue(it2->second);
        }
        for (u32 i = 0; i < d->inputs.size(); ++i) {
            const ValueType t = PinType({n.id, PinDir::Input, i});
            v.inputs.push_back({d->inputs[i].name, TypeColor(t),
                                std::string(TypeName(t)) + (d->inputs[i].tooltip.empty() ? "" : "\n" + d->inputs[i].tooltip)});
        }
        for (u32 i = 0; i < d->outputs.size(); ++i) {
            const ValueType t = PinType({n.id, PinDir::Output, i});
            v.outputs.push_back({d->outputs[i].name, TypeColor(t), TypeName(t)});
        }
        if (auto it = m_errors.find(n.id); it != m_errors.end()) v.error = it->second;
        out.push_back(std::move(v));
    }
    return out;
}

std::vector<LinkView> MaterialGraphModel::Links() const {
    std::vector<LinkView> out;
    for (const MaterialGraph::Link& l : m_material.graph.Links()) {
        LinkView v;
        v.from = {l.from.node, PinDir::Output, l.from.pin};
        v.to = {l.to.node, PinDir::Input, l.to.pin};
        v.color = TypeColor(PinType(v.from));
        out.push_back(v);
    }
    return out;
}

bool MaterialGraphModel::DependsOn(MaterialGraph::NodeId node, MaterialGraph::NodeId upstream) const {
    // Is `upstream` reachable walking inputs from `node`?
    std::vector<MaterialGraph::NodeId> stack{node};
    std::set<MaterialGraph::NodeId> seen;
    while (!stack.empty()) {
        const MaterialGraph::NodeId cur = stack.back();
        stack.pop_back();
        if (cur == upstream) return true;
        if (!seen.insert(cur).second) continue;
        for (const MaterialGraph::Link& l : m_material.graph.Links())
            if (l.to.node == cur) stack.push_back(l.from.node);
    }
    return false;
}

ConnectCheck MaterialGraphModel::CanConnect(PinKey o, PinKey i) const {
    if (IsComment(o.node) || IsComment(i.node)) return {ConnectResponse::Disallow, "Comments have no pins"};
    const ExpressionDesc* od = Desc(o.node);
    const ExpressionDesc* id = Desc(i.node);
    if (!od || !id) return {ConnectResponse::Disallow, "Unknown node"};
    if (o.node == i.node) return {ConnectResponse::Disallow, "Both are on the same node"};
    if (o.index >= od->outputs.size() || i.index >= id->inputs.size()) return {ConnectResponse::Disallow, "No such pin"};
    if (DependsOn(o.node, i.node)) return {ConnectResponse::Disallow, "Connection would create a cycle"};
    std::string why;
    if (!TypesCompatible(PinType(o), PinType(i), &why)) return {ConnectResponse::Disallow, why};
    if (m_material.graph.Source({i.node, i.index})) return {ConnectResponse::Replace, "Replace existing link"};
    return {ConnectResponse::Make, ""};
}

bool MaterialGraphModel::Connect(PinKey o, PinKey i) {
    if (!CanConnect(o, i).Allowed()) return false;
    return m_material.graph.Connect({o.node, o.index}, {i.node, i.index}, nullptr);
}

void MaterialGraphModel::BreakLinks(PinKey pin) {
    if (pin.dir == PinDir::Input) {
        m_material.graph.Disconnect({pin.node, pin.index});
        return;
    }
    std::vector<MaterialGraph::PinRef> targets;
    for (const MaterialGraph::Link& l : m_material.graph.Links())
        if (l.from.node == pin.node && l.from.pin == pin.index) targets.push_back(l.to);
    for (const auto& t : targets) m_material.graph.Disconnect(t);
}

void MaterialGraphModel::SetPosition(NodeId id, Vec2 p) {
    if (IsComment(id)) {
        for (auto& c : m_material.graph.Comments())
            if (c.id == CommentId(id)) c.position = {p.x, p.y};
        return;
    }
    if (MaterialGraph::Node* n = m_material.graph.Find(id)) n->position = {p.x, p.y};
}

void MaterialGraphModel::SetSize(NodeId id, Vec2 s) {
    for (auto& c : m_material.graph.Comments())
        if (IsComment(id) && c.id == CommentId(id)) c.size = {s.x, s.y};
}

void MaterialGraphModel::SetCommentText(NodeId id, std::string text) {
    for (auto& c : m_material.graph.Comments())
        if (IsComment(id) && c.id == CommentId(id)) c.text = text;
}

bool MaterialGraphModel::RemoveNode(NodeId id) {
    if (IsComment(id)) {
        auto& cs = m_material.graph.Comments();
        const auto before = cs.size();
        std::erase_if(cs, [&](const MaterialGraph::Comment& c) { return c.id == CommentId(id); });
        return cs.size() != before;
    }
    return m_material.graph.RemoveNode(id);
}

bool MaterialGraphModel::SetProperty(MaterialGraph::NodeId id, const std::string& key, MaterialGraph::PropertyValue value) {
    MaterialGraph::Node* n = m_material.graph.Find(id);
    if (!n) return false;
    n->properties[key] = std::move(value);
    return true;
}

std::vector<GraphAction> MaterialGraphModel::Actions(const PinKey* from) const {
    std::vector<GraphAction> out;
    std::optional<ValueType> fromType;
    if (from && !IsComment(from->node)) fromType = PinType(*from);
    for (const ExpressionDesc* d : ExpressionRegistry::Get().All()) {
        if (d->hiddenInPalette) continue;
        if (fromType) {
            const bool wantInput = from->dir == PinDir::Output;
            const auto& pins = wantInput ? d->inputs : d->outputs;
            const bool any = std::any_of(pins.begin(), pins.end(), [&](const MaterialGraph::PinDesc& p) {
                return wantInput ? TypesCompatible(*fromType, p.type) : TypesCompatible(p.type, *fromType);
            });
            if (!any) continue;
        }
        std::string keywords;
        for (const std::string& k : d->keywords) keywords += (keywords.empty() ? "" : " ") + k;
        out.push_back({d->type, d->category, d->displayName, d->description, keywords});
    }
    return out;
}

std::string MaterialGraphModel::UniqueParameterName(const std::string& base) const {
    std::set<std::string> used;
    for (const MaterialGraph::Node& n : m_material.graph.Nodes())
        if (auto it = n.properties.find("name"); it != n.properties.end()) used.insert(it->second.AsString());
    if (!used.count(base)) return base;
    for (int i = 1;; ++i) {
        const std::string candidate = base + "_" + std::to_string(i);
        if (!used.count(candidate)) return candidate;
    }
}

NodeId MaterialGraphModel::Spawn(const std::string& type, Vec2 p) {
    const ExpressionDesc* d = ExpressionRegistry::Get().Find(type);
    if (!d || type == MaterialGraph::Graph::kOutputType) return 0;
    const NodeId id = m_material.graph.AddNode(type, {p.x, p.y});
    for (const MaterialGraph::PropertyDesc& prop : d->properties) {
        if (prop.key == "name") {
            m_material.graph.Find(id)->properties["name"] =
                MaterialGraph::PropertyValue(UniqueParameterName(prop.defaultValue.AsString()));
        }
    }
    return id;
}

NodeId MaterialGraphModel::AddReroute(Vec2 p) { return m_material.graph.AddNode("Reroute", {p.x, p.y}); }

NodeId MaterialGraphModel::AddComment(Vec2 p, Vec2 s, std::string text) {
    return kCommentBit | m_material.graph.AddComment({p.x, p.y}, {s.x, s.y}, std::move(text));
}

void MaterialGraphModel::Restore(const std::any& snapshot) {
    if (const auto* m = std::any_cast<Material::GraphMaterial>(&snapshot)) m_material = *m;
}

std::string MaterialGraphModel::Copy(const std::vector<NodeId>& ids) const {
    // Copied nodes get fresh ids above the scratch graph's own output node.
    MaterialGraph::Graph scratch;
    std::map<MaterialGraph::NodeId, MaterialGraph::NodeId> remap;
    MaterialGraph::NodeId next = scratch.NextId() + 1;
    for (NodeId id : ids) {
        if (IsComment(id)) {
            for (const auto& c : m_material.graph.Comments()) {
                if (c.id != CommentId(id)) continue;
                const u64 cid = scratch.AddComment(c.position, c.size, c.text);
                for (auto& sc : scratch.Comments())
                    if (sc.id == cid) sc.color = c.color;
            }
            continue;
        }
        const MaterialGraph::Node* n = m_material.graph.Find(id);
        if (!n || id == m_material.graph.OutputNode()) continue;
        const MaterialGraph::NodeId nid = next++;
        scratch.AddNodeWithId(nid, n->type, n->position);
        MaterialGraph::Node* sn = scratch.Find(nid);
        sn->properties = n->properties;
        sn->comment = n->comment;
        remap[id] = nid;
    }
    for (const MaterialGraph::Link& l : m_material.graph.Links()) {
        auto f = remap.find(l.from.node), t = remap.find(l.to.node);
        if (f != remap.end() && t != remap.end()) scratch.Connect({f->second, l.from.pin}, {t->second, l.to.pin}, nullptr);
    }
    return Material::GraphToJsonText(scratch);
}

std::vector<NodeId> MaterialGraphModel::Paste(const std::string& text, Vec2 topLeft) {
    MaterialGraph::Graph scratch;
    if (!Material::GraphFromJsonText(text, scratch, nullptr)) return {};
    // Block origin: the top-left of everything pasted.
    f32 minX = 1e30f, minY = 1e30f;
    for (const auto& n : scratch.Nodes()) {
        if (n.id == scratch.OutputNode() || n.type == MaterialGraph::Graph::kOutputType) continue;
        minX = std::min(minX, n.position.x);
        minY = std::min(minY, n.position.y);
    }
    for (const auto& c : scratch.Comments()) {
        minX = std::min(minX, c.position.x);
        minY = std::min(minY, c.position.y);
    }
    if (minX > 1e29f) return {};
    const Math::Vec2 shift{topLeft.x - minX, topLeft.y - minY};
    std::map<MaterialGraph::NodeId, MaterialGraph::NodeId> remap;
    std::vector<NodeId> added;
    for (const auto& n : scratch.Nodes()) {
        if (n.type == MaterialGraph::Graph::kOutputType) continue;
        const MaterialGraph::NodeId id = m_material.graph.AddNode(n.type, {n.position.x + shift.x, n.position.y + shift.y});
        MaterialGraph::Node* dst = m_material.graph.Find(id);
        dst->properties = n.properties;
        dst->comment = n.comment;
        remap[n.id] = id;
        added.push_back(id);
    }
    for (const auto& l : scratch.Links()) {
        auto f = remap.find(l.from.node), t = remap.find(l.to.node);
        if (f != remap.end() && t != remap.end())
            m_material.graph.Connect({f->second, l.from.pin}, {t->second, l.to.pin}, nullptr);
    }
    for (const auto& c : scratch.Comments()) {
        const u64 cid =
            m_material.graph.AddComment({c.position.x + shift.x, c.position.y + shift.y}, c.size, c.text);
        for (auto& dc : m_material.graph.Comments())
            if (dc.id == cid) dc.color = c.color;
        added.push_back(kCommentBit | cid);
    }
    return added;
}

}  // namespace Luma::MaterialEditors
