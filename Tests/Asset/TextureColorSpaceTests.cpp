#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "Luma/Asset/LumaTextureFormat.h"
#include "Luma/Asset/TextureColorSpace.h"
#include "Luma/Asset/TextureImporter.h"
#include "Luma/Material/MaterialTypes.h"

namespace fs = std::filesystem;

TEST_CASE("Texture color space is inferred from token-delimited names", "[texture]") {
    struct Row { const char* name; bool srgb; };
    const Row rows[] = {
        {"Barrel_02_diff_2k.png", true},
        {"barrel_02_nor_gl_2k.png", false},
        {"T_Rock_ORM.png", false},
        {"Snormal.png", true},  // "normal" is not token-delimited here
        {"rock-Roughness.jpg", false},
        {"metal plate.metal.png", false},
        {"wall_AO.tga", false},
        {"leaf_opacity.png", false},
        {"T_Brick_BaseColor.png", true},
    };
    for (const Row& r : rows) {
        INFO(r.name);
        CHECK(Luma::IsTextureSRGB(r.name, nullptr) == r.srgb);
    }
}

TEST_CASE("Texture metadata sRGB flag overrides the name", "[texture]") {
    Luma::LumaTextureMetadata meta;
    meta.isSRGB = false;
    CHECK_FALSE(Luma::IsTextureSRGB("Barrel_02_diff_2k.png", &meta));
    meta.isSRGB = true;
    CHECK(Luma::IsTextureSRGB("barrel_02_nor_gl_2k.png", &meta));
}

TEST_CASE("Texture view is sRGB only for color parameters", "[texture]") {
    using namespace Luma::Material;
    CHECK(SelectTextureView(MaterialParameter::Normal, true) == TextureView::Linear);
    CHECK(SelectTextureView(MaterialParameter::BaseColor, true) == TextureView::SRGB);
    CHECK(SelectTextureView(MaterialParameter::BaseColor, false) == TextureView::Linear);
    CHECK(SelectTextureView(MaterialParameter::Roughness, true) == TextureView::Linear);
}

TEST_CASE("Texture import keeps source pixels for sRGB textures", "[texture]") {
    const fs::path dir = fs::temp_directory_path() / "luma_texture_colorspace_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // 2x1 uncompressed true-color TGA, top-left origin, pixels stored BGR.
    const std::vector<Luma::u8> rgb = {64, 128, 200, 10, 20, 30};
    {
        std::ofstream f(dir / "Albedo_diff.tga", std::ios::binary);
        const unsigned char header[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 0, 24, 0x20};
        f.write(reinterpret_cast<const char*>(header), sizeof(header));
        for (size_t i = 0; i < rgb.size(); i += 3) {
            const char bgr[3] = {char(rgb[i + 2]), char(rgb[i + 1]), char(rgb[i])};
            f.write(bgr, 3);
        }
    }

    Luma::TextureImporter importer;
    const auto result = importer.Import(dir / "Albedo_diff.tga", R"({"sRGB":true})", dir);
    REQUIRE(result.status == Luma::ImportStatus::Completed);

    Luma::LumaTextureHeader header;
    std::vector<Luma::u8> pixels(rgb.size());
    {
        std::ifstream in(result.nativePath, std::ios::binary);
        in.read(reinterpret_cast<char*>(&header), sizeof(header));
        in.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    }
    CHECK(header.width == 2);
    CHECK(header.height == 1);
    CHECK(static_cast<Luma::TextureFormat>(header.format) == Luma::TextureFormat::SRGB8);
    CHECK(pixels == rgb);

    fs::remove_all(dir);
}
