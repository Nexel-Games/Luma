#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <optional>

#include "Luma/Editor/Panels/MeshPlacement.h"

using Catch::Approx;
using namespace Luma;
using Luma::Editor::Panels::ComputeMeshPlacement;
using Luma::Editor::Panels::MeshPlacementInput;
using Luma::Editor::Panels::PlacementHit;
using Luma::Editor::Panels::RayAabb;

TEST_CASE("A mesh rests its bottom on the surface under the cursor", "[viewport][placement]") {
    MeshPlacementInput in;
    in.rayOrigin = {0, 5, 0};
    in.rayDir = {0, -1, 0};
    in.hit = PlacementHit{{2.0f, 1.5f, -3.0f}, {0, 1, 0}};
    in.boundsMin = {-0.5f, -1.0f, -0.5f};  // pivot at the mesh's centre
    in.boundsMax = {0.5f, 1.0f, 0.5f};
    const Math::Vec3 p = ComputeMeshPlacement(in);
    CHECK(p.x == Approx(2.0f));
    CHECK(p.y == Approx(2.5f));  // 1.5 + 1 so the bottom touches the surface
    CHECK(p.z == Approx(-3.0f));

    in.boundsMin.y = 0.0f;  // pivot at the base
    CHECK(ComputeMeshPlacement(in).y == Approx(1.5f));
}

TEST_CASE("Without a hit the mesh lands on the ground plane", "[viewport][placement]") {
    MeshPlacementInput in;
    in.rayOrigin = {0, 4, 0};
    in.rayDir = Math::Normalize(Math::Vec3(1, -1, 0));
    in.boundsMin = {-1, -0.25f, -1};
    in.boundsMax = {1, 0.25f, 1};
    const Math::Vec3 p = ComputeMeshPlacement(in);
    CHECK(p.x == Approx(4.0f));
    CHECK(p.y == Approx(0.25f));
    CHECK(p.z == Approx(0.0f).margin(1e-5));

    // Looking at the sky: a point in front of the camera.
    in.rayDir = {0, 0, -1};
    const Math::Vec3 q = ComputeMeshPlacement(in);
    CHECK(q.z == Approx(-10.0f));
    CHECK(q.y == Approx(4.25f));
}

TEST_CASE("Placement snaps to the location grid", "[viewport][placement]") {
    MeshPlacementInput in;
    in.rayOrigin = {0, 5, 0};
    in.rayDir = {0, -1, 0};
    in.hit = PlacementHit{{1.37f, 0.33f, -0.62f}, {0, 1, 0}};
    in.boundsMin = {-0.5f, 0, -0.5f};
    in.boundsMax = {0.5f, 1, 0.5f};
    in.snap = true;
    in.snapStep = 0.25f;
    const Math::Vec3 p = ComputeMeshPlacement(in);
    CHECK(p.x == Approx(1.25f));
    CHECK(p.z == Approx(-0.5f));
    CHECK(p.y == Approx(0.33f));  // resting wins over snapping
}

TEST_CASE("Ray against a box reports the entry point and face normal", "[viewport][placement]") {
    const Math::Vec3 lo{-1, 0, -1}, hi{1, 2, 1};
    const auto top = RayAabb({0.5f, 5, 0}, {0, -1, 0}, lo, hi);
    REQUIRE(top);
    CHECK(top->point.y == Approx(2.0f));
    CHECK(top->normal.y == Approx(1.0f));

    const auto side = RayAabb({-5, 1, 0}, {1, 0, 0}, lo, hi);
    REQUIRE(side);
    CHECK(side->point.x == Approx(-1.0f));
    CHECK(side->normal.x == Approx(-1.0f));

    CHECK_FALSE(RayAabb({5, 5, 5}, {0, 1, 0}, lo, hi));
    CHECK_FALSE(RayAabb({0, 5, 0}, {0, 1, 0}, lo, hi));  // box behind the ray
}

TEST_CASE("Placed bounds follow the asset's placement rotation and scale", "[viewport][placement]") {
    using Luma::Editor::Panels::PlacedBounds;
    Math::Vec3 mn, mx;
    PlacedBounds({-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, {0, 0, 0}, {2, 2, 2}, mn, mx);
    CHECK(mn.x == Catch::Approx(-1.0f));
    CHECK(mx.y == Catch::Approx(1.0f));
    // A 2 x 1 x 1 box turned 90 degrees about Y: its length now runs along Z.
    PlacedBounds({0, 0, 0}, {2, 1, 1}, {0, 90, 0}, {1, 1, 1}, mn, mx);
    CHECK(mx.x - mn.x == Catch::Approx(1.0f).margin(1e-4));
    CHECK(mx.z - mn.z == Catch::Approx(2.0f).margin(1e-4));
    CHECK(mx.y - mn.y == Catch::Approx(1.0f).margin(1e-4));
}
