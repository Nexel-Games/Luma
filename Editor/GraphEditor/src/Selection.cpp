#include "Luma/GraphEditor/Selection.h"

#include <algorithm>

namespace Luma::GraphEditor {

MarqueeOp MarqueeOpFromModifiers(bool ctrl, bool shift, bool alt) {
    if (ctrl) return MarqueeOp::Invert;
    if (shift) return MarqueeOp::Add;
    if (alt) return MarqueeOp::Remove;
    return MarqueeOp::Replace;
}

void Selection::Toggle(NodeId id) {
    if (!m_nodes.erase(id)) m_nodes.insert(id);
}

void Selection::ApplyMarquee(const std::vector<NodeId>& hits, MarqueeOp op, const std::set<NodeId>& before) {
    switch (op) {
        case MarqueeOp::Replace: m_nodes = {hits.begin(), hits.end()}; break;
        case MarqueeOp::Add:
            m_nodes = before;
            m_nodes.insert(hits.begin(), hits.end());
            break;
        case MarqueeOp::Remove:
            m_nodes = before;
            for (NodeId id : hits) m_nodes.erase(id);
            break;
        case MarqueeOp::Invert:
            m_nodes = before;
            for (NodeId id : hits) Toggle(id);
            break;
    }
}

void Selection::Prune(const std::vector<NodeView>& nodes) {
    std::erase_if(m_nodes, [&](NodeId id) {
        return std::none_of(nodes.begin(), nodes.end(), [&](const NodeView& n) { return n.id == id; });
    });
}

void UndoStack::Push(std::string label, std::any before, std::any after) {
    m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(m_cursor), m_entries.end());
    m_entries.push_back({std::move(label), std::move(before), std::move(after)});
    while (m_entries.size() > m_max) m_entries.pop_front();
    m_cursor = m_entries.size();
}

const std::any* UndoStack::Undo() {
    if (!CanUndo()) return nullptr;
    return &m_entries[--m_cursor].before;
}

const std::any* UndoStack::Redo() {
    if (!CanRedo()) return nullptr;
    return &m_entries[m_cursor++].after;
}

}  // namespace Luma::GraphEditor
