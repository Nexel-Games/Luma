// One-off baker: converts the provided RED camera OBJ into a compact, decimated
// SM_Camera.lmesh shipped as editor content for the camera gizmo. Hidden tag
// ([.]) so it never runs in the normal suite; run on demand with:
//   LumaTests.exe "[bake-camera]"
// The source OBJ lives outside the repo; only the baked .lmesh is committed.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
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

TEST_CASE("bake SM_Camera.lmesh from the RED camera OBJ", "[.][bake-camera]") {
    const std::filesystem::path src =
        "C:/Users/sdami/Downloads/Cutie/Sound Effects/"
        "red-digital-cinema-weapon-dragon-8k-camera/source/luma.obj";
    if (!std::filesystem::exists(src)) {
        WARN("source OBJ not found; skipping bake: " << src.string());
        return;
    }

    Import::ModelTranslator tr;

    // 1. Enumerate every mesh node (BuildMeshes selects by name only).
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

    // 2. Combine them all into one mesh.
    Import::ModelPayloadOptions opt;
    opt.combineMeshes = true;
    opt.importNormals = true;
    opt.computeTangents = false;  // recomputed on load (ConvertLumaMesh)
    opt.pivot = Import::ModelPayloadOptions::Pivot::BoundsCenter;
    std::vector<LumaMeshData> meshes = tr.BuildMeshes(src, names, opt, &err, nullptr);
    REQUIRE_FALSE(meshes.empty());
    LumaMeshData& lmd = meshes.front();
    // Sanity: we kept the bulk of the model (guards against lost nodes).
    REQUIRE(lmd.indices.size() / 3 > 300000);

    // 3. Weld vertices by quantized position. The source is 299 disjoint
    //    shells, so every shell boundary is a locked seam the simplifier can't
    //    collapse; welding (and averaging normals) merges them into one
    //    manifold that decimates aggressively. Fine for a flat-shaded gizmo.
    const f32 kWeld = 0.0025f;  // 2.5 mm grid
    auto keyOf = [&](const Math::Vec3& p) {
        auto q = [&](f32 v) { return static_cast<long long>(std::llround(v / kWeld)); };
        return std::to_string(q(p.x)) + "," + std::to_string(q(p.y)) + "," + std::to_string(q(p.z));
    };
    std::unordered_map<std::string, u32> weldMap;
    std::vector<MeshVertex> verts;
    std::vector<Math::Vec3> nAccum;
    std::vector<u32> wremap(lmd.vertices.size());
    for (size_t i = 0; i < lmd.vertices.size(); ++i) {
        const LumaVertex& lv = lmd.vertices[i];
        const std::string k = keyOf(lv.position);
        auto it = weldMap.find(k);
        if (it == weldMap.end()) {
            const u32 wi = static_cast<u32>(verts.size());
            weldMap.emplace(k, wi);
            MeshVertex mv;
            mv.position = lv.position;
            mv.uv0 = lv.texCoord;
            verts.push_back(mv);
            nAccum.push_back(lv.normal);
            wremap[i] = wi;
        } else {
            wremap[i] = it->second;
            nAccum[it->second] = nAccum[it->second] + lv.normal;
        }
    }
    for (size_t i = 0; i < verts.size(); ++i) {
        const Math::Vec3 n = nAccum[i];
        const f32 l = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        verts[i].normal = l > 1e-6f ? Math::Vec3{n.x / l, n.y / l, n.z / l} : Math::Vec3{0, 1, 0};
    }
    std::vector<u32> welded;
    welded.reserve(lmd.indices.size());
    for (size_t t = 0; t + 2 < lmd.indices.size(); t += 3) {
        const u32 a = wremap[lmd.indices[t]], b = wremap[lmd.indices[t + 1]], c = wremap[lmd.indices[t + 2]];
        if (a != b && b != c && a != c) {
            welded.push_back(a);
            welded.push_back(b);
            welded.push_back(c);
        }
    }

    // 3b. Decimate the welded index list to a gizmo-sized budget.
    Mesh::SimplifyOptions so;
    so.targetIndexCount = 90000;  // ~30k triangles
    so.maxRelativeError = 0.6f;
    const std::vector<u32> idx = Mesh::SimplifyIndices(verts, welded, so, nullptr);

    // 4. Compact: keep only vertices the decimated indices reference.
    std::vector<u32> remap(verts.size(), 0xFFFFFFFFu);
    LumaMeshData out;
    out.version = 1;
    out.name = "SM_Camera";
    out.sourceFile = src.string();
    out.indices.reserve(idx.size());
    for (u32 i : idx) {
        if (remap[i] == 0xFFFFFFFFu) {
            remap[i] = static_cast<u32>(out.vertices.size());
            LumaVertex v{};
            v.position = verts[i].position;
            v.normal = verts[i].normal;
            v.texCoord = verts[i].uv0;
            v.color = {1.0f, 1.0f, 1.0f, 1.0f};
            out.vertices.push_back(v);
        }
        out.indices.push_back(remap[i]);
    }

    LumaSubmesh sm{};
    sm.indexOffset = 0;
    sm.indexCount = static_cast<u32>(out.indices.size());
    sm.vertexOffset = 0;
    sm.vertexCount = static_cast<u32>(out.vertices.size());
    sm.materialName = "EditorCamera";
    out.submeshes.push_back(sm);
    out.ComputeBounds();

    // 5. Write the committed editor asset.
    const std::filesystem::path dst =
        std::filesystem::path(LUMA_SOURCE_DIR) / "Content" / "Editor" / "Meshes" / "SM_Camera.lmesh";
    std::filesystem::create_directories(dst.parent_path());
    REQUIRE(LumaMeshIO::WriteMesh(dst, out));
    WARN("Baked " << out.indices.size() / 3 << " tris, " << out.vertices.size() << " verts -> "
                  << dst.string());
}
