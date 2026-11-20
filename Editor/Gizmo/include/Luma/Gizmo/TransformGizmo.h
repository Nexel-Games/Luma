#pragma once

#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/Math/Math.h"
#include "Luma/RHI/Renderer.h"

// Luma::Gizmo - the viewport transform gizmo (Unreal "widget"): translate,
// rotate and scale handles in world or local space, with grid snapping.
// The module owns only geometry + manipulation math: it emits overlay line
// vertices for the renderer and turns mouse drags into a new transform. It
// knows nothing about Vulkan, Slate or the editor's scene.

namespace Luma {

enum class GizmoMode : u8 { Translate, Rotate, Scale };
enum class GizmoSpace : u8 { World, Local };

// Which handle is hovered / dragged.
enum class GizmoHandle : u8 { None, X, Y, Z, XY, XZ, YZ, Uniform };

// Screen-space info the gizmo needs to project itself and hit-test the mouse.
struct GizmoInput {
    Math::Mat4 view;
    Math::Mat4 proj;
    f32 viewportX = 0, viewportY = 0, viewportW = 0, viewportH = 0;
    f32 mouseX = 0, mouseY = 0;
    f32 scale = 1.0f;  // world length of the axes (matches BuildLines)
    bool leftDown = false;
    bool leftPressed = false;
};

// The transform being edited (rotation as Euler degrees, applied Y * X * Z
// like TransformComponent).
struct GizmoTransform {
    Math::Vec3 position{0, 0, 0};
    Math::Vec3 rotationEuler{0, 0, 0};
    Math::Vec3 scale{1, 1, 1};
};

// Grid snapping (Unreal: location grid, rotation grid, scale grid).
struct GizmoSnap {
    bool translate = true;
    f32 translateStep = 0.1f;   // world units
    bool rotate = true;
    f32 rotateStepDeg = 10.0f;
    bool scale = true;
    f32 scaleStep = 0.25f;
};

class TransformGizmo {
public:
    void SetMode(GizmoMode mode) {
        if (!Dragging()) m_mode = mode;
    }
    GizmoMode Mode() const { return m_mode; }
    void SetSpace(GizmoSpace space) { m_space = space; }
    GizmoSpace Space() const { return m_space; }
    GizmoSnap& Snap() { return m_snap; }
    const GizmoSnap& Snap() const { return m_snap; }

    // Updates hover/drag against `current`. While a handle is dragged, writes
    // the manipulated transform to `out` and returns true.
    bool Update(const GizmoTransform& current, const GizmoInput& in, GizmoTransform& out);

    bool Dragging() const { return m_active != GizmoHandle::None; }
    GizmoHandle Hovered() const { return m_hovered; }

    // Overlay line geometry for the gizmo at `t`. `scale` is the world-space
    // axis length; `view` orients the rotate rings (back halves dimmed).
    const std::vector<LineVertex>& BuildLines(const GizmoTransform& t, f32 scale,
                                              const Math::Mat4& view);

    // --- Math helpers (exposed for tests) --------------------------------
    // Rotation matrix of Euler degrees, Y * X * Z order.
    static Math::Mat4 RotationFromEuler(const Math::Vec3& degrees);
    // Inverse of RotationFromEuler (degrees).
    static Math::Vec3 EulerFromRotation(const Math::Mat4& rotation);
    // Rodrigues rotation about a unit axis (radians).
    static Math::Mat4 AxisAngle(const Math::Vec3& axis, f32 radians);

private:
    // Handle axes for the current space (columns of the rotation for Local;
    // scale always uses local axes, like Unreal).
    void Axes(const GizmoTransform& t, Math::Vec3 out[3]) const;
    GizmoHandle HitTest(const GizmoTransform& t, const GizmoInput& in) const;

    GizmoMode m_mode = GizmoMode::Translate;
    GizmoSpace m_space = GizmoSpace::World;
    GizmoSnap m_snap;

    GizmoHandle m_hovered = GizmoHandle::None;
    GizmoHandle m_active = GizmoHandle::None;
    // Drag state captured on press.
    GizmoTransform m_start;
    Math::Vec3 m_startAxes[3];
    Math::Vec3 m_planeNormal{0, 1, 0};
    Math::Vec3 m_startHit{0, 0, 0};
    f32 m_startMouseX = 0, m_startMouseY = 0;
    f32 m_screenTangentX = 0, m_screenTangentY = 0;  // rotate fallback
    bool m_useScreenRotate = false;
    std::vector<LineVertex> m_lines;
};

}  // namespace Luma
