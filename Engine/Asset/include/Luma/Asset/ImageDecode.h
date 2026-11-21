#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Types.h"

namespace Luma::Import {

struct DecodedImage {
    u32 width = 0;
    u32 height = 0;
    u32 channels = 0;  // source channels, 1..4; buffers below are always RGBA
    bool isFloat = false;
    std::vector<u8> pixels8;    // RGBA8 when !isFloat
    std::vector<f32> pixelsF;   // RGBA32F when isFloat
};

std::optional<DecodedImage> DecodeImageFile(const std::filesystem::path& file, std::string* err);
std::optional<DecodedImage> DecodeImageMemory(std::span<const u8> bytes, std::string_view ext,
                                              std::string* err);

}  // namespace Luma::Import
