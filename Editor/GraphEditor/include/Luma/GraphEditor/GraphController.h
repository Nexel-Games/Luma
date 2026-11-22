#pragma once

#include <functional>
#include <optional>
#include <string>

#include "Luma/GraphEditor/GraphMath.h"
#include "Luma/GraphEditor/GraphModel.h"
#include "Luma/GraphEditor/Selection.h"

namespace Luma::GraphEditor {

// Every editing operation of the graph editor, headless: the Slate widget
// translates input into these calls. Each mutating call is one undo
// transaction (snapshot before / after) and fires onChanged.
class GraphController {
public:
    explicit GraphController(IGraphModel& model, NodeMetrics metrics = {});

    IGraphModel& Model() { return m_model; }
    Selection& Sel() { return m_selection; }
    const Selection& Sel() const { return m_selection; }
    UndoStack& History() { return m_undo; }
    const NodeMetrics& Metrics() const { return m_metrics; }
    void SetMetrics(const NodeMetrics& metrics) { m_metrics = metrics; }

    // Fired after any change to the graph (not selection-only changes).
    std::function<void()> onChanged;
    // Fired when the selection changes through the controller.
    std::function<void()> onSelectionChanged;

    // --- Links ----------------------------------------------------------
    // Orders `a`/`b` into output -> input and asks the model.
    ConnectCheck CheckLink(PinKey a, PinKey b) const;
    // Makes the link when allowed (one transaction); returns the check.
    ConnectCheck TryLink(PinKey a, PinKey b);
    void BreakPin(PinKey pin);
    // Splits `link` with a reroute node at `position`; returns its id.
    NodeId InsertReroute(const LinkView& link, Vec2 position);

    // --- Nodes ----------------------------------------------------------
    // Spawns an action; with `from`, links the first compatible pin of the
    // new node to it (drag-off-pin). Selects the new node.
    NodeId SpawnAction(const std::string& actionId, Vec2 position, std::optional<PinKey> from = {});
    // A comment around the selection (or at `fallback` when empty).
    NodeId AddComment(Vec2 fallback, std::string text = "Comment");
    void DeleteSelection();

    // Moving: Begin snapshots, Drag moves the selection (plus nodes inside
    // selected comments), End commits one transaction if anything moved.
    void BeginMove();
    void DragSelection(Vec2 graphDelta);
    void EndMove();
    bool Moving() const { return m_moveBefore.has_value(); }

    // --- Clipboard ------------------------------------------------------
    std::string CopySelection() const;
    // Pastes with the block's top-left at `position`; selects the result.
    void Paste(const std::string& text, Vec2 position);
    void Duplicate();
    void CutSelection(std::string& clipboard);

    // --- Undo -----------------------------------------------------------
    bool Undo();
    bool Redo();

    // Runs `edit` as one undo transaction labelled `label`.
    void Transact(const std::string& label, const std::function<void()>& edit);

    // Selection helpers (fire onSelectionChanged).
    void SelectOnly(NodeId id);
    void ClearSelection();

private:
    // Nodes that a move of the selection carries (selected + inside selected comments).
    std::vector<NodeId> MoveSet() const;
    void Changed();

    IGraphModel& m_model;
    NodeMetrics m_metrics;
    Selection m_selection;
    UndoStack m_undo;
    std::optional<std::any> m_moveBefore;
    std::vector<std::pair<NodeId, Vec2>> m_moveStart;
    Vec2 m_moveDelta{0, 0};
};

}  // namespace Luma::GraphEditor
