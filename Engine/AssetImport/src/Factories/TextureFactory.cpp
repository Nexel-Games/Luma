#include "Luma/AssetImport/Factories/TextureFactory.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <vector>

#pragma warning(push, 0)
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#pragma warning(pop)

#include "Luma/Asset/ImageDecode.h"

namespace Luma::Import {
namespace {

FactoryResult Failed(std::string error) {
    FactoryResult result;
    result.error = std::move(error);
    return result;
}

int MaxEdge(const SettingsBlock& textures) {
    const std::string value = textures.GetString("maxSize");
    if (value.empty() || value == "None") {
        return 0;
    }
    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        return 0;
    }
}

bool IsPsd(const ImportNode& node) {
    std::string ext = node.textureFile.extension().string();
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return ext == ".psd" || node.textureEmbeddedExt == "psd" || node.textureEmbeddedExt == ".psd";
}

std::vector<u8> ReadFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::vector<u8>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<u8> Downscale(const DecodedImage& image, int width, int height) {
    std::vector<u8> out(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    auto sample = [&](int x, int y, int channel) -> f32 {
        const size_t index = (static_cast<size_t>(y) * image.width + static_cast<size_t>(x)) * 4 +
                             static_cast<size_t>(channel);
        if (image.isFloat) {
            return image.pixelsF[index];
        }
        return static_cast<f32>(image.pixels8[index]) / 255.0f;
    };
    for (int y = 0; y < height; ++y) {
        const int y0 = y * static_cast<int>(image.height) / height;
        int y1 = (y + 1) * static_cast<int>(image.height) / height;
        if (y1 <= y0) {
            y1 = y0 + 1;
        }
        for (int x = 0; x < width; ++x) {
            const int x0 = x * static_cast<int>(image.width) / width;
            int x1 = (x + 1) * static_cast<int>(image.width) / width;
            if (x1 <= x0) {
                x1 = x0 + 1;
            }
            f32 sum[4] = {};
            int count = 0;
            for (int sy = y0; sy < y1; ++sy) {
                for (int sx = x0; sx < x1; ++sx) {
                    for (int channel = 0; channel < 4; ++channel) {
                        sum[channel] += sample(sx, sy, channel);
                    }
                    ++count;
                }
            }
            u8* pixel = out.data() + (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
            for (int channel = 0; channel < 4; ++channel) {
                const f32 value = count == 0 ? 0.0f : sum[channel] / static_cast<f32>(count);
                const f32 encoded = image.isFloat ? value : value;
                pixel[channel] = static_cast<u8>(std::clamp(encoded, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        }
    }
    return out;
}

bool WritePng(const std::filesystem::path& dest, const u8* pixels, int width, int height, std::string* err) {
    int length = 0;
    unsigned char* png = stbi_write_png_to_mem(pixels, width * 4, width, height, 4, &length);
    if (png == nullptr || length <= 0) {
        if (err != nullptr) {
            *err = "Could not encode '" + dest.filename().string() + "'";
        }
        return false;
    }
    const bool wrote = WriteFileAtomic(dest, png, static_cast<std::size_t>(length), err);
    STBIW_FREE(png);
    return wrote;
}

}  // namespace

FactoryResult CreateTextureAsset(const AssetRequest& request, const FactoryContext& context) {
    if (request.sources.empty() || request.sources.front() >= context.scene.nodes.size()) {
        return Failed("Texture request has no source node");
    }
    const ImportNode& node = context.scene.nodes[request.sources.front()];
    const int maxEdge = MaxEdge(context.settings.Textures());
    const bool psd = IsPsd(node);

    std::string error;
    std::optional<DecodedImage> decoded;
    if (psd || maxEdge > 0) {
        if (!node.textureEmbedded.empty()) {
            decoded = DecodeImageMemory(node.textureEmbedded, node.textureEmbeddedExt, &error);
        } else {
            decoded = DecodeImageFile(node.textureFile, &error);
        }
        if (!decoded) {
            return Failed(error.empty() ? "Could not decode the texture" : error);
        }
    }

    bool reencode = psd;
    int width = decoded ? static_cast<int>(decoded->width) : 0;
    int height = decoded ? static_cast<int>(decoded->height) : 0;
    std::vector<u8> pixels;
    if (decoded && maxEdge > 0 && (width > maxEdge || height > maxEdge)) {
        const double scale = std::min(static_cast<double>(maxEdge) / width, static_cast<double>(maxEdge) / height);
        width = std::max(1, static_cast<int>(std::lround(width * scale)));
        height = std::max(1, static_cast<int>(std::lround(height * scale)));
        pixels = Downscale(*decoded, width, height);
        reencode = true;
    } else if (psd && decoded) {
        if (decoded->isFloat) {
            pixels.resize(decoded->pixelsF.size());
            for (size_t i = 0; i < decoded->pixelsF.size(); ++i) {
                pixels[i] = static_cast<u8>(std::clamp(decoded->pixelsF[i], 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        } else {
            pixels = decoded->pixels8;
        }
        width = static_cast<int>(decoded->width);
        height = static_cast<int>(decoded->height);
    }

    if (ImportCancelled(context)) {
        return Failed("Import was cancelled");
    }

    std::filesystem::path dest = request.destFile;
    if (reencode) {
        dest.replace_extension(".png");
        if (!WritePng(dest, pixels.data(), width, height, &error)) {
            return Failed(error.empty() ? "Could not write the texture" : error);
        }
    } else {
        std::vector<u8> bytes = node.textureEmbedded.empty() ? ReadFile(node.textureFile) : node.textureEmbedded;
        if (bytes.empty()) {
            return Failed("Texture '" + node.name + "' has no image data");
        }
        if (!WriteFileAtomic(dest, bytes.data(), bytes.size(), &error)) {
            return Failed(error.empty() ? "Could not write the texture" : error);
        }
    }

    SerialValue settings = context.settings.ToJson();
    settings["colorSpace"] = context.settings.Textures().GetString("colorSpace");
    return FinishAsset(context, request, dest, &node, "Luma.Import.Image", std::move(settings));
}

}  // namespace Luma::Import
