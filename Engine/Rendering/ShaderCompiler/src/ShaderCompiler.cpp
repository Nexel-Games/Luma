#include "Luma/ShaderCompiler/ShaderCompiler.h"

#include <shaderc/shaderc.hpp>

#include <algorithm>
#include <fstream>
#include <functional>
#include <regex>
#include <set>
#include <sstream>

namespace Luma::ShaderCompiler {
namespace fs = std::filesystem;

namespace {

constexpr u64 kFnvBasis = 1469598103934665603ull;
constexpr u64 kFnvPrime = 1099511628211ull;

void Mix(u64& h, std::string_view text) {
    for (unsigned char c : text) {
        h ^= c;
        h *= kFnvPrime;
    }
    h ^= 0xFF;  // field separator: ("ab","c") != ("a","bc")
    h *= kFnvPrime;
}

std::string ReadText(const fs::path& file, bool& ok) {
    std::ifstream in(file, std::ios::binary);
    ok = static_cast<bool>(in);
    if (!ok) return {};
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

// `#include "name"` targets in a source, in order.
std::vector<std::string> IncludesOf(const std::string& source) {
    static const std::regex kInclude(R"re(^[ \t]*#[ \t]*include[ \t]*"([^"]+)")re", std::regex::multiline);
    std::vector<std::string> out;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), kInclude); it != std::sregex_iterator(); ++it) {
        out.push_back((*it)[1].str());
    }
    return out;
}

shaderc_shader_kind KindOf(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex: return shaderc_vertex_shader;
        case ShaderStage::Compute: return shaderc_compute_shader;
        default: return shaderc_fragment_shader;
    }
}

// "name:3: error: '' :  syntax error" -> (3, "'' :  syntax error")
std::vector<std::pair<u32, std::string>> ParseErrors(const std::string& log) {
    static const std::regex kLine(R"(:(\d+): (?:fatal )?error: ?(.*))");
    std::vector<std::pair<u32, std::string>> out;
    std::istringstream lines(log);
    for (std::string line; std::getline(lines, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::smatch m;
        if (std::regex_search(line, m, kLine)) {
            out.emplace_back(static_cast<u32>(std::stoul(m[1].str())), m[2].str());
        } else if (line.find("error:") != std::string::npos) {
            out.emplace_back(0u, line);
        }
    }
    return out;
}

// Serves #include "..." from the include directory (or next to the file
// that includes it). Results live until shaderc releases them.
class Includer final : public shaderc::CompileOptions::IncluderInterface {
public:
    using Resolve = std::function<fs::path(const std::string&, const std::string&)>;
    explicit Includer(Resolve resolve) : m_resolve(std::move(resolve)) {}

    shaderc_include_result* GetInclude(const char* requested, shaderc_include_type, const char* requesting,
                                       size_t) override {
        auto* data = new Data;
        const fs::path file = m_resolve(requested, requesting ? requesting : "");
        bool ok = false;
        if (!file.empty()) data->content = ReadText(file, ok);
        if (ok) {
            data->name = file.generic_string();
        } else {
            data->name.clear();  // empty name = failure; content is the message
            data->content = std::string("Can't find include file \"") + requested + "\"";
        }
        data->result.source_name = data->name.c_str();
        data->result.source_name_length = data->name.size();
        data->result.content = data->content.c_str();
        data->result.content_length = data->content.size();
        data->result.user_data = data;
        return &data->result;
    }

    void ReleaseInclude(shaderc_include_result* result) override { delete static_cast<Data*>(result->user_data); }

private:
    struct Data {
        std::string name, content;
        shaderc_include_result result{};
    };
    Resolve m_resolve;
};

}  // namespace

Compiler::Compiler(fs::path includeDir, fs::path cacheDir, u32 workers)
    : m_includeDir(std::move(includeDir)), m_cacheDir(std::move(cacheDir)) {
    for (u32 i = 0; i < std::max(workers, 1u); ++i) m_workers.emplace_back([this] { WorkerLoop(); });
}

Compiler::~Compiler() {
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
        m_queue.clear();
    }
    m_wake.notify_all();
    for (std::thread& t : m_workers) t.join();
}

u64 Compiler::CacheKey(const CompileRequest& request, std::string_view includeHash) {
    u64 h = kFnvBasis;
    Mix(h, request.source);
    Mix(h, std::to_string(static_cast<int>(request.stage)));
    for (const auto& [name, value] : request.defines) {
        Mix(h, name);
        Mix(h, value);
    }
    Mix(h, includeHash);
    unsigned int version = 0, revision = 0;
    shaderc_get_spv_version(&version, &revision);
    Mix(h, "shaderc" + std::to_string(version) + "." + std::to_string(revision) + "|vk1.3|spv1.6");
    Mix(h, request.optimize ? "O" : "O0");
    return h;
}

fs::path Compiler::ResolveInclude(const std::string& requested, const std::string& requester) const {
    std::error_code ec;
    if (!requester.empty()) {
        const fs::path beside = fs::path(requester).parent_path() / requested;
        if (fs::path(requester).has_parent_path() && fs::is_regular_file(beside, ec)) return beside;
    }
    const fs::path inDir = m_includeDir / requested;
    if (fs::is_regular_file(inDir, ec)) return inDir;
    return {};
}

u64 Compiler::HashFile(const fs::path& file) {
    std::error_code ec;
    const auto mtime = fs::last_write_time(file, ec);
    const u64 size = ec ? 0 : static_cast<u64>(fs::file_size(file, ec));
    {
        std::lock_guard lock(m_hashMutex);
        const auto it = m_fileHashes.find(file);
        if (it != m_fileHashes.end() && it->second.mtime == mtime && it->second.size == size) return it->second.hash;
    }
    bool ok = false;
    const std::string text = ReadText(file, ok);
    u64 h = kFnvBasis;
    Mix(h, ok ? text : std::string("<unreadable>"));
    std::lock_guard lock(m_hashMutex);
    m_fileHashes[file] = {mtime, size, h};
    return h;
}

std::string Compiler::IncludeHash(const std::string& source) {
    u64 h = kFnvBasis;
    std::set<fs::path> seen;
    std::vector<std::pair<std::string, std::string>> pending;  // (requested, requester)
    for (const std::string& inc : IncludesOf(source)) pending.emplace_back(inc, "");
    while (!pending.empty()) {
        auto [requested, requester] = pending.front();
        pending.erase(pending.begin());
        const fs::path file = ResolveInclude(requested, requester);
        Mix(h, requested);
        if (file.empty()) {
            Mix(h, "<missing>");
            continue;
        }
        if (!seen.insert(file).second) continue;
        Mix(h, std::to_string(HashFile(file)));
        bool ok = false;
        const std::string text = ReadText(file, ok);
        for (const std::string& inc : IncludesOf(text)) pending.emplace_back(inc, file.generic_string());
    }
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(h));
    return hex;
}

CompileResult Compiler::Compile(const CompileRequest& request, void* shadercCompiler) {
    CompileResult result;
    result.key = CacheKey(request, IncludeHash(request.source));
    if (LoadCached(result.key, result.spirv)) {
        result.ok = true;
        result.fromCache = true;
        return result;
    }

    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    options.SetSourceLanguage(shaderc_source_language_glsl);
    options.SetOptimizationLevel(request.optimize ? shaderc_optimization_level_performance
                                                  : shaderc_optimization_level_zero);
    for (const auto& [name, value] : request.defines) options.AddMacroDefinition(name, value);
    options.SetIncluder(std::make_unique<Includer>(
        [this](const std::string& requested, const std::string& requester) {
            return ResolveInclude(requested, requester);
        }));

    auto& compiler = *static_cast<shaderc::Compiler*>(shadercCompiler);
    const std::string name = request.name.empty() ? std::string("shader") : request.name;
    const shaderc::SpvCompilationResult spv =
        compiler.CompileGlslToSpv(request.source, KindOf(request.stage), name.c_str(), options);
    result.log = spv.GetErrorMessage();
    if (spv.GetCompilationStatus() != shaderc_compilation_status_success) {
        result.errors = ParseErrors(result.log);
        if (result.errors.empty()) result.errors.emplace_back(0u, result.log.empty() ? "Compilation failed" : result.log);
        return result;
    }
    result.spirv.assign(spv.cbegin(), spv.cend());
    result.ok = true;
    StoreCached(result.key, result.spirv);
    return result;
}

CompileResult Compiler::CompileSync(const CompileRequest& request) {
    shaderc::Compiler compiler;
    return Compile(request, &compiler);
}

u64 Compiler::Submit(CompileRequest request) {
    u64 ticket = 0;
    {
        std::lock_guard lock(m_mutex);
        ticket = m_nextTicket++;
        m_queue.push_back({ticket, std::move(request)});
        ++m_pending;
    }
    m_wake.notify_one();
    return ticket;
}

std::vector<std::pair<u64, CompileResult>> Compiler::Poll() {
    std::lock_guard lock(m_mutex);
    std::vector<std::pair<u64, CompileResult>> out;
    out.swap(m_done);
    m_pending -= static_cast<u32>(out.size());
    return out;
}

u32 Compiler::Pending() const {
    std::lock_guard lock(m_mutex);
    return m_pending;
}

void Compiler::WorkerLoop() {
    shaderc::Compiler compiler;  // one per worker: no shared compiler state
    for (;;) {
        Job job;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stop || !m_queue.empty(); });
            if (m_stop) return;
            job = std::move(m_queue.front());
            m_queue.pop_front();
        }
        CompileResult result = Compile(job.request, &compiler);
        std::lock_guard lock(m_mutex);
        m_done.emplace_back(job.ticket, std::move(result));
    }
}

}  // namespace Luma::ShaderCompiler
