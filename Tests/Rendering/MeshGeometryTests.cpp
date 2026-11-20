// Geometry tests: primitive UVs/tangents, tangent generation (incl. mirrored
// UVs) and .lmesh conversion with sections.

#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "Luma/Asset/LumaMesh.h"
#include "Luma/Mesh/Mesh.h"
#include "Luma/Mesh/MeshConversion.h"
#include "Luma/Mesh/Tangents.h"

using namespace Luma;
using Math::Vec2;
using Math::Vec3;

namespace {

f32 Len(const Vec3& v) { return std::sqrt(Math::Dot(v, v)); }
Vec3 Xyz(const Math::Vec4& v) { return {v.x, v.y, v.z}; }

}  // namespace

TEST_CASE("Primitive UVs are within [0,1] and tangents are unit and orthogonal",
          "[mesh]") {
    for (MeshPrimitive p : {MeshPrimitive::Cube, MeshPrimitive::Plane,
                            MeshPrimitive::Sphere, MeshPrimitive::Cylinder}) {
        const MeshData m = BuildPrimitive(p);
        REQUIRE(m.sections.size() == 1);
        CHECK(m.sections[0].indexOffset == 0);
        CHECK(m.sections[0].indexCount == m.indices.size());
        for (const MeshVertex& v : m.vertices) {
            CHECK(v.uv0.x >= -1e-5f);
            CHECK(v.uv0.x <= 1.0f + 1e-5f);
            CHECK(v.uv0.y >= -1e-5f);
            CHECK(v.uv0.y <= 1.0f + 1e-5f);
            const Vec3 t = Xyz(v.tangent);
            CHECK(std::fabs(Len(t) - 1.0f) < 1e-3f);
            CHECK(std::fabs(Math::Dot(t, v.normal)) < 1e-3f);
            CHECK(std::fabs(std::fabs(v.tangent.w) - 1.0f) < 1e-6f);
        }
    }
}

TEST_CASE("Plane tangent points along +X with positive handedness", "[mesh]") {
    const MeshData m = BuildPrimitive(MeshPrimitive::Plane);
    for (const MeshVertex& v : m.vertices) {
        CHECK(std::fabs(v.tangent.x - 1.0f) < 1e-4f);
        CHECK(std::fabs(v.tangent.y) < 1e-4f);
        CHECK(std::fabs(v.tangent.z) < 1e-4f);
        CHECK(v.tangent.w == 1.0f);
    }
}

TEST_CASE("Sphere has a duplicated UV seam", "[mesh]") {
    const MeshData m = BuildPrimitive(MeshPrimitive::Sphere);
    for (usize i = 0; i + 2 < m.indices.size(); i += 3) {
        const f32 u0 = m.vertices[m.indices[i]].uv0.x;
        const f32 u1 = m.vertices[m.indices[i + 1]].uv0.x;
        const f32 u2 = m.vertices[m.indices[i + 2]].uv0.x;
        const f32 span = std::max({u0, u1, u2}) - std::min({u0, u1, u2});
        CHECK(span < 0.5f);  // no triangle wraps from u≈1 back to u≈0
    }
}

TEST_CASE("ComputeTangents flags mirrored UVs with negative handedness", "[mesh]") {
    // Quad on the XY plane facing +Z; second copy has U mirrored.
    auto quad = [](bool mirrored) {
        MeshData m;
        const Vec3 n{0, 0, 1};
        const Vec2 uv[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        const Vec3 p[4] = {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
        for (int i = 0; i < 4; ++i) {
            MeshVertex v;
            v.position = p[i];
            v.normal = n;
            v.uv0 = mirrored ? Vec2(1.0f - uv[i].x, uv[i].y) : uv[i];
            m.vertices.push_back(v);
        }
        m.indices = {0, 1, 2, 0, 2, 3};
        m.sections.push_back({0, 6, ""});
        ComputeTangents(m);
        return m;
    };
    const MeshData normal = quad(false);
    const MeshData mirrored = quad(true);
    CHECK(normal.vertices[0].tangent.w == 1.0f);
    CHECK(std::fabs(normal.vertices[0].tangent.x - 1.0f) < 1e-4f);
    CHECK(mirrored.vertices[0].tangent.w == -1.0f);
    CHECK(std::fabs(mirrored.vertices[0].tangent.x + 1.0f) < 1e-4f);
}

TEST_CASE("ConvertLumaMesh keeps sections, UVs and builds tangents", "[mesh]") {
    LumaMeshData src;
    const Vec3 p[4] = {{-1, 0, -1}, {1, 0, -1}, {1, 0, 1}, {-1, 0, 1}};
    const Vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int i = 0; i < 4; ++i) {
        LumaVertex v{};
        v.position = p[i];
        v.normal = {0, 1, 0};
        v.texCoord = uv[i];
        src.vertices.push_back(v);
    }
    src.indices = {0, 2, 1, 0, 3, 2};
    LumaSubmesh a{};
    a.indexOffset = 0;
    a.indexCount = 3;
    a.materialName = "Wood";
    LumaSubmesh b{};
    b.indexOffset = 3;
    b.indexCount = 3;
    b.materialName = "Metal";
    src.submeshes = {a, b};

    const MeshData m = ConvertLumaMesh(src);
    REQUIRE(m.vertices.size() == 4);
    REQUIRE(m.sections.size() == 2);
    CHECK(m.sections[0].indexOffset == 0);
    CHECK(m.sections[0].indexCount == 3);
    CHECK(m.sections[0].materialName == "Wood");
    CHECK(m.sections[1].indexOffset == 3);
    CHECK(m.sections[1].materialName == "Metal");
    CHECK(m.vertices[2].uv0.x == 1.0f);
    CHECK(m.vertices[2].uv0.y == 1.0f);
    for (const MeshVertex& v : m.vertices) {
        CHECK(std::fabs(Len(Xyz(v.tangent)) - 1.0f) < 1e-3f);
        CHECK(std::fabs(Math::Dot(Xyz(v.tangent), v.normal)) < 1e-3f);
    }
}

TEST_CASE("ConvertLumaMesh without submeshes yields one section", "[mesh]") {
    LumaMeshData src;
    for (int i = 0; i < 3; ++i) {
        LumaVertex v{};
        v.position = {static_cast<f32>(i), 0, static_cast<f32>(i % 2)};
        v.normal = {0, 1, 0};
        src.vertices.push_back(v);
    }
    src.indices = {0, 1, 2};
    const MeshData m = ConvertLumaMesh(src);
    REQUIRE(m.sections.size() == 1);
    CHECK(m.sections[0].indexCount == 3);
    // No UVs: tangents still valid (any unit vector orthogonal to the normal).
    for (const MeshVertex& v : m.vertices) {
        CHECK(std::fabs(Len(Xyz(v.tangent)) - 1.0f) < 1e-3f);
        CHECK(std::fabs(Math::Dot(Xyz(v.tangent), v.normal)) < 1e-3f);
    }
}

// The G-buffer builds the normal-map frame as T, B = cross(N, T) * w and
// reads OpenGL-style maps (+Y = image up). Pin that convention on every
// primitive: T follows +u and B follows image-up (-v) across each triangle.
TEST_CASE("Primitive tangent frames match OpenGL normal maps", "[mesh]") {
    for (MeshPrimitive p : {MeshPrimitive::Cube, MeshPrimitive::Plane, MeshPrimitive::Sphere,
                            MeshPrimitive::Cylinder}) {
        const MeshData m = BuildPrimitive(p);
        int checked = 0;
        for (Luma::usize i = 0; i + 2 < m.indices.size(); i += 3) {
            const MeshVertex& v0 = m.vertices[m.indices[i]];
            const MeshVertex& v1 = m.vertices[m.indices[i + 1]];
            const MeshVertex& v2 = m.vertices[m.indices[i + 2]];
            const Vec3 e1 = v1.position - v0.position, e2 = v2.position - v0.position;
            const float du1 = v1.uv0.x - v0.uv0.x, dv1 = v1.uv0.y - v0.uv0.y;
            const float du2 = v2.uv0.x - v0.uv0.x, dv2 = v2.uv0.y - v0.uv0.y;
            const float det = du1 * dv2 - du2 * dv1;
            if (std::fabs(det) < 1e-6f) continue;
            const Vec3 dPdu = (e1 * dv2 - e2 * dv1) * (1.0f / det);
            const Vec3 dPdv = (e2 * du1 - e1 * du2) * (1.0f / det);
            // Pole triangles collapse to a point in u (|dP/du| ~ 0) and say
            // nothing about the frame; skip them.
            if (Len(dPdu) < 1e-4f || Len(dPdv) < 1e-4f) continue;
            const Vec3 u = dPdu * (1.0f / Len(dPdu));
            const Vec3 up = dPdv * (-1.0f / Len(dPdv));
            for (const MeshVertex* v : {&v0, &v1, &v2}) {
                const Vec3 t = Xyz(v->tangent);
                const Vec3 b = Math::Cross(v->normal, t) * v->tangent.w;
                INFO("primitive " << static_cast<int>(p) << " triangle " << i / 3);
                CHECK(Math::Dot(t, u) > 0.5f);
                CHECK(Math::Dot(b, up) > 0.5f);
            }
            ++checked;
        }
        CHECK(checked > 0);
    }
}

// Front faces are counter-clockwise seen from outside (the G-buffer pipeline
// culls back faces with VK_FRONT_FACE_COUNTER_CLOCKWISE): every triangle's
// geometric normal points the same way as its vertex normals.
TEST_CASE("Primitive triangles wind counter-clockwise from outside", "[mesh]") {
    for (MeshPrimitive p : {MeshPrimitive::Cube, MeshPrimitive::Plane, MeshPrimitive::Sphere,
                            MeshPrimitive::Cylinder}) {
        const MeshData m = BuildPrimitive(p);
        int wrong = 0, checked = 0;
        for (Luma::usize i = 0; i + 2 < m.indices.size(); i += 3) {
            const MeshVertex& v0 = m.vertices[m.indices[i]];
            const MeshVertex& v1 = m.vertices[m.indices[i + 1]];
            const MeshVertex& v2 = m.vertices[m.indices[i + 2]];
            const Vec3 face = Math::Cross(v1.position - v0.position, v2.position - v0.position);
            if (Len(face) < 1e-9f) continue;  // degenerate (pole) triangle
            const Vec3 n = v0.normal + v1.normal + v2.normal;
            ++checked;
            if (Math::Dot(face, n) <= 0.0f) ++wrong;
        }
        INFO("primitive " << static_cast<int>(p));
        CHECK(checked > 0);
        CHECK(wrong == 0);
    }
}
