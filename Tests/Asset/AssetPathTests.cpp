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
