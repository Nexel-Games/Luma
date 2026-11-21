#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

#include "Luma/Asset/ImageDecode.h"
#include "Luma/AssetImport/Translators/ImageTranslator.h"

using Catch::Approx;
using namespace Luma;
using namespace Luma::Import;

namespace {

std::filesystem::path Fixture(const char* name) {
    return std::filesystem::path(LUMA_SOURCE_DIR) / "Tests" / "Fixtures" / "Import" / name;
}

void RequireChannel(int got, int expected, int margin) {
    REQUIRE(std::abs(got - expected) <= margin);
}

void RequireLdrPixel(const DecodedImage& image, int margin, int alpha) {
    REQUIRE(image.width == 32);
    REQUIRE(image.height == 16);
    REQUIRE_FALSE(image.isFloat);
    REQUIRE(image.pixels8.size() == 32u * 16u * 4u);
    const usize i = (2u * image.width + 3u) * 4u;
    RequireChannel(image.pixels8[i + 0], 24, margin);
    RequireChannel(image.pixels8[i + 1], 32, margin);
    RequireChannel(image.pixels8[i + 2], 128, margin);
    REQUIRE(image.pixels8[i + 3] == static_cast<u8>(alpha));
}

void RequireFloatPixel(const DecodedImage& image, double margin) {
    REQUIRE(image.width == 4);
    REQUIRE(image.height == 2);
    REQUIRE(image.isFloat);
    REQUIRE(image.pixelsF.size() == 4u * 2u * 4u);
    REQUIRE(image.pixelsF[0] == Approx(2.0f).margin(margin));
    REQUIRE(image.pixelsF[1] == Approx(0.5f).margin(margin));
    REQUIRE(image.pixelsF[2] == Approx(0.25f).margin(margin));
}

const ImportNode* FindKind(const ImportScene& scene, NodeKind kind) {
    for (const ImportNode& node : scene.nodes) {
        if (node.kind == kind) {
            return &node;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("Fixture images decode to the authored pixels", "[import][image]") {
    const char* ldr[] = {"img_rgba.png", "img.jpg", "img.tga", "img.bmp", "img.psd"};
    for (const char* name : ldr) {
        std::string error;
        const auto image = DecodeImageFile(Fixture(name), &error);
        REQUIRE(image);
        INFO(name);
        const bool jpeg = std::string(name).ends_with(".jpg");
        const bool authoredAlpha = std::string(name) == "img_rgba.png" || std::string(name) == "img.tga";
        RequireLdrPixel(*image, jpeg ? 6 : 0, authoredAlpha ? 24 : 255);
    }

    std::string error;
    const auto hdr = DecodeImageFile(Fixture("img.hdr"), &error);
    REQUIRE(hdr);
    RequireFloatPixel(*hdr, 2.0 * 0.02);

    const auto exr = DecodeImageFile(Fixture("img.exr"), &error);
    INFO(error);
    REQUIRE(exr);
    RequireFloatPixel(*exr, 1e-3);

    std::ifstream png(Fixture("img_rgba.png"), std::ios::binary);
    std::vector<u8> bytes((std::istreambuf_iterator<char>(png)), std::istreambuf_iterator<char>());
    const auto fromMemory = DecodeImageMemory(bytes, "png", &error);
    REQUIRE(fromMemory);
    RequireLdrPixel(*fromMemory, 0, 24);
}

TEST_CASE("Image translator reports one texture and rejects a corrupt file", "[import][image]") {
    ImageTranslator translator;
    REQUIRE(translator.Name() == "Luma.Import.Image");
    REQUIRE(translator.CanTranslate(Fixture("img_rgba.png")));
    REQUIRE(translator.CanTranslate(Fixture("img.jpg")));
    REQUIRE(translator.CanTranslate(Fixture("img.hdr")));
    REQUIRE(translator.CanTranslate(Fixture("img.exr")));
    REQUIRE_FALSE(translator.CanTranslate(Fixture("robot.gltf")));

    const char* files[] = {"img_rgba.png", "img.jpg", "img.tga", "img.bmp", "img.psd", "img.hdr", "img.exr"};
    for (const char* name : files) {
        ImportScene scene;
        std::string error;
        const bool ok = translator.Translate(Fixture(name), scene, &error);
        INFO(name);
        INFO(error);
        REQUIRE(ok);
        REQUIRE(error.empty());
        REQUIRE(scene.OfKind(NodeKind::File).size() == 1);
        REQUIRE(scene.OfKind(NodeKind::Texture).size() == 1);
        const ImportNode* texture = FindKind(scene, NodeKind::Texture);
        REQUIRE(texture);
        REQUIRE(texture->name == std::filesystem::path(name).stem().string());
        const bool linear = std::string(name).ends_with(".hdr") || std::string(name).ends_with(".exr");
        REQUIRE(texture->textureIsLinear == linear);
    }

    const auto temp = std::filesystem::temp_directory_path() / "luma-import-corrupt.png";
    {
        std::ofstream out(temp, std::ios::binary);
        const unsigned char junk[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        out.write(reinterpret_cast<const char*>(junk), 10);
    }
    ImportScene scene;
    std::string error;
    REQUIRE_FALSE(translator.Translate(temp, scene, &error));
    REQUIRE_FALSE(error.empty());
    std::filesystem::remove(temp);
}
