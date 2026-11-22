#pragma once

#include <map>
#include <string>

#include "Luma/GraphEditor/GraphModel.h"
#include "Luma/Material/GraphAssets.h"
#include "Luma/MaterialGraph/ExpressionRegistry.h"

// The material graph as a GraphEditor model (Unreal's UMaterialGraph +
// UMaterialGraphSchema roles): expression nodes from the ExpressionRegistry,
// pins typed by MaterialGraph::ValueType, links checked with the compiler's
// conversion rules, comments, reroute knots ("Reroute" expressions) and a
// JSON clipboard. Holds its own copy of the material; the editor pushes it to
// the MaterialLibrary on Apply.

namespace Luma::MaterialEditors {

using GraphEditor::NodeId;

// Comment ids are mapped into the node id space with this bit set.
inline constexpr NodeId kCommentBit = 1ull << 62;

// Can an output of type `from` feed an input of type `to`? (Any matches
// everything; Float1 broadcasts; wider floats truncate; bools and textures
// only match themselves.) `why` explains a refusal.
bool TypesCompatible(MaterialGraph::ValueType from, MaterialGraph::ValueType to, std::string* why = nullptr);
const char* TypeName(MaterialGraph::ValueType type);
Slate::Color TypeColor(MaterialGraph::ValueType type);

class MaterialGraphModel final : public GraphEditor::IGraphModel {
public:
    explicit MaterialGraphModel(Material::GraphMaterial material);

    const Material::GraphMaterial& Material() const { return m_material; }
    Material::GraphMaterial& MutableMaterial() { return m_material; }
    // Compile errors by node (0 = whole material), shown on the nodes.
    void SetErrors(std::map<MaterialGraph::NodeId, std::string> errors) { m_errors = std::move(errors); }

    // A node property edit (Details panel).
    bool SetProperty(MaterialGraph::NodeId node, const std::string& key, MaterialGraph::PropertyValue value);

    // IGraphModel
    std::vector<GraphEditor::NodeView> Nodes() const override;
    std::vector<GraphEditor::LinkView> Links() const override;
    GraphEditor::ConnectCheck CanConnect(GraphEditor::PinKey output, GraphEditor::PinKey input) const override;
    bool Connect(GraphEditor::PinKey output, GraphEditor::PinKey input) override;
    void BreakLinks(GraphEditor::PinKey pin) override;
    void SetPosition(NodeId node, GraphEditor::Vec2 position) override;
    void SetSize(NodeId comment, GraphEditor::Vec2 size) override;
    void SetCommentText(NodeId comment, std::string text) override;
    bool RemoveNode(NodeId node) override;
    std::vector<GraphEditor::GraphAction> Actions(const GraphEditor::PinKey* fromPin) const override;
    NodeId Spawn(const std::string& actionId, GraphEditor::Vec2 position) override;
    NodeId AddReroute(GraphEditor::Vec2 position) override;
    NodeId AddComment(GraphEditor::Vec2 position, GraphEditor::Vec2 size, std::string text) override;
    bool SetInlineValue(GraphEditor::PinKey pin, const std::string& text) override;
    std::any Snapshot() const override { return m_material; }
    void Restore(const std::any& snapshot) override;
    std::string Copy(const std::vector<NodeId>& nodes) const override;
    std::vector<NodeId> Paste(const std::string& text, GraphEditor::Vec2 topLeft) override;

    // Pin types of a node (input or output), Any when unknown. A Reroute
    // takes the type of whatever feeds it.
    MaterialGraph::ValueType PinType(GraphEditor::PinKey pin) const;

private:
    const MaterialGraph::ExpressionDesc* Desc(MaterialGraph::NodeId id) const;
    bool DependsOn(MaterialGraph::NodeId node, MaterialGraph::NodeId upstream) const;
    std::string UniqueParameterName(const std::string& base) const;

    Material::GraphMaterial m_material;
    std::map<MaterialGraph::NodeId, std::string> m_errors;
};

}  // namespace Luma::MaterialEditors
