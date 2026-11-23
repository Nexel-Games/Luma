#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/RHI/MeshData.h"

// On-disk cache of generated mesh LODs (derived data, like Unreal's DDC), so
// a mesh is simplified once and later loads read its LODs back instantly.
//
// A cache file is valid only for the exact source it was built from: the
// source file's size and modification time and the mesh's vertex, index and
// section counts are stored and compared on read. Any mismatch (the mesh was
// re-imported or edited), a different format version, or a truncated /
// corrupt file reads as "no cache" and the LODs are rebuilt.

namespace Luma::Mesh {

struct LodCacheKey {
    u64 sourceSize = 0;
    i64 sourceTime = 0;  // file_time_type ticks
    u32 vertexCount = 0;
    u32 indexCount = 0;
    u32 sectionCount = 0;
    bool operator==(const LodCacheKey&) const = default;
};

// Key for `meshFile` holding `mesh` (nullopt when the file can't be stat'ed).
std::optional<LodCacheKey> MakeLodCacheKey(const std::filesystem::path& meshFile, const MeshData& mesh);

// Writes atomically (temp file + rename). False on I/O failure.
bool WriteLodCache(const std::filesystem::path& cacheFile, const LodCacheKey& key,
                   const std::vector<MeshLodData>& lods);
// The cached LODs when the file exists, is intact and matches `key`.
std::optional<std::vector<MeshLodData>> ReadLodCache(const std::filesystem::path& cacheFile, const LodCacheKey& key);

}  // namespace Luma::Mesh
