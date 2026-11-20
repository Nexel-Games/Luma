#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "Luma/Gizmo/TransformGizmo.h"

using Catch::Approx;
using namespace Luma;
using Math::Vec3;

namespace {

// Camera looking at the origin from +Z (slightly above), 800x600 viewport.
GizmoInput MakeInput(f32 mx, f32 my) {
    GizmoInput in;
    in.view = Math::LookAt(Vec3(0, 2, 10), Vec3(0, 0, 0), Vec3(0, 1, 0));
    in.proj = Math::PerspectiveForViewport(0.9f, 800.0f, 600.0f, 0.1f, 500.0f);
    in.viewportX = 0;
    in.viewportY = 0;
    in.viewportW = 800;
    in.viewportH = 600;
    in.mouseX = mx;
    in.mouseY = my;
    in.scale = 2.0f;
    return in;
}

// Screen position of a world point under MakeInput's camera.
void Project(const Vec3& p, f32& x, f32& y) {
    GizmoInput in = MakeInput(0, 0);
    const Math::Mat4 vp = in.proj * in.view;
    const f32 cx = vp.m[0] * p.x + vp.m[4] * p.y + vp.m[8] * p.z + vp.m[12];
    const f32 cy = vp.m[1] * p.x + vp.m[5] * p.y + vp.m[9] * p.z + vp.m[13];
    const f32 cw = vp.m[3] * p.x + vp.m[7] * p.y + vp.m[11] * p.z + vp.m[15];
    x = (cx / cw * 0.5f + 0.5f) * 800.0f;
    y = (cy / cw * 0.5f + 0.5f) * 600.0f;
}

// Press on `from`, drag to `to`; returns the transform after the drag.
GizmoTransform Drag(TransformGizmo& g, const GizmoTransform& t, const Vec3& fromWorld,
                    const Vec3& toWorld) {
    f32 fx, fy, tx, ty;
    Project(fromWorld, fx, fy);
    Project(toWorld, tx, ty);
    GizmoTransform out = t;
    GizmoInput press = MakeInput(fx, fy);
    press.leftDown = true;
    press.leftPressed = true;
    g.Update(t, press, out);  // hover + begin
    REQUIRE(g.Dragging());
    GizmoInput move = MakeInput(tx, ty);
    move.leftDown = true;
    REQUIRE(g.Update(t, move, out));
    GizmoInput release = MakeInput(tx, ty);
    g.Update(out, release, out);
    CHECK_FALSE(g.Dragging());
    return out;
}

}  // namespace

TEST_CASE("Gizmo Euler <-> rotation round-trips (Y * X * Z)", "[gizmo]") {
    const Vec3 cases[] = {{0, 0, 0}, {30, 45, 60}, {-20, 170, 5}, {80, -35, -120}};
    for (const Vec3& e : cases) {
        const Vec3 back = TransformGizmo::EulerFromRotation(TransformGizmo::RotationFromEuler(e));
        const Math::Mat4 a = TransformGizmo::RotationFromEuler(e);
        const Math::Mat4 b = TransformGizmo::RotationFromEuler(back);
        for (int i = 0; i < 16; ++i) CHECK(a.m[i] == Approx(b.m[i]).margin(1e-4));
    }
}

TEST_CASE("Gizmo axis-angle matches the elementary rotations", "[gizmo]") {
    const f32 a = 0.7f;
    const Math::Mat4 y = TransformGizmo::AxisAngle(Vec3(0, 1, 0), a);
    const Math::Mat4 ry = Math::RotateY(a);
    for (int i = 0; i < 16; ++i) CHECK(y.m[i] == Approx(ry.m[i]).margin(1e-5));
}

TEST_CASE("Translate drag along X moves only X and snaps to the world grid", "[gizmo]") {
    TransformGizmo g;
    g.Snap().translate = true;
    g.Snap().translateStep = 0.5f;
    GizmoTransform t;
    t.position = Vec3(0.2f, 0.0f, 0.0f);
    const GizmoTransform out =
        Drag(g, t, Vec3(0.2f + 1.6f, 0, 0), Vec3(0.2f + 1.6f + 1.13f, 0, 0));
    CHECK(out.position.x == Approx(1.5f).margin(1e-4));  // 0.2 + ~1.13 -> 1.5 grid
    CHECK(out.position.y == Approx(0.0f).margin(1e-4));
    CHECK(out.position.z == Approx(0.0f).margin(1e-4));
}

TEST_CASE("Local-space translate follows the actor's rotated axis", "[gizmo]") {
    TransformGizmo g;
    g.SetSpace(GizmoSpace::Local);
    g.Snap().translate = false;
    GizmoTransform t;
    t.rotationEuler = Vec3(0, 0, 90);  // local +X points along world +Y
    const Vec3 localX(0, 1, 0);
    const GizmoTransform out = Drag(g, t, localX * 1.6f, localX * 2.6f);
    CHECK(out.position.y == Approx(1.0f).margin(0.02));
    CHECK(out.position.x == Approx(0.0f).margin(0.02));
    CHECK(out.position.z == Approx(0.0f).margin(0.02));
}

TEST_CASE("Rotate drag on the Y ring rotates about Y with snapping", "[gizmo]") {
    TransformGizmo g;
    g.SetMode(GizmoMode::Rotate);
    g.Snap().rotate = true;
    g.Snap().rotateStepDeg = 15.0f;
    GizmoTransform t;
    // Grab the Y ring (radius = scale 2) in front of the actor and drag ~40
    // degrees around it.
    const f32 a0 = 1.2f, a1 = a0 + Math::Radians(40.0f);
    const GizmoTransform out =
        Drag(g, t, Vec3(std::sin(a0) * 2.0f, 0, std::cos(a0) * 2.0f),
             Vec3(std::sin(a1) * 2.0f, 0, std::cos(a1) * 2.0f));
    CHECK(out.rotationEuler.x == Approx(0.0f).margin(1e-3));
    CHECK(out.rotationEuler.z == Approx(0.0f).margin(1e-3));
    CHECK(std::fabs(out.rotationEuler.y) == Approx(45.0f).margin(1e-3));  // 40 -> 45 snapped
}

TEST_CASE("Scale drag on an axis scales that component", "[gizmo]") {
    TransformGizmo g;
    g.SetMode(GizmoMode::Scale);
    g.Snap().scale = false;
    GizmoTransform t;
    // Axis length 2: dragging the X handle out by one axis length doubles X.
    const GizmoTransform out = Drag(g, t, Vec3(1.6f, 0, 0), Vec3(3.6f, 0, 0));
    CHECK(out.scale.x == Approx(2.0f).margin(0.02));
    CHECK(out.scale.y == Approx(1.0f));
    CHECK(out.scale.z == Approx(1.0f));
}

TEST_CASE("Gizmo mode cannot change mid-drag", "[gizmo]") {
    TransformGizmo g;
    GizmoTransform t;
    f32 x, y;
    Project(Vec3(1.6f, 0, 0), x, y);
    GizmoInput press = MakeInput(x, y);
    press.leftDown = press.leftPressed = true;
    GizmoTransform out;
    g.Update(t, press, out);
    REQUIRE(g.Dragging());
    g.SetMode(GizmoMode::Rotate);
    CHECK(g.Mode() == GizmoMode::Translate);
}

TEST_CASE("Edge-on plane handles never steal an axis grab", "[gizmo]") {
    // Local X along world +Y puts the local XZ plane edge-on to a camera on
    // +Z; grabbing the X axis must pick the axis, not the degenerate plane.
    TransformGizmo g;
    g.SetSpace(GizmoSpace::Local);
    GizmoTransform t;
    t.rotationEuler = Vec3(0, 0, 90);
    f32 x, y;
    Project(Vec3(0, 1.6f, 0), x, y);
    GizmoTransform out;
    g.Update(t, MakeInput(x, y), out);
    CHECK(g.Hovered() == GizmoHandle::X);
}
