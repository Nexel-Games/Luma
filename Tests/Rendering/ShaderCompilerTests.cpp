// Runtime GLSL -> SPIR-V (shaderc): errors with lines, includes, the disk
// cache and its key, async jobs and SPIR-V stats.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "Luma/ShaderCompiler/ShaderCompiler.h"

namespace fs = std::filesystem;
using namespace Luma;
using namespace Luma::ShaderCompiler;

namespace {

struct Dirs {
    fs::path root = fs::temp_directory_path() / "luma_shadercompiler_test";
    fs::path include = root / "include";
    fs::path cache = root / "cache";
    Dirs() {
        fs::remove_all(root);
        fs::create_directories(include);
        WriteInclude("float LumaHalf(float x) { return x * 0.5; }\n");
    }
    ~Dirs() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void WriteInclude(const std::string& text) const {
        const fs::path file = include / "material_common.glsl";
        const bool existed = fs::exists(file);
        const auto before = existed ? fs::last_write_time(file) : fs::file_time_type{};
        std::ofstream(file, std::ios::binary) << text;
        // Make sure the modification time moves even on coarse clocks.
        if (existed) fs::last_write_time(file, before + std::chrono::seconds(2));
    }
};

CompileRequest Fragment(std::string source) {
    CompileRequest r;
    r.name = "test.frag";
    r.stage = ShaderStage::Fragment;
    r.source = std::move(source);
    return r;
}

const char* kTrivial = "#version 450\nlayout(location = 0) out vec4 o;\nvoid main() { o = vec4(1.0); }\n";
const char* kIncluding =
    "#version 450\n#extension GL_GOOGLE_include_directive : require\n#include \"material_common.glsl\"\n"
    "layout(location = 0) out vec4 o;\nvoid main() { o = vec4(LumaHalf(1.0)); }\n";

}  // namespace

TEST_CASE("A trivial fragment shader compiles to SPIR-V", "[shadercompiler]") {
    Dirs d;
    Compiler c(d.include, d.cache, 1);
    const CompileResult r = c.CompileSync(Fragment(kTrivial));
    INFO(r.log);
    REQUIRE(r.ok);
    REQUIRE(r.spirv.size() > 5);
    CHECK(r.spirv[0] == 0x07230203u);
    CHECK_FALSE(r.fromCache);
}

TEST_CASE("A syntax error reports its line", "[shadercompiler]") {
    Dirs d;
    Compiler c(d.include, d.cache, 1);
    const CompileResult r = c.CompileSync(Fragment("#version 450\nvoid main() {\n    float x = ;\n}\n"));
    CHECK_FALSE(r.ok);
    REQUIRE_FALSE(r.errors.empty());
    CHECK(r.errors.front().first == 3);
    CHECK_FALSE(r.errors.front().second.empty());
}

TEST_CASE("Includes resolve from the include directory", "[shadercompiler]") {
    Dirs d;
    Compiler c(d.include, d.cache, 1);
    const CompileResult r = c.CompileSync(Fragment(kIncluding));
    INFO(r.log);
    CHECK(r.ok);
}

TEST_CASE("An identical compile comes from the disk cache", "[shadercompiler]") {
    Dirs d;
    CompileResult first;
    {
        Compiler c(d.include, d.cache, 1);
        first = c.CompileSync(Fragment(kTrivial));
        REQUIRE(first.ok);
        const CompileResult second = c.CompileSync(Fragment(kTrivial));
        CHECK(second.fromCache);
        CHECK(second.spirv == first.spirv);
    }
    Compiler fresh(d.include, d.cache, 1);  // survives a restart
    const CompileResult third = fresh.CompileSync(Fragment(kTrivial));
    CHECK(third.fromCache);
    CHECK(third.key == first.key);
}

TEST_CASE("Changing an included file changes the cache key", "[shadercompiler]") {
    Dirs d;
    Compiler c(d.include, d.cache, 1);
    const CompileResult before = c.CompileSync(Fragment(kIncluding));
    REQUIRE(before.ok);
    d.WriteInclude("float LumaHalf(float x) { return x / 2.0; }\n");
    const CompileResult after = c.CompileSync(Fragment(kIncluding));
    REQUIRE(after.ok);
    CHECK(after.key != before.key);
    CHECK_FALSE(after.fromCache);
}

TEST_CASE("Defines and the stage are part of the key", "[shadercompiler]") {
    CompileRequest a = Fragment(kTrivial);
    CompileRequest b = a;
    b.defines.push_back({"LUMA_MASKED", "1"});
    CompileRequest v = a;
    v.stage = ShaderStage::Vertex;
    CHECK(Compiler::CacheKey(a, "") != Compiler::CacheKey(b, ""));
    CHECK(Compiler::CacheKey(a, "") != Compiler::CacheKey(v, ""));
    CHECK(Compiler::CacheKey(a, "") == Compiler::CacheKey(Fragment(kTrivial), ""));
}

TEST_CASE("Submitted jobs all finish through Poll", "[shadercompiler]") {
    Dirs d;
    Compiler c(d.include, d.cache, 4);
    std::vector<u64> tickets;
    for (int i = 0; i < 8; ++i) {
        CompileRequest r = Fragment(kTrivial);
        r.defines.push_back({"VARIANT", std::to_string(i)});  // distinct keys: real compiles
        tickets.push_back(c.Submit(std::move(r)));
    }
    std::vector<std::pair<u64, CompileResult>> done;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (done.size() < tickets.size() && std::chrono::steady_clock::now() < deadline) {
        for (auto& r : c.Poll()) done.push_back(std::move(r));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(done.size() == 8);
    for (const auto& [ticket, result] : done) {
        CHECK(std::find(tickets.begin(), tickets.end(), ticket) != tickets.end());
        CHECK(result.ok);
    }
}

TEST_CASE("SPIR-V stats count texture samples and samplers", "[shadercompiler]") {
    Dirs d;
    Compiler c(d.include, d.cache, 1);
    const CompileResult r = c.CompileSync(Fragment(
        "#version 450\nlayout(binding = 0) uniform sampler2D tex;\nlayout(location = 0) in vec2 uv;\n"
        "layout(location = 0) out vec4 o;\nvoid main() { o = texture(tex, uv) * 2.0; }\n"));
    REQUIRE(r.ok);
    const SpirvStats s = Compiler::Stats(r.spirv);
    CHECK(s.textureSamples >= 1);
    CHECK(s.samplers == 1);
    CHECK(s.instructions > 0);
}
