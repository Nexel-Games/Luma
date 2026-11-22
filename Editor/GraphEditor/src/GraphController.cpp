#include "Luma/GraphEditor/GraphController.h"

#include <algorithm>

namespace Luma::GraphEditor {

GraphController::GraphController(IGraphModel& model, NodeMetrics metrics) : m_model(model), m_metrics(metrics) {}

void GraphController::Changed() {
    if (onChanged) onChanged();
}

void GraphController::Transact(const std::string& label, const std::function<void()>& edit) {
    std::any before = m_model.Snapshot();
    edit();
    m_undo.Push(label, std::move(before), m_model.Snapshot());
    Changed();
}

void GraphController::SelectOnly(NodeId id) {
    m_selection.Set({id});
    if (onSelectionChanged) onSelectionChanged();
}

void GraphController::ClearSelection() {
    if (m_selection.Empty()) return;
    m_selection.Clear();
    if (onSelectionChanged) onSelectionChanged();
}

ConnectCheck GraphController::CheckLink(PinKey a, PinKey b) const {
    if (a.dir == b.dir) return {ConnectResponse::Disallow, "Directions are not compatible"};
    if (a.node == b.node) return {ConnectResponse::Disallow, "Both are on the same node"};
    const PinKey out = a.dir == PinDir::Output ? a : b;
    const PinKey in = a.dir == PinDir::Output ? b : a;
    return m_model.CanConnect(out, in);
}

ConnectCheck GraphController::TryLink(PinKey a, PinKey b) {
    const ConnectCheck check = CheckLink(a, b);
    if (!check.Allowed()) return check;
    const PinKey out = a.dir == PinDir::Output ? a : b;
    const PinKey in = a.dir == PinDir::Output ? b : a;
    Transact("Make Link", [&] { m_model.Connect(out, in); });
    return check;
}

void GraphController::BreakPin(PinKey pin) {
    Transact("Break Link", [&] { m_model.BreakLinks(pin); });
}

NodeId GraphController::InsertReroute(const LinkView& link, Vec2 position) {
    NodeId r = 0;
    Transact("Add Reroute", [&] {
        const Vec2 half{m_metrics.rerouteSize * 0.5f, m_metrics.rerouteSize * 0.5f};
        r = m_model.AddReroute({position.x - half.x, position.y - half.y});
        if (r == 0) return;
        m_model.Connect(link.from, {r, PinDir::Input, 0});
        m_model.Connect({r, PinDir::Output, 0}, link.to);
    });
    return r;
}

NodeId GraphController::SpawnAction(const std::string& actionId, Vec2 position, std::optional<PinKey> from) {
    std::any before = m_model.Snapshot();
    const NodeId id = m_model.Spawn(actionId, position);
    if (id == 0) return 0;
    if (from) {
        for (const NodeView& n : m_model.Nodes()) {
            if (n.id != id) continue;
            // Link the first pin of the opposite direction the model accepts.
            const bool wantInput = from->dir == PinDir::Output;
            const usize count = wantInput ? n.inputs.size() : n.outputs.size();
            for (u32 i = 0; i < count; ++i) {
                const PinKey candidate{id, wantInput ? PinDir::Input : PinDir::Output, i};
                const PinKey out = wantInput ? *from : candidate;
                const PinKey in = wantInput ? candidate : *from;
                if (m_model.CanConnect(out, in).Allowed()) {
                    m_model.Connect(out, in);
                    break;
                }
            }
            break;
        }
    }
    m_undo.Push("Add Node", std::move(before), m_model.Snapshot());
    Changed();
    SelectOnly(id);
    return id;
}

NodeId GraphController::AddComment(Vec2 fallback, std::string text) {
    Rect bounds{fallback.x, fallback.y, 400.0f, 240.0f};
    bool any = false;
    for (const NodeView& n : m_model.Nodes()) {
        if (!m_selection.Has(n.id)) continue;
        const Rect b = LayoutNode(n, m_metrics).body;
        if (!any) {
            bounds = b;
            any = true;
        } else {
            const f32 x0 = std::min(bounds.x, b.x), y0 = std::min(bounds.y, b.y);
            const f32 x1 = std::max(bounds.Right(), b.Right()), y1 = std::max(bounds.Bottom(), b.Bottom());
            bounds = {x0, y0, x1 - x0, y1 - y0};
        }
    }
    if (any) {
        constexpr f32 pad = 24.0f;
        bounds = {bounds.x - pad, bounds.y - pad - m_metrics.headerH, bounds.w + 2 * pad,
                  bounds.h + 2 * pad + m_metrics.headerH};
    }
    NodeId id = 0;
    Transact("Add Comment", [&] { id = m_model.AddComment({bounds.x, bounds.y}, {bounds.w, bounds.h}, text); });
    if (id) SelectOnly(id);
    return id;
}

void GraphController::DeleteSelection() {
    if (m_selection.Empty()) return;
    const std::vector<NodeId> ids(m_selection.Nodes().begin(), m_selection.Nodes().end());
    Transact("Delete", [&] {
        for (NodeId id : ids)
            if (m_model.RemoveNode(id)) m_selection.Remove(id);
    });
    if (onSelectionChanged) onSelectionChanged();
}

std::vector<NodeId> GraphController::MoveSet() const {
    const std::vector<NodeView> nodes = m_model.Nodes();
    std::vector<NodeId> out(m_selection.Nodes().begin(), m_selection.Nodes().end());
    for (const NodeView& c : nodes) {
        if (c.kind != NodeKind::Comment || !m_selection.Has(c.id)) continue;
        const Rect cb = LayoutNode(c, m_metrics).body;
        for (const NodeView& n : nodes) {
            if (n.id == c.id || std::find(out.begin(), out.end(), n.id) != out.end()) continue;
            if (ContainsRect(cb, LayoutNode(n, m_metrics).body)) out.push_back(n.id);
        }
    }
    return out;
}

void GraphController::BeginMove() {
    m_moveBefore = m_model.Snapshot();
    m_moveDelta = {0, 0};
    m_moveStart.clear();
    const std::vector<NodeId> ids = MoveSet();
    for (const NodeView& n : m_model.Nodes())
        if (std::find(ids.begin(), ids.end(), n.id) != ids.end()) m_moveStart.push_back({n.id, n.position});
}

void GraphController::DragSelection(Vec2 d) {
    if (!m_moveBefore) BeginMove();
    m_moveDelta = {m_moveDelta.x + d.x, m_moveDelta.y + d.y};
    for (auto& [id, start] : m_moveStart) m_model.SetPosition(id, {start.x + m_moveDelta.x, start.y + m_moveDelta.y});
}

void GraphController::EndMove() {
    if (!m_moveBefore) return;
    if (m_moveDelta.x != 0.0f || m_moveDelta.y != 0.0f) {
        m_undo.Push("Move", std::move(*m_moveBefore), m_model.Snapshot());
        Changed();
    }
    m_moveBefore.reset();
    m_moveStart.clear();
}

std::string GraphController::CopySelection() const {
    std::vector<NodeId> ids;
    for (const NodeView& n : m_model.Nodes())
        if (m_selection.Has(n.id) && n.removable) ids.push_back(n.id);
    return ids.empty() ? std::string() : m_model.Copy(ids);
}

void GraphController::Paste(const std::string& text, Vec2 position) {
    if (text.empty()) return;
    std::vector<NodeId> added;
    std::any before = m_model.Snapshot();
    added = m_model.Paste(text, position);
    if (added.empty()) return;
    m_undo.Push("Paste", std::move(before), m_model.Snapshot());
    Changed();
    m_selection.Set(added);
    if (onSelectionChanged) onSelectionChanged();
}

void GraphController::Duplicate() {
    const std::string text = CopySelection();
    if (text.empty()) return;
    Vec2 tl{1e30f, 1e30f};
    for (const NodeView& n : m_model.Nodes())
        if (m_selection.Has(n.id)) tl = {std::min(tl.x, n.position.x), std::min(tl.y, n.position.y)};
    Paste(text, {tl.x + 30.0f, tl.y + 30.0f});
}

void GraphController::CutSelection(std::string& clipboard) {
    clipboard = CopySelection();
    DeleteSelection();
}

bool GraphController::Undo() {
    const std::any* s = m_undo.Undo();
    if (!s) return false;
    m_model.Restore(*s);
    m_selection.Prune(m_model.Nodes());
    Changed();
    return true;
}

bool GraphController::Redo() {
    const std::any* s = m_undo.Redo();
    if (!s) return false;
    m_model.Restore(*s);
    m_selection.Prune(m_model.Nodes());
    Changed();
    return true;
}

}  // namespace Luma::GraphEditor
