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

TEST_CASE("The process-wide DebugDraw is shared and clearable", "[rendering][debugdraw]") {
    DebugDrawer().Clear();
    DebugDrawer().Line({0, 0, 0}, {0, 0, 1}, {1, 1, 1});
    CHECK(DebugDrawer().Lines(Mode::DepthTested).size() == 2);
    DebugDrawer().Clear();
    CHECK(DebugDrawer().Lines(Mode::DepthTested).empty());
}
