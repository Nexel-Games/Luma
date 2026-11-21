#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "Luma/AssetImport/ImportMetadata.h"
#include "Luma/AssetImport/SourceWatch.h"
#include "Luma/Jobs/JobSystem.h"

using namespace Luma;
using namespace Luma::Import;

namespace {

struct Dir {
    std::filesystem::path root;
    explicit Dir(const char* name) : root(std::filesystem::temp_directory_path() / name) {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }
    ~Dir() { std::filesystem::remove_all(root); }
};

void WriteBytes(const std::filesystem::path& file, const std::string& bytes) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << bytes;
}

i64 Mtime(const std::filesystem::path& file) {
    return static_cast<i64>(std::filesystem::last_write_time(file).time_since_epoch().count());
}

// An imported asset whose .meta matches `source` as it is now.
std::filesystem::path MakeImported(const Dir& dir, const std::filesystem::path& source) {
    const std::filesystem::path asset = dir.root / "SM_Thing.lmesh";
    WriteBytes(asset, "mesh");
    ImportMetadata meta;
    meta.type = AssetType::Mesh;
    meta.sourceFile = source.string();
    meta.sourceHash = HashFile(source);
    meta.sourceMtime = Mtime(source);
    meta.sourceSize = static_cast<i64>(std::filesystem::file_size(source));
    REQUIRE(WriteMetadata(asset, meta, nullptr));
    return asset;
}

void BumpMtime(const std::filesystem::path& file) {
    std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::seconds(10));
}

}  // namespace

TEST_CASE("HashFile is FNV-1a 64 of the bytes", "[import][source]") {
    Dir dir("luma-source-hash");
    WriteBytes(dir.root / "abc.bin", "abc");
    REQUIRE(HashFile(dir.root / "abc.bin") == "e71fa2190541574b");
    WriteBytes(dir.root / "empty.bin", "");
    REQUIRE(HashFile(dir.root / "empty.bin") == "cbf29ce484222325");
}

TEST_CASE("An unchanged source is checked without reading its bytes", "[import][source]") {
    Dir dir("luma-source-unchanged");
    const auto source = dir.root / "src.fbx";
    WriteBytes(source, std::string(4096, 'x'));
    const auto asset = MakeImported(dir, source);

    SourceCheckStats stats;
    REQUIRE(CheckSource(asset, &stats) == SourceState::UpToDate);
    REQUIRE_FALSE(stats.hashed);
}

TEST_CASE("Edited and touched sources", "[import][source]") {
    Dir dir("luma-source-edited");
    const auto source = dir.root / "src.fbx";
    WriteBytes(source, std::string(4096, 'x'));
    const auto asset = MakeImported(dir, source);

    SECTION("same size, new bytes, new mtime -> Changed") {
        WriteBytes(source, std::string(4096, 'y'));
        BumpMtime(source);
        SourceCheckStats stats;
        REQUIRE(CheckSource(asset, &stats) == SourceState::Changed);
        REQUIRE(stats.hashed);
    }
    SECTION("size differs -> Changed without hashing") {
        WriteBytes(source, std::string(5000, 'x'));
        SourceCheckStats stats;
        REQUIRE(CheckSource(asset, &stats) == SourceState::Changed);
        REQUIRE_FALSE(stats.hashed);
    }
    SECTION("only the mtime moved -> UpToDate, hashed once per stamp") {
        BumpMtime(source);
        SourceCheckStats first;
        u64 announced = 0;
        first.beforeHash = [&](const std::filesystem::path&, u64 bytes) { announced = bytes; };
        REQUIRE(CheckSource(asset, &first) == SourceState::UpToDate);
        REQUIRE(first.hashed);
        REQUIRE(announced == 4096);
        SourceCheckStats second;
        REQUIRE(CheckSource(asset, &second) == SourceState::UpToDate);
        REQUIRE_FALSE(second.hashed);
    }
}

TEST_CASE("Missing source and non-imported assets", "[import][source]") {
    Dir dir("luma-source-missing");
    const auto source = dir.root / "src.fbx";
    WriteBytes(source, "abc");
    const auto asset = MakeImported(dir, source);
    std::filesystem::remove(source);
    REQUIRE(CheckSource(asset) == SourceState::Missing);

    WriteBytes(dir.root / "T_Plain.png", "png");
    REQUIRE(CheckSource(dir.root / "T_Plain.png") == SourceState::NotImported);
}

TEST_CASE("SourceWatch answers from its cache and checks in the background", "[import][source]") {
    Dir dir("luma-source-watch");
    const auto source = dir.root / "src.fbx";
    WriteBytes(source, std::string(64, 'x'));
    const auto asset = MakeImported(dir, source);

    SECTION("inline (no job system)") {
        SourceWatch watch;
        REQUIRE(watch.Query(asset) == SourceState::Unknown);
        watch.Update();
        REQUIRE(watch.Query(asset) == SourceState::UpToDate);

        WriteBytes(source, std::string(65, 'x'));
        // Cached until invalidated (or the recheck interval passes).
        REQUIRE(watch.Query(asset) == SourceState::UpToDate);
        watch.Invalidate(asset);
        REQUIRE(watch.Query(asset) == SourceState::Unknown);
        watch.Update();
        REQUIRE(watch.IsChanged(asset));
    }
    SECTION("on the job system") {
        Jobs::JobSystem jobs(2);
        SourceWatch watch(&jobs);
        REQUIRE(watch.Query(asset) == SourceState::Unknown);
        watch.Update();
        watch.WaitIdle();
        REQUIRE(watch.Query(asset) == SourceState::UpToDate);
        watch.InvalidateAll();
        REQUIRE(watch.Query(asset) == SourceState::Unknown);
    }
}

TEST_CASE("Renaming an asset moves its .meta with it", "[import][rename]") {
    Dir dir("luma-rename-meta");
    WriteBytes(dir.root / "SM_Arm.lmesh", "mesh");
    WriteBytes(dir.root / "SM_Arm.lmesh.meta", "{}");
    std::string warning;
    REQUIRE(RenameAssetFile(dir.root / "SM_Arm.lmesh", dir.root / "SM_RobotArm.lmesh", &warning));
    CHECK(warning.empty());
    CHECK(std::filesystem::exists(dir.root / "SM_RobotArm.lmesh"));
    CHECK(std::filesystem::exists(dir.root / "SM_RobotArm.lmesh.meta"));
    CHECK_FALSE(std::filesystem::exists(dir.root / "SM_Arm.lmesh"));
    CHECK_FALSE(std::filesystem::exists(dir.root / "SM_Arm.lmesh.meta"));

    // No sidecar: just the file. A taken name fails and changes nothing.
    WriteBytes(dir.root / "T_A.png", "png");
    WriteBytes(dir.root / "T_B.png", "png");
    CHECK(RenameAssetFile(dir.root / "T_A.png", dir.root / "T_C.png", &warning));
    std::string err;
    CHECK_FALSE(RenameAssetFile(dir.root / "T_C.png", dir.root / "T_B.png", &err));
    CHECK_FALSE(err.empty());
    CHECK(std::filesystem::exists(dir.root / "T_C.png"));
}
