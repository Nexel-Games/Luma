#pragma once

#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/Math/Math.h"
#include "Luma/RHI/Renderer.h"

// Immediate-mode debug shapes (lines, boxes, spheres, capsules) for the
// editor viewport and Play-in-Editor. Shapes are wireframe line lists in
// world space, either depth-tested against the scene or drawn on top
// (overlay). The host clears the process-wide drawer once per frame after
// the frame's scene view was built; systems and tools add shapes freely in
// between. Backends draw DepthTested lines into SceneView::lines and Overlay
// lines into SceneView::overlayLines.

namespace Luma {

class DebugDraw {
public:
    enum class Mode : u8 { DepthTested, Overlay };

    void Line(const Math::Vec3& a, const Math::Vec3& b, const Math::Vec3& color, Mode mode = Mode::DepthTested);
    // Axis-aligned box.
    void Box(const Math::Vec3& center, const Math::Vec3& halfExtents, const Math::Vec3& color,
             Mode mode = Mode::DepthTested);
    // Oriented box: `transform` places the box's local origin and axes.
    void OrientedBox(const Math::Mat4& transform, const Math::Vec3& halfExtents, const Math::Vec3& color,
             Mode mode = Mode::DepthTested);
    // Three great circles (XY, XZ, YZ).
    void Sphere(const Math::Vec3& center, f32 radius, const Math::Vec3& color, Mode mode = Mode::DepthTested,
                u32 segments = 24);
    // Rings at both ends, four side lines and the two hemisphere caps.
    void Capsule(const Math::Vec3& a, const Math::Vec3& b, f32 radius, const Math::Vec3& color,
                 Mode mode = Mode::DepthTested, u32 segments = 24);
    // A cone from `apex` opening along `dir` for `length`, with the given
    // half-angle: a base ring (radius length*tan(halfAngle)) plus four side
    // lines from the apex. Used for spot-light influence gizmos.
    void Cone(const Math::Vec3& apex, const Math::Vec3& dir, f32 length, f32 halfAngleDeg,
              const Math::Vec3& color, Mode mode = Mode::DepthTested, u32 segments = 24);
    // A view frustum from the inverse of a view-projection matrix: unprojects
    // the 8 clip-space corners (x,y in [-1,1], z in [0,1]) and draws the near
    // rect, far rect and four connecting edges. Handles perspective and
    // orthographic projections uniformly.
    void Frustum(const Math::Mat4& invViewProj, const Math::Vec3& color, Mode mode = Mode::DepthTested);

    // Vertex pairs (line list) for one mode.
    const std::vector<LineVertex>& Lines(Mode mode) const {
        return mode == Mode::Overlay ? m_overlay : m_depthTested;
    }
    void Clear() {
        m_depthTested.clear();
        m_overlay.clear();
    }

private:
    std::vector<LineVertex>& Target(Mode mode) { return mode == Mode::Overlay ? m_overlay : m_depthTested; }
    std::vector<LineVertex> m_depthTested;
    std::vector<LineVertex> m_overlay;
};

// The process-wide drawer (editor viewport + PIE systems).
DebugDraw& DebugDrawer();

}  // namespace Luma
