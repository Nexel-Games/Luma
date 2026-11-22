#pragma once

#include <string>
#include <string_view>

#include "Luma/Core/Types.h"
#include "Luma/RHI/Renderer.h"
#include "Luma/Slate/Types.h"

// Slate drag and drop (Unreal FDragDropOperation, immediate-mode style).
//
// A source calls Context::BeginDrag every frame it is drawn, offering a
// payload; the drag starts once the button, pressed inside the source, has
// moved kStartDistance pixels. Targets call Context::AcceptDrop every frame:
// while a matching payload hovers them they are highlighted, and on release
// inside them the payload is returned exactly once. Releasing elsewhere or
// pressing Escape cancels. The Context draws the drag ghost on top of
// everything.

namespace Luma::Slate {

struct DragPayload {
    std::string type;   // e.g. "material", "mesh"
    std::string data;   // e.g. a virtual asset path
    std::string label;  // shown in the drag ghost
    // Absolute file or folder this drag came from, when it can be moved
    // between content folders. Empty for drags that only assign a reference.
    std::string sourcePath;
    TextureHandle icon = 0;
};

// The drag state machine the Context owns (input in, decisions out).
class DragDropState {
public:
    static constexpr f32 kStartDistance = 6.0f;

    // Called once per frame with the frame's (unblocked) pointer input.
    void BeginFrame(Vec2 mouse, bool pressed, bool down, bool released, bool escape);
    // A source offers `payload`; returns true while it is being dragged.
    bool Offer(u64 sourceId, const Rect& source, const DragPayload& payload);
    // A target: `hovered` reports a matching payload over `target`; returns
    // the payload on the release frame (once).
    const DragPayload* Accept(const Rect& target, std::string_view type, bool& hovered);
    void EndFrame();

    bool IsDragging(std::string_view type) const;
    bool Active() const { return m_phase == Phase::Dragging; }
    const DragPayload& Payload() const { return m_payload; }
    Vec2 Mouse() const { return m_mouse; }

private:
    enum class Phase { Idle, Pending, Dragging, Dropping };

    Phase m_phase = Phase::Idle;
    Vec2 m_mouse{};
    Vec2 m_pressPos{};
    bool m_pressHeld = false;  // button held since a press this drag can use
    bool m_down = false;
    bool m_delivered = false;
    u64 m_source = 0;
    DragPayload m_payload;
};

}  // namespace Luma::Slate
