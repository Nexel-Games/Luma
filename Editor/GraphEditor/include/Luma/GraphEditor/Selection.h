#pragma once

#include <any>
#include <deque>
#include <set>
#include <string>
#include <vector>

#include "Luma/GraphEditor/GraphModel.h"

namespace Luma::GraphEditor {

// FMarqueeOperation's modes: plain drag replaces, Shift adds, Alt removes,
// Ctrl inverts.
enum class MarqueeOp : u8 { Replace, Add, Remove, Invert };
MarqueeOp MarqueeOpFromModifiers(bool ctrl, bool shift, bool alt);

class Selection {
public:
    const std::set<NodeId>& Nodes() const { return m_nodes; }
    bool Has(NodeId id) const { return m_nodes.count(id) != 0; }
    bool Empty() const { return m_nodes.empty(); }
    void Clear() { m_nodes.clear(); }
    void Set(std::vector<NodeId> ids) { m_nodes = {ids.begin(), ids.end()}; }
    void Add(NodeId id) { m_nodes.insert(id); }
    void Remove(NodeId id) { m_nodes.erase(id); }
    void Toggle(NodeId id);
    // `hits` = items under the marquee; `before` = the selection when the
    // marquee started (the marquee is re-applied every frame).
    void ApplyMarquee(const std::vector<NodeId>& hits, MarqueeOp op, const std::set<NodeId>& before);
    // Drops ids no longer in the graph.
    void Prune(const std::vector<NodeView>& nodes);

private:
    std::set<NodeId> m_nodes;
};

// Snapshot-based undo (each entry = the model state before and after).
class UndoStack {
public:
    explicit UndoStack(usize maxDepth = 128) : m_max(maxDepth) {}
    void Push(std::string label, std::any before, std::any after);
    bool CanUndo() const { return m_cursor > 0; }
    bool CanRedo() const { return m_cursor < m_entries.size(); }
    // The state to restore, or null.
    const std::any* Undo();
    const std::any* Redo();
    std::string UndoLabel() const { return CanUndo() ? m_entries[m_cursor - 1].label : std::string(); }
    std::string RedoLabel() const { return CanRedo() ? m_entries[m_cursor].label : std::string(); }
    usize Size() const { return m_entries.size(); }
    void Clear() {
        m_entries.clear();
        m_cursor = 0;
    }

private:
    struct Entry {
        std::string label;
        std::any before, after;
    };
    std::deque<Entry> m_entries;
    usize m_cursor = 0;  // entries before the cursor are undoable
    usize m_max;
};

}  // namespace Luma::GraphEditor
