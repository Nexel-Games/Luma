// Material and mesh thumbnails render through the GPU callback the editor
// installs on the ThumbnailManager (the engine module itself has no renderer).
// These tests install a stub callback and check the manager's contract:
// the callback receives the asset, its pixels become the cached PNG, and a
// second request is served from the cache without rendering again.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Luma/Asset/AssetId.h"
#include "Luma/Asset/MaterialThumbnailRenderer.h"
#include "Luma/Asset/MeshThumbnailRenderer.h"
#include "Luma/Asset/ThumbnailRenderer.h"

namespace fs = std::filesystem;
using namespace Luma;

namespace {

struct ThumbnailFixture {
    fs::path root = fs::temp_directory_path() / "luma_thumbnail_callback_test";
    int calls = 0;
    AssetType lastType = AssetType::Unknown;
    fs::path lastPath;

    ThumbnailFixture() {
        fs::remove_all(root);
        fs::create_directories(root / "Assets");
        auto& mgr = ThumbnailManager::Get();
        mgr.Initialize();
        mgr.SetProjectRoot(root);
        mgr.RegisterRenderer(AssetType::Material, std::make_unique<MaterialThumbnailRenderer>());
        mgr.RegisterRenderer(AssetType::Mesh, std::make_unique<MeshThumbnailRenderer>());
        mgr.SetRenderCallback([this](AssetType type, const fs::path& path, u32 width,
                                     u32 height, std::vector<u8>& rgba) {
            ++calls;
            lastType = type;
            lastPath = path;
            rgba.assign(static_cast<usize>(width) * height * 4, 0);
            for (usize i = 0; i < rgba.size(); i += 4) {
                rgba[i] = 200;      // a recognizable color
                rgba[i + 1] = 40;
                rgba[i + 2] = 10;
                rgba[i + 3] = 255;
            }
            return true;
        });
    }
    ~ThumbnailFixture() {
        ThumbnailManager::Get().SetRenderCallback({});
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    fs::path Write(const char* name, const std::string& content) {
        const fs::path file = root / "Assets" / name;
        std::ofstream(file, std::ios::binary) << content;
        return file;
    }
};

AssetId Id(const fs::path& p) { return MakeAssetIdFromKey(p.string()); }

}  // namespace

TEST_CASE("Material thumbnails render through the installed callback", "[thumbnail]") {
    ThumbnailFixture f;
    const fs::path mat = f.Write("M_Red.lmat", R"({"version":3,"type":"Material","name":"M_Red"})");
    ThumbnailSettings settings;
    settings.width = settings.height = 64;

    auto thumb = ThumbnailManager::Get().GetThumbnail(Id(mat), mat, settings);
    REQUIRE(thumb.has_value());
    REQUIRE(thumb->IsValid());
    CHECK(f.calls == 1);
    CHECK(f.lastType == AssetType::Material);
    CHECK(f.lastPath == mat);
    CHECK(thumb->width == 64);
    CHECK(thumb->pixels[0] == 200);
    CHECK(thumb->pixels[1] == 40);

    // Cached: the second request does not render again.
    auto again = ThumbnailManager::Get().GetThumbnail(Id(mat), mat, settings);
    REQUIRE(again.has_value());
    CHECK(f.calls == 1);
}

TEST_CASE("Material instance and mesh thumbnails use the callback", "[thumbnail]") {
    ThumbnailFixture f;
    const fs::path inst = f.Write("MI_Red.lmatinst", R"({"version":3,"type":"MaterialInstance"})");
    const fs::path mesh = f.Write("Rock.lmesh", "LMESH");
    ThumbnailSettings settings;
    settings.width = settings.height = 32;

    REQUIRE(ThumbnailManager::Get().GetThumbnail(Id(inst), inst, settings).has_value());
    CHECK(f.lastType == AssetType::Material);
    REQUIRE(ThumbnailManager::Get().GetThumbnail(Id(mesh), mesh, settings).has_value());
    CHECK(f.lastType == AssetType::Mesh);
    CHECK(f.calls == 2);
}

TEST_CASE("Without a render callback no material thumbnail is produced", "[thumbnail]") {
    ThumbnailFixture f;
    ThumbnailManager::Get().SetRenderCallback({});
    const fs::path mat = f.Write("M_Blue.lmat", R"({"version":3,"type":"Material","name":"M_Blue"})");
    CHECK_FALSE(ThumbnailManager::Get().GetThumbnail(Id(mat), mat).has_value());
}
