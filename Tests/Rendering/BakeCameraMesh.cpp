// One-off baker: converts the provided RED camera OBJ into a compact,
// decimated SM_Camera.lmesh shipped as editor content for the camera gizmo.
// The 33 source materials are grouped by quantized base colour into <=16
// buckets; each bucket becomes one mesh section whose material NAME encodes its
// colour as "rrggbb" hex, which the editor reads to make a flat material.
// Hidden tag ([.]); run on demand with:  LumaTests.exe "[bake-camera]"
// The source OBJ/MTL live outside the repo; only the baked .lmesh is committed.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Luma/AssetImport/ImportScene.h"
#include "Luma/AssetImport/Translators/ModelTranslator.h"
#include "Luma/Asset/LumaMesh.h"
#include "Luma/Mesh/MeshSimplify.h"
#include "Luma/RHI/MeshData.h"

using namespace Luma;

namespace {

// Parse an OBJ .mtl into { material name -> diffuse Kd }.
std::map<std::string, Math::Vec3> ParseMtl(const std::filesystem::path& mtl) {
    std::map<std::string, Math::Vec3> out;
    std::ifstream in(mtl);
    std::string line, cur;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "newmtl") {
            ss >> cur;
            out[cur] = {0.3f, 0.3f, 0.3f};
        } else if (tag == "Kd" && !cur.empty()) {
            Math::Vec3 c;
            ss >> c.x >> c.y >> c.z;
            out[cur] = c;
        }
    }
    return out;
}

// Quantise a colour to 0.25 steps so the many near-black / grey materials
// collapse into a few buckets while the red accents stay distinct.
Math::Vec3 Quantize(const Math::Vec3& c) {
    auto q = [](f32 v) { return std::round(std::clamp(v, 0.0f, 1.0f) * 4.0f) / 4.0f; };
    return {q(c.x), q(c.y), q(c.z)};
}
std::string ColorKey(const Math::Vec3& c) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02x%02x%02x", static_cast<int>(std::lround(c.x * 255.0f)),
                  static_cast<int>(std::lround(c.y * 255.0f)), static_cast<int>(std::lround(c.z * 255.0f)));
    return buf;
}

// Weld by quantised position (merging shells, averaging normals) then decimate.
// Returns compacted LumaVertex/index lists for one colour bucket.
void WeldDecimate(const std::vector<LumaVertex>& srcVerts, const std::vector<u32>& bucketIndices,
                  std::vector<LumaVertex>& outVerts, std::vector<u32>& outIndices) {
    const f32 kWeld = 0.0025f;
    auto keyOf = [&](const Math::Vec3& p) {
        auto g = [&](f32 v) { return static_cast<long long>(std::llround(v / kWeld)); };
        return std::to_string(g(p.x)) + "," + std::to_string(g(p.y)) + "," + std::to_string(g(p.z));
    };
    std::unordered_map<std::string, u32> weld;
    std::vector<MeshVertex> verts;
    std::vector<Math::Vec3> nAccum;
    std::unordered_map<u32, u32> srcToWeld;
    auto weldVertex = [&](u32 si) -> u32 {
        auto hit = srcToWeld.find(si);
        if (hit != srcToWeld.end()) return hit->second;
        const LumaVertex& lv = srcVerts[si];
        const std::string k = keyOf(lv.position);
        auto it = weld.find(k);
        u32 wi;
        if (it == weld.end()) {
            wi = static_cast<u32>(verts.size());
            weld.emplace(k, wi);
            MeshVertex mv;
            mv.position = lv.position;
            mv.uv0 = lv.texCoord;
            verts.push_back(mv);
            nAccum.push_back(lv.normal);
        } else {
            wi = it->second;
            nAccum[wi] = nAccum[wi] + lv.normal;
        }
        srcToWeld.emplace(si, wi);
        return wi;
    };
    std::vector<u32> welded;
    welded.reserve(bucketIndices.size());
    for (size_t t = 0; t + 2 < bucketIndices.size(); t += 3) {
        const u32 a = weldVertex(bucketIndices[t]), b = weldVertex(bucketIndices[t + 1]),
                  c = weldVertex(bucketIndices[t + 2]);
        if (a != b && b != c && a != c) {
            welded.push_back(a);
            welded.push_back(b);
            welded.push_back(c);
        }
    }
    for (size_t i = 0; i < verts.size(); ++i) {
        const Math::Vec3 n = nAccum[i];
        const f32 l = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        verts[i].normal = l > 1e-6f ? Math::Vec3{n.x / l, n.y / l, n.z / l} : Math::Vec3{0, 1, 0};
    }
    Mesh::SimplifyOptions so;
    so.targetIndexCount = std::max<usize>(300, static_cast<usize>(welded.size() * 0.3f));
    so.maxRelativeError = 0.6f;
    const std::vector<u32> idx = Mesh::SimplifyIndices(verts, welded, so, nullptr);

    std::vector<u32> remap(verts.size(), 0xFFFFFFFFu);
    for (u32 i : idx) {
        if (remap[i] == 0xFFFFFFFFu) {
            remap[i] = static_cast<u32>(outVerts.size());
            LumaVertex v{};
            v.position = verts[i].position;
            v.normal = verts[i].normal;
            v.texCoord = verts[i].uv0;
            v.color = {1.0f, 1.0f, 1.0f, 1.0f};
            outVerts.push_back(v);
        }
        outIndices.push_back(remap[i]);
    }
}

}  // namespace

TEST_CASE("bake SM_Camera.lmesh from the RED camera OBJ", "[.][bake-camera]") {
    const std::filesystem::path base =
        "C:/Users/sdami/Downloads/Cutie/Sound Effects/red-digital-cinema-weapon-dragon-8k-camera/source";
    const std::filesystem::path src = base / "luma.obj";
    if (!std::filesystem::exists(src)) {
        WARN("source OBJ not found; skipping bake: " << src.string());
        return;
    }
    const std::map<std::string, Math::Vec3> mtl = ParseMtl(base / "luma.mtl");

    Import::ModelTranslator tr;
    Import::ImportScene imported;
    std::string err;
    REQUIRE(tr.Translate(src, imported, &err));
    std::vector<std::string> names;
    std::unordered_set<std::string> seen;
    for (Import::NodeId id : imported.OfKind(Import::NodeKind::Mesh)) {
        const std::string& n = imported.nodes[id].name;
        if (seen.insert(n).second) names.push_back(n);
    }
    REQUIRE_FALSE(names.empty());

    Import::ModelPayloadOptions opt;
    opt.combineMeshes = true;  // one mesh, one section per source material
    opt.importNormals = true;
    opt.computeTangents = false;
    opt.pivot = Import::ModelPayloadOptions::Pivot::BoundsCenter;
    std::vector<LumaMeshData> meshes = tr.BuildMeshes(src, names, opt, &err, nullptr);
    REQUIRE_FALSE(meshes.empty());
    LumaMeshData& lmd = meshes.front();
    REQUIRE(lmd.indices.size() / 3 > 300000);

    // Group every section's triangles by quantised base colour.
    std::map<std::string, Math::Vec3> bucketColor;          // key -> colour
    std::map<std::string, std::vector<u32>> bucketIndices;  // key -> raw indices into lmd.vertices
    for (const LumaSubmesh& sm : lmd.submeshes) {
        Math::Vec3 raw{0.3f, 0.3f, 0.3f};
        auto it = mtl.find(sm.materialName);
        if (it != mtl.end()) raw = it->second;
        const Math::Vec3 q = Quantize(raw);
        const std::string key = ColorKey(q);
        bucketColor[key] = q;
        std::vector<u32>& dst = bucketIndices[key];
        dst.insert(dst.end(), lmd.indices.begin() + sm.indexOffset,
                   lmd.indices.begin() + sm.indexOffset + sm.indexCount);
    }
    REQUIRE(bucketColor.size() <= 16u);

    LumaMeshData out;
    out.version = 1;
    out.name = "SM_Camera";
    out.sourceFile = src.string();
    for (const auto& [key, color] : bucketColor) {
        std::vector<LumaVertex> bv;
        std::vector<u32> bi;
        WeldDecimate(lmd.vertices, bucketIndices[key], bv, bi);
        if (bi.empty()) continue;
        const u32 vbase = static_cast<u32>(out.vertices.size());
        const u32 ibase = static_cast<u32>(out.indices.size());
        for (const LumaVertex& v : bv) out.vertices.push_back(v);
        for (u32 i : bi) out.indices.push_back(i + vbase);
        LumaSubmesh sm{};
        sm.indexOffset = ibase;
        sm.indexCount = static_cast<u32>(bi.size());
        sm.vertexOffset = vbase;
        sm.vertexCount = static_cast<u32>(bv.size());
        sm.materialName = key;  // "rrggbb" -> editor builds a flat material
        out.submeshes.push_back(sm);
    }
    out.ComputeBounds();
    REQUIRE_FALSE(out.submeshes.empty());

    const std::filesystem::path dst =
        std::filesystem::path(LUMA_SOURCE_DIR) / "Content" / "Editor" / "Meshes" / "SM_Camera.lmesh";
    std::filesystem::create_directories(dst.parent_path());
    REQUIRE(LumaMeshIO::WriteMesh(dst, out));
    WARN("Baked " << out.indices.size() / 3 << " tris, " << out.vertices.size() << " verts, "
                  << out.submeshes.size() << " colour sections -> " << dst.string());
}
