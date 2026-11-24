#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "Luma/Asset/AssetPath.h"

namespace fs = std::filesystem;
using Luma::AssetPathResolver;

namespace {
struct Roots {
    fs::path base;
    fs::path game;
    fs::path engine;
    Roots() {
        base = fs::temp_directory_path() / "luma_assetpath_tests";
        fs::remove_all(base);
        game = base / "Project" / "Assets";
        engine = base / "Content" / "Engine";
        fs::create_directories(game / "Materials");
        fs::create_directories(engine / "Materials");
        std::ofstream(game / "Materials" / "M_Wood.lmat") << "{}";
        std::ofstream(engine / "Materials" / "DefaultMaterial.lmat") << "{}";
    }
    ~Roots() { fs::remove_all(base); }
};
}  // namespace

TEST_CASE("AssetPath maps /Game and /Engine to roots", "[assetpath]") {
    Roots r;
    AssetPathResolver resolver;
    resolver.SetGameRoot(r.game);
    resolver.SetEngineRoot(r.engine);

    auto wood = resolver.ToFile("/Game/Materials/M_Wood");
    REQUIRE(wood.has_value());
    CHECK(fs::equivalent(*wood, r.game / "Materials" / "M_Wood.lmat"));

    auto def = resolver.ToFile("/Engine/Materials/DefaultMaterial");
    REQUIRE(def.has_value());
    CHECK(fs::equivalent(*def, r.engine / "Materials" / "DefaultMaterial.lmat"));

    auto back = resolver.ToVirtual(r.game / "Materials" / "M_Wood.lmat");
    REQUIRE(back.has_value());
    CHECK(*back == "/Game/Materials/M_Wood");
    CHECK(resolver.ToVirtual(r.engine / "Materials" / "DefaultMaterial.lmat") ==
          std::optional<std::string>("/Engine/Materials/DefaultMaterial"));
}

TEST_CASE("AssetPath rejects paths outside both roots", "[assetpath]") {
    Roots r;
    AssetPathResolver resolver;
    resolver.SetGameRoot(r.game);
    resolver.SetEngineRoot(r.engine);
    CHECK_FALSE(resolver.ToVirtual(r.base / "elsewhere.lmat").has_value());
    CHECK_FALSE(resolver.ToFile("/Game/Materials/Missing").has_value());
    CHECK_FALSE(resolver.ToFile("/Other/Materials/M_Wood").has_value());
    CHECK_FALSE(resolver.ToFile("Materials/M_Wood").has_value());
}

TEST_CASE("AssetPath normalizes Windows separators", "[assetpath]") {
    Roots r;
    AssetPathResolver resolver;
    resolver.SetGameRoot(r.game);
    const fs::path windowsStyle =
        fs::path(r.game.string() + "\\Materials\\M_Wood.lmat");
    CHECK(resolver.ToVirtual(windowsStyle) ==
          std::optional<std::string>("/Game/Materials/M_Wood"));
}

TEST_CASE("Plugin mounts resolve /Name/ only while mounted", "[assetpath]") {
    Roots r;
    fs::path plugin = r.base / "Plugins" / "Pack" / "Content";
    fs::create_directories(plugin / "Materials");
    std::ofstream(plugin / "Materials" / "M_Pack.lmat") << "{}";
    AssetPathResolver resolver;
    resolver.SetGameRoot(r.game);
    resolver.AddKnownPlugin("Pack");
    CHECK(resolver.Classify("/Pack/Materials/M_Pack") ==
          AssetPathResolver::Status::MissingPlugin);
    CHECK_FALSE(resolver.ToFile("/Pack/Materials/M_Pack").has_value());
    REQUIRE(resolver.MountPlugin("Pack", plugin));
    auto file = resolver.ToFile("/Pack/Materials/M_Pack");
    REQUIRE(file.has_value());
    CHECK(*resolver.ToVirtual(*file) == "/Pack/Materials/M_Pack");
    resolver.UnmountPlugin("Pack");
    CHECK(resolver.Classify("/Pack/Materials/M_Pack") ==
          AssetPathResolver::Status::MissingPlugin);
    CHECK_FALSE(resolver.ToFile("/Pack/Materials/M_Pack").has_value());
}

TEST_CASE("Typed lookup disambiguates assets sharing a stem", "[assetpath]") {
    Roots r;
    std::ofstream(r.game / "Materials" / "M_Wood.png") << "png";  // same stem
    AssetPathResolver resolver;
    resolver.SetGameRoot(r.game);
    CHECK_FALSE(resolver.ToFile("/Game/Materials/M_Wood").has_value());  // ambiguous
    auto mat = resolver.ToFile("/Game/Materials/M_Wood", {"lmat", "lmatinst"});
    REQUIRE(mat.has_value());
    CHECK(mat->extension() == ".lmat");
    auto tex = resolver.ToFile("/Game/Materials/M_Wood", {"png", "jpg"});
    REQUIRE(tex.has_value());
    CHECK(tex->extension() == ".png");
    // Target path for a not-yet-written asset.
    auto fresh = resolver.MakeFilePath("/Game/Materials/MI_New", "lmatinst");
    REQUIRE(fresh.has_value());
    CHECK(fresh->filename() == "MI_New.lmatinst");
    CHECK_FALSE(resolver.MakeFilePath("/Nope/X", "lmat").has_value());
}
