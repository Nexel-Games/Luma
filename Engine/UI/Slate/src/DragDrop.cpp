#include "Luma/Slate/DragDrop.h"

#include <cmath>

namespace Luma::Slate {

void DragDropState::BeginFrame(Vec2 mouse, bool pressed, bool down, bool released,
                               bool escape) {
    m_mouse = mouse;
    m_down = down;
    if (pressed) {
        m_pressPos = mouse;
        m_pressHeld = true;
        m_phase = Phase::Idle;
    }
    if (escape && (m_phase == Phase::Dragging || m_phase == Phase::Pending)) {
        // Cancelled: nothing more until the button is released and pressed again.
        m_phase = Phase::Idle;
        m_pressHeld = false;
    }
    if (released) {
        m_phase = m_phase == Phase::Dragging ? Phase::Dropping : Phase::Idle;
        m_delivered = false;
        m_pressHeld = false;
    }
}

bool DragDropState::Offer(u64 sourceId, const Rect& source, const DragPayload& payload) {
    if (m_phase == Phase::Dragging) return m_source == sourceId;
    if (m_phase == Phase::Dropping || !m_pressHeld || !m_down) return false;
    if (!source.Contains(m_pressPos)) return false;
    if (m_phase == Phase::Idle || m_source != sourceId) {
        m_source = sourceId;
        m_phase = Phase::Pending;
    }
    m_payload = payload;
    const f32 dx = m_mouse.x - m_pressPos.x;
    const f32 dy = m_mouse.y - m_pressPos.y;
    if (std::sqrt(dx * dx + dy * dy) >= kStartDistance) {
        m_phase = Phase::Dragging;
        return true;
    }
    return false;
}

const DragPayload* DragDropState::Accept(const Rect& target, std::string_view type,
                                         bool& hovered) {
    hovered = false;
    if (m_phase != Phase::Dragging && m_phase != Phase::Dropping) return nullptr;
    if (m_payload.type != type || !target.Contains(m_mouse)) return nullptr;
    hovered = true;
    if (m_phase != Phase::Dropping || m_delivered) return nullptr;
    m_delivered = true;
    return &m_payload;
}

void DragDropState::EndFrame() {
    if (m_phase == Phase::Dropping) m_phase = Phase::Idle;
    if (m_phase == Phase::Pending && !m_down) m_phase = Phase::Idle;
}

bool DragDropState::IsDragging(std::string_view type) const {
    return m_phase == Phase::Dragging && m_payload.type == type;
}

}  // namespace Luma::Slate
