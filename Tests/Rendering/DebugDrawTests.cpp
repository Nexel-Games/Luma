#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "Luma/RHI/DebugDraw.h"

using namespace Luma;
using Catch::Approx;
using Mode = DebugDraw::Mode;

namespace {
f32 Dist(const Math::Vec3& a, const Math::Vec3& b) {
    const Math::Vec3 d = a - b;
    return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
}
}  // namespace

TEST_CASE("DebugDraw keeps depth-tested and overlay lines apart", "[rendering][debugdraw]") {
    DebugDraw dd;
    dd.Line({0, 0, 0}, {1, 0, 0}, {1, 0, 0});
    dd.Line({0, 0, 0}, {0, 1, 0}, {0, 1, 0}, Mode::Overlay);
    REQUIRE(dd.Lines(Mode::DepthTested).size() == 2);
    REQUIRE(dd.Lines(Mode::Overlay).size() == 2);
    CHECK(dd.Lines(Mode::DepthTested)[1].position.x == 1.0f);
    CHECK(dd.Lines(Mode::Overlay)[0].color.y == 1.0f);
    dd.Clear();
    CHECK(dd.Lines(Mode::DepthTested).empty());
    CHECK(dd.Lines(Mode::Overlay).empty());
}

TEST_CASE("DebugDraw boxes are 12 edges on their corners", "[rendering][debugdraw]") {
    DebugDraw dd;
    dd.Box({1, 2, 3}, {0.5f, 1.0f, 2.0f}, {1, 1, 1});
    const auto& v = dd.Lines(Mode::DepthTested);
    REQUIRE(v.size() == 24);
    for (const LineVertex& p : v) {
        CHECK(std::fabs(std::fabs(p.position.x - 1.0f) - 0.5f) < 1e-5f);
        CHECK(std::fabs(std::fabs(p.position.y - 2.0f) - 1.0f) < 1e-5f);
        CHECK(std::fabs(std::fabs(p.position.z - 3.0f) - 2.0f) < 1e-5f);
    }
    // Oriented box: a 90-degree yaw swaps the x / z extents.
    DebugDraw rotated;
    rotated.OrientedBox(Math::RotateY(Math::Radians(90.0f)), {2.0f, 0.5f, 0.25f}, {1, 1, 1});
    for (const LineVertex& p : rotated.Lines(Mode::DepthTested)) {
        CHECK(std::fabs(p.position.x) == Approx(0.25f).margin(1e-4));
        CHECK(std::fabs(p.position.z) == Approx(2.0f).margin(1e-4));
    }
}

TEST_CASE("DebugDraw spheres and capsules lie on their surfaces", "[rendering][debugdraw]") {
    DebugDraw dd;
    dd.Sphere({0, 1, 0}, 2.0f, {1, 1, 1}, Mode::DepthTested, 16);
    const auto& s = dd.Lines(Mode::DepthTested);
    REQUIRE(s.size() == 3u * 16u * 2u);  // three great circles
    for (const LineVertex& p : s) CHECK(Dist(p.position, {0, 1, 0}) == Approx(2.0f).margin(1e-4));

    DebugDraw cap;
    const Math::Vec3 a{0, 0, 0}, b{0, 3, 0};
    cap.Capsule(a, b, 0.5f, {1, 1, 1}, Mode::Overlay, 16);
    const auto& c = cap.Lines(Mode::Overlay);
    REQUIRE_FALSE(c.empty());
    CHECK(c.size() % 2 == 0);
    for (const LineVertex& p : c) {
        // Distance to the segment a-b is the radius.
        const f32 t = std::clamp(p.position.y / 3.0f, 0.0f, 1.0f);
        CHECK(Dist(p.position, {0, 3.0f * t, 0}) == Approx(0.5f).margin(1e-4));
    }
    CHECK(cap.Lines(Mode::DepthTested).empty());
}

TEST_CASE("DebugDraw cones are a base ring plus four side lines", "[rendering][debugdraw]") {
    DebugDraw dd;
    const Math::Vec3 apex{0, 0, 0};
    const Math::Vec3 dir{0, 0, -1};
    const f32 length = 2.0f;
    const f32 halfAngleDeg = 30.0f;
    dd.Cone(apex, dir, length, halfAngleDeg, {1, 1, 0}, Mode::Overlay, 24);
    const auto& v = dd.Lines(Mode::Overlay);
    // ring: 24 segments * 2 verts, + 4 side lines * 2 verts.
    REQUIRE(v.size() == (24u + 4u) * 2u);
    const Math::Vec3 baseCenter{0, 0, -length};
    const f32 baseRadius = length * std::tan(Math::Radians(halfAngleDeg));
    for (const LineVertex& p : v) {
        // Every vertex is either the apex or on the base ring.
        const bool atApex = Dist(p.position, apex) < 1e-4f;
        const bool onRing = std::fabs(p.position.z - baseCenter.z) < 1e-4f &&
                            std::fabs(Dist(p.position, baseCenter) - baseRadius) < 1e-3f;
        CHECK((atApex || onRing));
    }
    // Degenerate cone (zero length / angle) emits only finite coordinates.
    DebugDraw z;
    z.Cone(apex, dir, 0.0f, 0.0f, {1, 1, 1}, Mode::Overlay, 24);
    for (const LineVertex& p : z.Lines(Mode::Overlay)) {
        CHECK(std::isfinite(p.position.x));
        CHECK(std::isfinite(p.position.y));
        CHECK(std::isfinite(p.position.z));
    }
}

TEST_CASE("DebugDraw frustum unprojects clip corners into 12 edges", "[rendering][debugdraw]") {
    // Identity inverse-view-projection: corners are the clip cube itself
    // (x,y in [-1,1], z in [0,1]).
    DebugDraw dd;
    dd.Frustum(Math::Mat4::Identity(), {1, 1, 1}, Mode::Overlay);
    const auto& v = dd.Lines(Mode::Overlay);
    REQUIRE(v.size() == 12u * 2u);  // 12 edges
    for (const LineVertex& p : v) {
        CHECK(std::isfinite(p.position.x));
        CHECK(p.position.x == Approx(p.position.x < 0 ? -1.0f : 1.0f).margin(1e-4));
        CHECK(p.position.y == Approx(p.position.y < 0 ? -1.0f : 1.0f).margin(1e-4));
        CHECK(p.position.z >= -1e-4f);
        CHECK(p.position.z <= 1.0f + 1e-4f);
    }
    // A real perspective projection's inverse must stay finite (no NaNs even
    // for a singular projection, where Inverse() falls back to identity).
    DebugDraw persp;
    const Math::Mat4 proj = Math::Perspective(Math::Radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    persp.Frustum(Math::Inverse(proj), {1, 1, 1}, Mode::Overlay);
    const auto& pv = persp.Lines(Mode::Overlay);
    REQUIRE(pv.size() == 12u * 2u);
    for (const LineVertex& p : pv) CHECK(std::isfinite(p.position.z));
}

TEST_CASE("The process-wide DebugDraw is shared and clearable", "[rendering][debugdraw]") {
    DebugDrawer().Clear();
    DebugDrawer().Line({0, 0, 0}, {0, 0, 1}, {1, 1, 1});
    CHECK(DebugDrawer().Lines(Mode::DepthTested).size() == 2);
    DebugDrawer().Clear();
    CHECK(DebugDrawer().Lines(Mode::DepthTested).empty());
}
