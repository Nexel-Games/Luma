#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "Luma/Asset/ImageDecode.h"
#include "Luma/Asset/SoundThumbnailRenderer.h"
#include "Luma/Asset/TextureThumbnailRenderer.h"

using namespace Luma;

namespace {

std::filesystem::path Fixture(const char* name) {
    return std::filesystem::path(LUMA_SOURCE_DIR) / "Tests" / "Fixtures" / "Import" / name;
}

int SrgbByte(float linear) {
    const float mapped = linear / (1.0f + linear);
    const float encoded = mapped <= 0.0031308f ? 12.92f * mapped
                                               : 1.055f * std::pow(mapped, 1.0f / 2.4f) - 0.055f;
    return static_cast<int>(std::lround(std::clamp(encoded, 0.0f, 1.0f) * 255.0f));
}

bool Near(int value, int expected) { return std::abs(value - expected) <= 3; }

}  // namespace

TEST_CASE("Sound thumbnail draws a deterministic waveform", "[import][thumbnail]") {
    const auto wav = Fixture("tone_mono.wav");
    const auto first = std::filesystem::temp_directory_path() / "luma-sound-thumb-a.png";
    const auto second = std::filesystem::temp_directory_path() / "luma-sound-thumb-b.png";
    std::filesystem::remove(first);
    std::filesystem::remove(second);

    SoundThumbnailRenderer renderer;
    REQUIRE(renderer.RenderThumbnail({}, wav, first, 128, 128));
    REQUIRE(renderer.RenderThumbnail({}, wav, second, 128, 128));

    std::string error;
    const auto image = Import::DecodeImageFile(first, &error);
    REQUIRE(image);
    REQUIRE(image->width == 128);
    REQUIRE(image->height == 128);
    REQUIRE_FALSE(image->isFloat);

    const auto at = [&](int x, int y) -> const std::uint8_t* {
        return image->pixels8.data() + (static_cast<size_t>(y) * 128 + static_cast<size_t>(x)) * 4;
    };
    const std::uint8_t* corner = at(0, 0);
    REQUIRE(corner[0] == 0x26);
    REQUIRE(corner[1] == 0x26);
    REQUIRE(corner[2] == 0x26);

    bool above = false;
    bool below = false;
    for (int y = 0; y < 128; ++y) {
        const std::uint8_t* pixel = at(64, y);
        const bool accent = pixel[0] == 0x2D && pixel[1] == 0x9C && pixel[2] == 0xDB;
        if (accent && y < 64) above = true;
        if (accent && y > 64) below = true;
    }
    REQUIRE(above);
    REQUIRE(below);

    const auto again = Import::DecodeImageFile(second, &error);
    REQUIRE(again);
    REQUIRE(again->pixels8 == image->pixels8);
    std::filesystem::remove(first);
    std::filesystem::remove(second);
}

TEST_CASE("HDR, EXR, and PSD thumbnails decode through the image importer", "[import][thumbnail]") {
    TextureThumbnailRenderer renderer;
    const auto out = std::filesystem::temp_directory_path() / "luma-tex-thumb.png";

    auto render = [&](const char* name) {
        std::filesystem::remove(out);
        REQUIRE(renderer.RenderThumbnail({}, Fixture(name), out, 128, 128));
        std::string error;
        const auto image = Import::DecodeImageFile(out, &error);
        INFO(error);
        REQUIRE(image);
        REQUIRE(image->width == 128);
        REQUIRE(image->height == 128);
        return image;
    };

    const auto hdr = render("img.hdr");
    const std::uint8_t* pixel = hdr->pixels8.data() + (64 * 128 + 64) * 4;
    REQUIRE(Near(pixel[0], SrgbByte(2.0f)));
    REQUIRE(Near(pixel[1], SrgbByte(0.5f)));
    REQUIRE(Near(pixel[2], SrgbByte(0.25f)));

    REQUIRE(render("img.exr"));
    REQUIRE(render("img.psd"));
    std::filesystem::remove(out);
}
