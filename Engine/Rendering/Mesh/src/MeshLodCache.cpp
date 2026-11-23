#include "Luma/Mesh/MeshLodCache.h"

#include <cstring>
#include <fstream>
#include <iterator>

namespace Luma::Mesh {

namespace {

constexpr char kMagic[4] = {'L', 'L', 'O', 'D'};
constexpr u32 kVersion = 1;
constexpr u32 kMaxLods = 16;

template <typename T>
void Put(std::vector<char>& out, const T& v) {
    const char* p = reinterpret_cast<const char*>(&v);
    out.insert(out.end(), p, p + sizeof(T));
}

struct Reader {
    const std::vector<char>& data;
    usize pos = 0;
    template <typename T>
    bool Get(T& v) {
        if (pos + sizeof(T) > data.size()) return false;
        std::memcpy(&v, data.data() + pos, sizeof(T));
        pos += sizeof(T);
        return true;
    }
};

}  // namespace

std::optional<LodCacheKey> MakeLodCacheKey(const std::filesystem::path& meshFile, const MeshData& mesh) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(meshFile, ec);
    if (ec) return std::nullopt;
    const auto time = std::filesystem::last_write_time(meshFile, ec);
    if (ec) return std::nullopt;
    LodCacheKey key;
    key.sourceSize = static_cast<u64>(size);
    key.sourceTime = static_cast<i64>(time.time_since_epoch().count());
    key.vertexCount = static_cast<u32>(mesh.vertices.size());
    key.indexCount = static_cast<u32>(mesh.indices.size());
    key.sectionCount = static_cast<u32>(mesh.sections.size());
    return key;
}

bool WriteLodCache(const std::filesystem::path& cacheFile, const LodCacheKey& key,
                   const std::vector<MeshLodData>& lods) {
    std::vector<char> out;
    out.insert(out.end(), kMagic, kMagic + 4);
    Put(out, kVersion);
    Put(out, key.sourceSize);
    Put(out, key.sourceTime);
    Put(out, key.vertexCount);
    Put(out, key.indexCount);
    Put(out, key.sectionCount);
    Put(out, static_cast<u32>(lods.size()));
    for (const MeshLodData& lod : lods) {
        Put(out, lod.screenSize);
        Put(out, static_cast<u32>(lod.sections.size()));
        for (const MeshSection& s : lod.sections) {
            Put(out, s.indexOffset);
            Put(out, s.indexCount);
        }
        Put(out, static_cast<u32>(lod.indices.size()));
        const char* p = reinterpret_cast<const char*>(lod.indices.data());
        out.insert(out.end(), p, p + lod.indices.size() * sizeof(u32));
    }
    std::error_code ec;
    std::filesystem::create_directories(cacheFile.parent_path(), ec);
    const std::filesystem::path temp = cacheFile.string() + ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file || !file.write(out.data(), static_cast<std::streamsize>(out.size()))) return false;
    }
    std::filesystem::rename(temp, cacheFile, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

std::optional<std::vector<MeshLodData>> ReadLodCache(const std::filesystem::path& cacheFile, const LodCacheKey& key) {
    std::ifstream file(cacheFile, std::ios::binary);
    if (!file) return std::nullopt;
    const std::vector<char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    Reader r{data};
    char magic[4];
    for (char& c : magic) {
        if (!r.Get(c)) return std::nullopt;
    }
    if (std::memcmp(magic, kMagic, 4) != 0) return std::nullopt;
    u32 version = 0;
    LodCacheKey stored;
    u32 lodCount = 0;
    if (!r.Get(version) || version != kVersion || !r.Get(stored.sourceSize) || !r.Get(stored.sourceTime) ||
        !r.Get(stored.vertexCount) || !r.Get(stored.indexCount) || !r.Get(stored.sectionCount) ||
        !r.Get(lodCount)) {
        return std::nullopt;
    }
    if (!(stored == key) || lodCount > kMaxLods) return std::nullopt;
    std::vector<MeshLodData> lods(lodCount);
    for (MeshLodData& lod : lods) {
        u32 sectionCount = 0, indexCount = 0;
        if (!r.Get(lod.screenSize) || !r.Get(sectionCount) || sectionCount != key.sectionCount) return std::nullopt;
        lod.sections.resize(sectionCount);
        for (MeshSection& s : lod.sections) {
            if (!r.Get(s.indexOffset) || !r.Get(s.indexCount)) return std::nullopt;
        }
        if (!r.Get(indexCount) || r.pos + usize(indexCount) * sizeof(u32) > data.size()) return std::nullopt;
        lod.indices.resize(indexCount);
        std::memcpy(lod.indices.data(), data.data() + r.pos, usize(indexCount) * sizeof(u32));
        r.pos += usize(indexCount) * sizeof(u32);
        for (u32 i : lod.indices) {
            if (i >= key.vertexCount) return std::nullopt;
        }
        for (const MeshSection& s : lod.sections) {
            if (usize(s.indexOffset) + s.indexCount > lod.indices.size()) return std::nullopt;
        }
    }
    if (r.pos != data.size()) return std::nullopt;  // trailing garbage
    return lods;
}

}  // namespace Luma::Mesh
