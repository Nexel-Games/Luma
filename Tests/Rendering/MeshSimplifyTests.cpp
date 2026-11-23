#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <set>

#include "Luma/Mesh/MeshSimplify.h"

using namespace Luma;
using Catch::Matchers::WithinAbs;

namespace {

// Welded UV sphere (one vertex per position, poles shared): closed, so no
// border is locked and the simplifier may collapse anywhere.
MeshData WeldedSphere(u32 rings, u32 segments) {
    MeshData m;
    m.vertices.push_back({{0, 1, 0}, {0, 1, 0}});
    for (u32 r = 1; r < rings; ++r) {
        const f32 phi = 3.14159265f * static_cast<f32>(r) / static_cast<f32>(rings);
        for (u32 s = 0; s < segments; ++s) {
            const f32 th = 6.2831853f * static_cast<f32>(s) / static_cast<f32>(segments);
            const Math::Vec3 p{std::sin(phi) * std::cos(th), std::cos(phi), std::sin(phi) * std::sin(th)};
            m.vertices.push_back({p, p});
        }
    }
    m.vertices.push_back({{0, -1, 0}, {0, -1, 0}});
    const u32 south = static_cast<u32>(m.vertices.size() - 1);
    auto ring = [&](u32 r, u32 s) { return 1 + (r - 1) * segments + (s % segments); };
    for (u32 s = 0; s < segments; ++s) m.indices.insert(m.indices.end(), {0, ring(1, s + 1), ring(1, s)});
    for (u32 r = 1; r + 1 < rings; ++r) {
        for (u32 s = 0; s < segments; ++s) {
            const u32 a = ring(r, s), b = ring(r, s + 1), c = ring(r + 1, s), d = ring(r + 1, s + 1);
            m.indices.insert(m.indices.end(), {a, b, c, b, d, c});
        }
    }
    for (u32 s = 0; s < segments; ++s) m.indices.insert(m.indices.end(), {south, ring(rings - 1, s), ring(rings - 1, s + 1)});
    m.sections.push_back({0, static_cast<u32>(m.indices.size()), "Sphere"});
    return m;
}

// Flat N x N grid in XZ (open: its outline is a locked border).
MeshData Grid(u32 n) {
    MeshData m;
    for (u32 z = 0; z <= n; ++z) {
        for (u32 x = 0; x <= n; ++x) {
            m.vertices.push_back({{static_cast<f32>(x), 0, static_cast<f32>(z)}, {0, 1, 0}});
        }
    }
    for (u32 z = 0; z < n; ++z) {
        for (u32 x = 0; x < n; ++x) {
            const u32 a = z * (n + 1) + x, b = a + 1, c = a + n + 1, d = c + 1;
            m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
        }
    }
    return m;
}

f32 MaxRadiusError(const MeshData& m, const std::vector<u32>& indices) {
    // Distance of each triangle's centroid from the unit sphere.
    f32 worst = 0.0f;
    for (usize i = 0; i + 2 < indices.size(); i += 3) {
        Math::Vec3 c{0, 0, 0};
        for (int k = 0; k < 3; ++k) c = c + m.vertices[indices[i + k]].position;
        c = c * (1.0f / 3.0f);
        worst = std::max(worst, 1.0f - std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z));
    }
    return worst;
}

}  // namespace

TEST_CASE("Simplification reaches the target on a closed surface within the error bound", "[mesh][lod]") {
    const MeshData sphere = WeldedSphere(64, 128);
    Mesh::SimplifyOptions options;
    options.targetIndexCount = sphere.indices.size() / 4;
    options.maxRelativeError = 0.05f;
    f32 error = -1.0f;
    const std::vector<u32> lod = Mesh::SimplifyIndices(sphere.vertices, sphere.indices, options, &error);
    CHECK(lod.size() % 3 == 0);
    CHECK(lod.size() <= options.targetIndexCount);
    CHECK(lod.size() > 0);
    CHECK(error > 0.0f);
    CHECK(error <= options.maxRelativeError);
    // Still a sphere: every surviving triangle stays close to the surface.
    CHECK(MaxRadiusError(sphere, lod) < 0.1f);
    // Only existing vertices are referenced, and no triangle is degenerate.
    for (usize i = 0; i < lod.size(); i += 3) {
        CHECK(lod[i] < sphere.vertices.size());
        CHECK(lod[i] != lod[i + 1]);
        CHECK(lod[i + 1] != lod[i + 2]);
        CHECK(lod[i] != lod[i + 2]);
    }
}

TEST_CASE("The error bound stops simplification early", "[mesh][lod]") {
    const MeshData sphere = WeldedSphere(32, 64);
    Mesh::SimplifyOptions options;
    options.targetIndexCount = 3;  // as far as allowed
    options.maxRelativeError = 0.002f;
    f32 error = 0.0f;
    const std::vector<u32> lod = Mesh::SimplifyIndices(sphere.vertices, sphere.indices, options, &error);
    CHECK(lod.size() > 3 * 100);  // a curved surface can't go far at 0.2 %
    CHECK(error <= 0.002f);
}

TEST_CASE("Flat interiors collapse for free; open borders are locked", "[mesh][lod]") {
    const MeshData grid = Grid(32);
    Mesh::SimplifyOptions options;
    options.targetIndexCount = 0;
    options.maxRelativeError = 1e-4f;
    f32 error = 1.0f;
    const std::vector<u32> lod = Mesh::SimplifyIndices(grid.vertices, grid.indices, options, &error);
    CHECK(lod.size() < grid.indices.size() / 4);
    CHECK_THAT(error, WithinAbs(0.0, 1e-6));
    // Every border vertex of the grid is still used (the outline is intact).
    std::set<u32> used(lod.begin(), lod.end());
    for (u32 i = 0; i <= 32; ++i) {
        CHECK(used.contains(i));                // z = 0 edge
        CHECK(used.contains(32 * 33 + i));      // z = 32 edge
        CHECK(used.contains(i * 33));           // x = 0 edge
        CHECK(used.contains(i * 33 + 32));      // x = 32 edge
    }
}

TEST_CASE("Malformed or already-small inputs come back unchanged", "[mesh][lod]") {
    const MeshData grid = Grid(4);
    Mesh::SimplifyOptions options;
    options.targetIndexCount = grid.indices.size();
    CHECK(Mesh::SimplifyIndices(grid.vertices, grid.indices, options) == grid.indices);
    std::vector<u32> bad = grid.indices;
    bad[0] = 9999;
    options.targetIndexCount = 0;
    CHECK(Mesh::SimplifyIndices(grid.vertices, bad, options) == bad);
}

TEST_CASE("LOD chains shrink per level, keep sections and get decreasing screen sizes", "[mesh][lod]") {
    MeshData sphere = WeldedSphere(64, 128);
    // Two sections (two materials): the halves of the index list.
    const u32 half = static_cast<u32>(sphere.indices.size() / 6 * 3);
    sphere.sections = {{0, half, "Top"}, {half, static_cast<u32>(sphere.indices.size()) - half, "Bottom"}};
    const std::vector<MeshLodData> lods = Mesh::BuildMeshLods(sphere);
    REQUIRE(lods.size() >= 2);
    usize previous = sphere.indices.size();
    f32 previousScreen = 1.0f;
    for (const MeshLodData& lod : lods) {
        CHECK(lod.indices.size() < previous);
        CHECK(lod.sections.size() == 2);
        CHECK(lod.sections[0].materialName == "Top");
        CHECK(lod.sections[1].indexOffset == lod.sections[0].indexCount);
        CHECK(lod.sections[0].indexCount + lod.sections[1].indexCount == lod.indices.size());
        CHECK(lod.screenSize < previousScreen);
        CHECK(lod.screenSize > 0.0f);
        previous = lod.indices.size();
        previousScreen = lod.screenSize;
    }
    // Tiny meshes get no LODs.
    CHECK(Mesh::BuildMeshLods(Grid(4)).empty());
}

TEST_CASE("Screen size and LOD selection", "[mesh][lod]") {
    const f32 fov = 2.0f * std::atan(1.0f);  // 90 degrees: tan(fov/2) = 1
    CHECK_THAT(Mesh::BoundsScreenSize(1.0f, 10.0f, fov), WithinAbs(0.1, 1e-5));
    CHECK(Mesh::BoundsScreenSize(1.0f, 0.5f, fov) == 1.0f);  // camera inside the bounds
    std::vector<MeshLodData> lods(3);
    lods[0].screenSize = 0.5f;
    lods[1].screenSize = 0.25f;
    lods[2].screenSize = 0.1f;
    CHECK(Mesh::SelectLod(lods, 0.8f) == 0);
    CHECK(Mesh::SelectLod(lods, 0.4f) == 1);
    CHECK(Mesh::SelectLod(lods, 0.2f) == 2);
    CHECK(Mesh::SelectLod(lods, 0.05f) == 3);
    CHECK(Mesh::SelectLod(lods, 0.4f, 1) == 2);    // bias: coarser
    CHECK(Mesh::SelectLod(lods, 0.4f, -5) == 0);   // clamped
    CHECK(Mesh::SelectLod(lods, 0.05f, 4) == 3);   // clamped to the last LOD
    CHECK(Mesh::SelectLod({}, 0.01f) == 0);        // no LODs
}
