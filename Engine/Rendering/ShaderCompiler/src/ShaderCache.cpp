// On-disk SPIR-V cache: <cacheDir>/<key as 16 hex digits>.spv. Writes go to a
// unique temporary file renamed into place, so concurrent workers and a
// crashed write never leave a torn entry; entries that fail validation are
// treated as misses.

#include "Luma/ShaderCompiler/ShaderCompiler.h"

#include <cstdio>
#include <fstream>
#include <thread>

namespace Luma::ShaderCompiler {
namespace fs = std::filesystem;

namespace {

constexpr u32 kSpirvMagic = 0x07230203u;

fs::path EntryPath(const fs::path& dir, u64 key) {
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(key));
    return dir / (std::string(hex) + ".spv");
}

}  // namespace

bool Compiler::LoadCached(u64 key, std::vector<u32>& spirv) const {
    if (m_cacheDir.empty()) return false;
    std::ifstream in(EntryPath(m_cacheDir, key), std::ios::binary | std::ios::ate);
    if (!in) return false;
    const std::streamoff bytes = in.tellg();
    if (bytes < 20 || bytes % 4 != 0) return false;
    spirv.resize(static_cast<usize>(bytes / 4));
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(spirv.data()), bytes) || spirv[0] != kSpirvMagic) {
        spirv.clear();
        return false;
    }
    return true;
}

void Compiler::StoreCached(u64 key, std::span<const u32> spirv) const {
    if (m_cacheDir.empty() || spirv.empty()) return;
    std::error_code ec;
    fs::create_directories(m_cacheDir, ec);
    const fs::path target = EntryPath(m_cacheDir, key);
    fs::path temp = target;
    temp += "." + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out.write(reinterpret_cast<const char*>(spirv.data()), static_cast<std::streamsize>(spirv.size_bytes()));
        if (!out) {
            out.close();
            fs::remove(temp, ec);
            return;
        }
    }
    fs::rename(temp, target, ec);
    if (ec) fs::remove(temp, ec);  // another worker stored the same entry
}

}  // namespace Luma::ShaderCompiler
