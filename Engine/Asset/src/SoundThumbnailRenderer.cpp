#include "Luma/Asset/SoundThumbnailRenderer.h"

#include "Luma/Audio/AudioDecode.h"
#include "Luma/Core/Log.h"

#include <algorithm>
#include <cmath>
#include <string>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace Luma {
namespace {

std::string LowerExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return ext;
}

void Put(std::vector<u8>& pixels, u32 width, int x, int y, u8 r, u8 g, u8 b) {
    const size_t offset = (static_cast<size_t>(y) * width + static_cast<size_t>(x)) * 4;
    pixels[offset + 0] = r;
    pixels[offset + 1] = g;
    pixels[offset + 2] = b;
    pixels[offset + 3] = 255;
}

}  // namespace

bool SoundThumbnailRenderer::CanRender(const AssetId& assetId, const std::filesystem::path& nativePath) const {
    (void)assetId;
    std::error_code error;
    if (!std::filesystem::exists(nativePath, error)) return false;
    const std::string ext = LowerExtension(nativePath);
    return ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac";
}

bool SoundThumbnailRenderer::RenderThumbnail(const AssetId& assetId, const std::filesystem::path& nativePath,
                                            const std::filesystem::path& outputPath, u32 width, u32 height) {
    (void)assetId;
    if (width == 0 || height == 0) return false;

    std::string error;
    const auto peaks = Luma::Audio::ComputePeaks(nativePath, width, &error);
    if (!peaks || peaks->min.size() != width || peaks->max.size() != width) {
        LUMA_LOG_WARN("SoundThumbnail", "Could not build a waveform for {}: {}", nativePath.filename().string(),
                      error);
        return false;
    }

    std::vector<u8> pixels(static_cast<size_t>(width) * height * 4, 0);
    for (u32 i = 0; i < width * height; ++i) {
        pixels[i * 4 + 0] = 0x26;
        pixels[i * 4 + 1] = 0x26;
        pixels[i * 4 + 2] = 0x26;
        pixels[i * 4 + 3] = 255;
    }

    const int mid = static_cast<int>(height / 2);
    const int margin = 4;
    for (u32 x = 0; x < width; ++x) {
        Put(pixels, width, static_cast<int>(x), mid, 0x3A, 0x3A, 0x3A);
    }

    const float half = static_cast<float>(mid) - static_cast<float>(margin);
    const int x0 = std::min(margin, static_cast<int>(width));
    const int x1 = std::max(x0, static_cast<int>(width) - margin);
    for (int x = x0; x < x1; ++x) {
        const float topValue = peaks->max[static_cast<size_t>(x)];
        const float bottomValue = peaks->min[static_cast<size_t>(x)];
        int yTop = static_cast<int>(std::lround(static_cast<float>(mid) - topValue * half));
        int yBottom = static_cast<int>(std::lround(static_cast<float>(mid) - bottomValue * half));
        if (yTop > yBottom) std::swap(yTop, yBottom);
        yTop = std::clamp(yTop, margin, static_cast<int>(height) - 1 - margin);
        yBottom = std::clamp(yBottom, margin, static_cast<int>(height) - 1 - margin);
        for (int y = yTop; y <= yBottom; ++y) {
            Put(pixels, width, x, y, 0x2D, 0x9C, 0xDB);
        }
    }

    std::error_code io;
    std::filesystem::create_directories(outputPath.parent_path(), io);
    const int wrote = stbi_write_png(outputPath.string().c_str(), static_cast<int>(width), static_cast<int>(height), 4,
                                     pixels.data(), static_cast<int>(width * 4));
    if (wrote == 0) {
        LUMA_LOG_WARN("SoundThumbnail", "Could not write {}", outputPath.string());
        return false;
    }
    return true;
}

}  // namespace Luma
