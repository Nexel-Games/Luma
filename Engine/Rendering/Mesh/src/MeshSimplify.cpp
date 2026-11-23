#include "Luma/Mesh/MeshSimplify.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

namespace Luma::Mesh {

namespace {

// Symmetric 4x4 quadric: a00 a01 a02 a11 a12 a22 (A), b0 b1 b2 (b), c.
struct Quadric {
    double a00 = 0, a01 = 0, a02 = 0, a11 = 0, a12 = 0, a22 = 0;
    double b0 = 0, b1 = 0, b2 = 0, c = 0;

    static Quadric FromPlane(double nx, double ny, double nz, double d, double w) {
        Quadric q;
        q.a00 = w * nx * nx;
        q.a01 = w * nx * ny;
        q.a02 = w * nx * nz;
        q.a11 = w * ny * ny;
        q.a12 = w * ny * nz;
        q.a22 = w * nz * nz;
        q.b0 = w * nx * d;
        q.b1 = w * ny * d;
        q.b2 = w * nz * d;
        q.c = w * d * d;
        return q;
    }
    Quadric& operator+=(const Quadric& o) {
        a00 += o.a00; a01 += o.a01; a02 += o.a02; a11 += o.a11; a12 += o.a12; a22 += o.a22;
        b0 += o.b0; b1 += o.b1; b2 += o.b2; c += o.c;
        return *this;
    }
    // Sum of squared (area-weighted) distances of p to the accumulated planes.
    double Error(const Math::Vec3& p) const {
        const double x = p.x, y = p.y, z = p.z;
        const double e = a00 * x * x + 2 * a01 * x * y + 2 * a02 * x * z + a11 * y * y + 2 * a12 * y * z +
                         a22 * z * z + 2 * (b0 * x + b1 * y + b2 * z) + c;
        return e > 0.0 ? e : 0.0;
    }
};

struct Candidate {
    double cost;
    u32 from, to;
    u32 fromVersion, toVersion;
    bool operator>(const Candidate& o) const { return cost > o.cost; }
};

Math::Vec3 VCross(const Math::Vec3& a, const Math::Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
f32 VDot(const Math::Vec3& a, const Math::Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Math::Vec3 VSub(const Math::Vec3& a, const Math::Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

}  // namespace

std::vector<u32> SimplifyIndices(std::span<const MeshVertex> vertices, std::span<const u32> indices,
                                 const SimplifyOptions& options, f32* outError) {
    if (outError) *outError = 0.0f;
    std::vector<u32> tris(indices.begin(), indices.end());
    const usize triCount = tris.size() / 3;
    if (triCount == 0 || options.targetIndexCount >= tris.size()) return tris;
    for (u32 i : tris) {
        if (i >= vertices.size()) return tris;  // malformed: leave untouched
    }

    // Local vertex set (only vertices this index list uses).
    Math::Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    const usize vertexCount = vertices.size();
    std::vector<Quadric> quadric(vertexCount);
    std::vector<std::vector<u32>> vertexTris(vertexCount);
    std::vector<u32> version(vertexCount, 0);
    std::vector<u8> collapsed(vertexCount, 0);
    std::vector<u8> locked(vertexCount, 0);
    std::vector<u8> alive(triCount, 1);

    for (usize t = 0; t < triCount; ++t) {
        const u32 a = tris[t * 3], b = tris[t * 3 + 1], c = tris[t * 3 + 2];
        for (u32 v : {a, b, c}) {
            vertexTris[v].push_back(static_cast<u32>(t));
            const Math::Vec3& p = vertices[v].position;
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
        }
        const Math::Vec3& pa = vertices[a].position;
        const Math::Vec3 n = VCross(VSub(vertices[b].position, pa), VSub(vertices[c].position, pa));
        const double len = std::sqrt(static_cast<double>(VDot(n, n)));
        if (len <= 1e-20) continue;  // degenerate: no plane
        const double nx = n.x / len, ny = n.y / len, nz = n.z / len;
        const double d = -(nx * pa.x + ny * pa.y + nz * pa.z);
        const Quadric q = Quadric::FromPlane(nx, ny, nz, d, len * 0.5);  // area weight
        quadric[a] += q;
        quadric[b] += q;
        quadric[c] += q;
    }
    const Math::Vec3 ext = VSub(hi, lo);
    const double diag = std::max(std::sqrt(static_cast<double>(VDot(ext, ext))), 1e-12);
    const double maxError = static_cast<double>(options.maxRelativeError) * diag;
    const double maxCost = maxError * maxError;

    // Border edges (used by one triangle) lock both ends. UV / normal seams
    // split vertices, so seam edges are borders here and stay put too.
    {
        std::unordered_map<u64, u32> edgeUse;
        edgeUse.reserve(triCount * 3);
        auto key = [](u32 x, u32 y) { return x < y ? (u64(x) << 32) | y : (u64(y) << 32) | x; };
        for (usize t = 0; t < triCount; ++t) {
            for (int e = 0; e < 3; ++e) ++edgeUse[key(tris[t * 3 + e], tris[t * 3 + (e + 1) % 3])];
        }
        for (const auto& [k, uses] : edgeUse) {
            if (uses != 1) continue;
            locked[static_cast<u32>(k >> 32)] = 1;
            locked[static_cast<u32>(k & 0xFFFFFFFFu)] = 1;
        }
    }

    std::priority_queue<Candidate, std::vector<Candidate>, std::greater<Candidate>> heap;
    auto pushEdge = [&](u32 from, u32 to) {
        if (locked[from] || from == to) return;
        Quadric q = quadric[from];
        q += quadric[to];
        heap.push({q.Error(vertices[to].position), from, to, version[from], version[to]});
    };
    for (usize t = 0; t < triCount; ++t) {
        for (int e = 0; e < 3; ++e) {
            const u32 x = tris[t * 3 + e], y = tris[t * 3 + (e + 1) % 3];
            pushEdge(x, y);
            pushEdge(y, x);
        }
    }

    usize liveTris = triCount;
    const usize targetTris = options.targetIndexCount / 3;
    double lastCost = 0.0;
    while (liveTris > targetTris && !heap.empty()) {
        const Candidate c = heap.top();
        heap.pop();
        if (collapsed[c.from] || collapsed[c.to] || version[c.from] != c.fromVersion ||
            version[c.to] != c.toVersion) {
            continue;  // stale
        }
        if (c.cost > maxCost) break;  // every remaining collapse is worse
        const u32 u = c.from, v = c.to;
        const Math::Vec3& pv = vertices[v].position;

        // Reject collapses that flip (or squash) a surviving triangle.
        bool valid = true;
        for (u32 t : vertexTris[u]) {
            if (!alive[t]) continue;
            u32* tri = &tris[t * 3];
            if (tri[0] == v || tri[1] == v || tri[2] == v) continue;  // removed by the collapse
            Math::Vec3 p[3];
            for (int k = 0; k < 3; ++k) p[k] = vertices[tri[k]].position;
            const Math::Vec3 before = VCross(VSub(p[1], p[0]), VSub(p[2], p[0]));
            for (int k = 0; k < 3; ++k) {
                if (tri[k] == u) p[k] = pv;
            }
            const Math::Vec3 after = VCross(VSub(p[1], p[0]), VSub(p[2], p[0]));
            const f32 lb = std::sqrt(VDot(before, before)), la = std::sqrt(VDot(after, after));
            if (la <= 1e-12f * std::max(lb, 1e-12f) || VDot(before, after) < 0.25f * lb * la) {
                valid = false;
                break;
            }
        }
        if (!valid) continue;

        // Collapse u -> v.
        collapsed[u] = 1;
        ++version[u];
        ++version[v];
        quadric[v] += quadric[u];
        lastCost = std::max(lastCost, c.cost);
        for (u32 t : vertexTris[u]) {
            if (!alive[t]) continue;
            u32* tri = &tris[t * 3];
            if (tri[0] == v || tri[1] == v || tri[2] == v) {
                alive[t] = 0;
                --liveTris;
                continue;
            }
            for (int k = 0; k < 3; ++k) {
                if (tri[k] == u) tri[k] = v;
            }
            vertexTris[v].push_back(t);
        }
        vertexTris[u].clear();
        std::erase_if(vertexTris[v], [&](u32 t) { return !alive[t]; });
        // Costs around v changed: re-queue its edges both ways.
        for (u32 t : vertexTris[v]) {
            const u32* tri = &tris[t * 3];
            for (int k = 0; k < 3; ++k) {
                const u32 w = tri[k];
                if (w == v) continue;
                pushEdge(w, v);
                pushEdge(v, w);
            }
        }
    }

    std::vector<u32> out;
    out.reserve(liveTris * 3);
    for (usize t = 0; t < triCount; ++t) {
        if (alive[t]) out.insert(out.end(), &tris[t * 3], &tris[t * 3] + 3);
    }
    if (outError) *outError = static_cast<f32>(std::sqrt(lastCost) / diag);
    return out;
}

std::vector<MeshLodData> BuildMeshLods(const MeshData& mesh, const LodSettings& settings) {
    std::vector<MeshLodData> lods;
    if (mesh.indices.size() / 3 < settings.minTrianglesForLods || settings.maxLods < 2) return lods;
    std::vector<MeshSection> sections = mesh.sections;
    if (sections.empty()) sections.push_back({0, static_cast<u32>(mesh.indices.size()), {}});

    // Previous level, per section.
    std::vector<std::vector<u32>> previous(sections.size());
    for (usize s = 0; s < sections.size(); ++s) {
        const MeshSection& sec = sections[s];
        const usize end = std::min<usize>(mesh.indices.size(), usize(sec.indexOffset) + sec.indexCount);
        if (sec.indexOffset < end) previous[s].assign(mesh.indices.begin() + sec.indexOffset, mesh.indices.begin() + end);
    }
    usize previousTotal = mesh.indices.size();
    f32 previousScreen = 1.0f;
    for (u32 level = 1; level < settings.maxLods; ++level) {
        MeshLodData lod;
        f32 worstError = 0.0f;
        for (usize s = 0; s < sections.size(); ++s) {
            SimplifyOptions options;
            options.targetIndexCount =
                static_cast<usize>(static_cast<f64>(previous[s].size()) * settings.reductionPerLod) / 3 * 3;
            options.maxRelativeError = settings.maxRelativeError;
            f32 error = 0.0f;
            std::vector<u32> simplified = SimplifyIndices(mesh.vertices, previous[s], options, &error);
            worstError = std::max(worstError, error);
            MeshSection section = sections[s];
            section.indexOffset = static_cast<u32>(lod.indices.size());
            section.indexCount = static_cast<u32>(simplified.size());
            lod.sections.push_back(std::move(section));
            lod.indices.insert(lod.indices.end(), simplified.begin(), simplified.end());
            previous[s] = std::move(simplified);
        }
        if (lod.indices.empty() ||
            static_cast<f32>(lod.indices.size()) > settings.minReduction * static_cast<f32>(previousTotal)) {
            break;  // too little gain for another level
        }
        // Switch when the geometric error projects to about one pixel at
        // 1080p (the error is relative to the mesh size, which is about the
        // bounding-sphere diameter the screen size measures).
        constexpr f32 kPixelError = 1.0f;
        constexpr f32 kReferenceHeight = 1080.0f;
        f32 screen = worstError > 0.0f ? kPixelError / (kReferenceHeight * worstError) : previousScreen;
        screen = std::clamp(screen, 0.02f, previousScreen * 0.95f);
        lod.screenSize = screen;
        previousScreen = screen;
        previousTotal = lod.indices.size();
        lods.push_back(std::move(lod));
    }
    return lods;
}

f32 BoundsScreenSize(f32 radius, f32 distance, f32 fovY) {
    const f32 t = std::tan(std::max(fovY, 1e-3f) * 0.5f);
    if (distance <= radius) return 1.0f;
    return std::clamp(radius / (distance * t), 0.0f, 1.0f);
}

u32 SelectLod(std::span<const MeshLodData> lods, f32 screenSize, i32 bias) {
    i32 lod = 0;
    for (usize i = 0; i < lods.size(); ++i) {
        if (screenSize < lods[i].screenSize) lod = static_cast<i32>(i) + 1;
    }
    lod = std::clamp(lod + bias, 0, static_cast<i32>(lods.size()));
    return static_cast<u32>(lod);
}

}  // namespace Luma::Mesh
