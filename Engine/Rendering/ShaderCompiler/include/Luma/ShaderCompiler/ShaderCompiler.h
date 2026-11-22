#pragma once

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "Luma/Core/Types.h"

// Runtime GLSL -> SPIR-V (Unreal: the shader compiling manager + DDC, in
// Luma's own form). Generated material shaders compile on worker threads;
// results are cached on disk by a key over everything that affects the
// SPIR-V (source, stage, defines, the contents of every included file,
// compiler version and options), so an unchanged material never recompiles,
// even across editor sessions.

namespace Luma::ShaderCompiler {

enum class ShaderStage : u8 { Vertex, Fragment, Compute };

struct CompileRequest {
    std::string source;
    std::string name;  // shown in errors ("M_Brick.frag")
    ShaderStage stage = ShaderStage::Fragment;
    std::vector<std::pair<std::string, std::string>> defines;
    bool optimize = true;
};

struct CompileResult {
    bool ok = false;
    std::vector<u32> spirv;
    std::string log;                                // compiler output (errors and warnings)
    std::vector<std::pair<u32, std::string>> errors;  // (line, message); line 0 = no line
    u64 key = 0;
    bool fromCache = false;
};

struct SpirvStats {
    u32 instructions = 0;    // instructions inside functions
    u32 textureSamples = 0;  // sample / fetch / gather instructions
    u32 samplers = 0;        // sampler / sampled-image bindings
};

class Compiler {
public:
    // `includeDir` resolves #include "..."; `cacheDir` holds <key>.spv files
    // (created on demand). `workers` threads serve Submit (at least 1).
    Compiler(std::filesystem::path includeDir, std::filesystem::path cacheDir, u32 workers);
    ~Compiler();
    Compiler(const Compiler&) = delete;
    Compiler& operator=(const Compiler&) = delete;

    CompileResult CompileSync(const CompileRequest& request);
    // Queues a compile; its result comes back from Poll with this ticket.
    u64 Submit(CompileRequest request);
    // Finished jobs since the last call (call from one thread, e.g. main).
    std::vector<std::pair<u64, CompileResult>> Poll();
    // Jobs submitted and not yet returned by Poll.
    u32 Pending() const;

    static SpirvStats Stats(std::span<const u32> spirv);
    // FNV-1a over source, stage, defines, `includeHash`, the shaderc
    // version and the options.
    static u64 CacheKey(const CompileRequest& request, std::string_view includeHash);

private:
    struct Job {
        u64 ticket = 0;
        CompileRequest request;
    };
    struct FileHash {
        std::filesystem::file_time_type mtime;
        u64 size = 0;
        u64 hash = 0;
    };

    void WorkerLoop();
    CompileResult Compile(const CompileRequest& request, void* shadercCompiler);
    // Hash of the contents of every file `source` includes, recursively.
    std::string IncludeHash(const std::string& source);
    u64 HashFile(const std::filesystem::path& file);
    std::filesystem::path ResolveInclude(const std::string& requested, const std::string& requester) const;

    // Disk cache (ShaderCache.cpp).
    bool LoadCached(u64 key, std::vector<u32>& spirv) const;
    void StoreCached(u64 key, std::span<const u32> spirv) const;

    std::filesystem::path m_includeDir;
    std::filesystem::path m_cacheDir;

    mutable std::mutex m_hashMutex;
    std::map<std::filesystem::path, FileHash> m_fileHashes;

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<Job> m_queue;
    std::vector<std::pair<u64, CompileResult>> m_done;
    u64 m_nextTicket = 1;
    u32 m_pending = 0;
    bool m_stop = false;
    std::vector<std::thread> m_workers;
};

}  // namespace Luma::ShaderCompiler
