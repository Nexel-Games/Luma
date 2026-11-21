#include "Luma/Asset/Import/ImageDecode.h"

#include <stb_image.h>
#include <tinyexr.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace Luma::Import {
namespace {

void SetError(std::string* err, const std::string& message) {
    if (err != nullptr) {
        *err = message;
    }
}

std::string NormalizeExt(std::string_view ext) {
    std::string out(ext);
    if (!out.empty() && out[0] == '.') {
        out.erase(out.begin());
    }
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

DecodedImage FromFloatRgba(const float* rgba, int width, int height, u32 channels) {
    DecodedImage image;
    image.width = static_cast<u32>(width);
    image.height = static_cast<u32>(height);
    image.channels = std::clamp(channels, 1u, 4u);
    image.isFloat = true;
    const usize count = static_cast<usize>(width) * static_cast<usize>(height) * 4u;
    image.pixelsF.assign(rgba, rgba + count);
    return image;
}

// ffmpeg's EXR writer omits pixelAspectRatio. OpenEXR's default is 1, and
// tinyexr rejects the file without it. Insert the attribute and shift the
// scanline offset table, which stores absolute file positions.
std::vector<u8> WithDefaultPixelAspect(std::span<const u8> bytes) {
    std::vector<u8> original(bytes.begin(), bytes.end());
    if (bytes.size() < 8 || bytes[0] != 0x76 || bytes[1] != 0x2f || bytes[2] != 0x31 || bytes[3] != 0x01) {
        return original;
    }
    const u32 version = static_cast<u32>(bytes[4]) | (static_cast<u32>(bytes[5]) << 8) |
                        (static_cast<u32>(bytes[6]) << 16) | (static_cast<u32>(bytes[7]) << 24);
    const bool singlePartScanline = (version & ~0xffu) == 0;
    usize i = 8;
    bool hasAspect = false;
    int minY = 0;
    int maxY = -1;
    bool sawWindow = false;
    while (i < bytes.size()) {
        if (bytes[i] == 0) {
            break;
        }
        const usize nameStart = i;
        while (i < bytes.size() && bytes[i] != 0) {
            ++i;
        }
        if (i >= bytes.size()) {
            return original;
        }
        const std::string name(reinterpret_cast<const char*>(bytes.data() + nameStart), i - nameStart);
        ++i;
        while (i < bytes.size() && bytes[i] != 0) {
            ++i;
        }
        if (i + 5 > bytes.size()) {
            return original;
        }
        ++i;
        uint32_t size = 0;
        std::memcpy(&size, bytes.data() + i, sizeof(size));
        i += sizeof(size);
        if (i + size > bytes.size()) {
            return original;
        }
        if (name == "pixelAspectRatio") {
            hasAspect = true;
        } else if (name == "dataWindow" && size >= 16) {
            std::memcpy(&minY, bytes.data() + i + 4, sizeof(minY));
            std::memcpy(&maxY, bytes.data() + i + 12, sizeof(maxY));
            sawWindow = true;
        }
        i += size;
    }
    if (hasAspect || !singlePartScanline || !sawWindow || maxY < minY || i >= bytes.size()) {
        return original;
    }
    const int chunks = maxY - minY + 1;
    const usize insertAt = i;
    std::vector<u8> patched;
    patched.reserve(bytes.size() + 32);
    patched.insert(patched.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(insertAt));
    const char* attrName = "pixelAspectRatio";
    const char* attrType = "float";
    patched.insert(patched.end(), attrName, attrName + std::strlen(attrName) + 1);
    patched.insert(patched.end(), attrType, attrType + std::strlen(attrType) + 1);
    const uint32_t attrSize = 4;
    const float one = 1.0f;
    const auto* sizeBytes = reinterpret_cast<const u8*>(&attrSize);
    const auto* oneBytes = reinterpret_cast<const u8*>(&one);
    patched.insert(patched.end(), sizeBytes, sizeBytes + sizeof(attrSize));
    patched.insert(patched.end(), oneBytes, oneBytes + sizeof(one));
    patched.insert(patched.end(), bytes.begin() + static_cast<std::ptrdiff_t>(insertAt), bytes.end());

    const usize added = patched.size() - bytes.size();
    const usize table = insertAt + added + 1;
    if (table + static_cast<usize>(chunks) * sizeof(uint64_t) > patched.size()) {
        return original;
    }
    for (int chunk = 0; chunk < chunks; ++chunk) {
        uint64_t offset = 0;
        const usize at = table + static_cast<usize>(chunk) * sizeof(uint64_t);
        std::memcpy(&offset, patched.data() + at, sizeof(offset));
        offset += added;
        std::memcpy(patched.data() + at, &offset, sizeof(offset));
    }
    return patched;
}

std::optional<DecodedImage> DecodeExr(std::span<const u8> bytes, std::string* err) {
    const std::vector<u8> patched = WithDefaultPixelAspect(bytes);
    float* rgba = nullptr;
    int width = 0;
    int height = 0;
    const char* message = nullptr;
    const int result =
        LoadEXRFromMemory(&rgba, &width, &height, patched.data(), patched.size(), &message);
    if (result < 0 || rgba == nullptr || width <= 0 || height <= 0) {
        SetError(err, message != nullptr ? message : "Failed to decode EXR");
        FreeEXRErrorMessage(message);
        return std::nullopt;
    }
    FreeEXRErrorMessage(message);
    DecodedImage image = FromFloatRgba(rgba, width, height, 4);
    free(rgba);
    return image;
}

std::optional<DecodedImage> DecodeStb(std::span<const u8> bytes, bool asFloat, std::string* err) {
    int width = 0;
    int height = 0;
    int channels = 0;
    if (asFloat) {
        float* pixels = stbi_loadf_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height,
                                               &channels, 4);
        if (pixels == nullptr || width <= 0 || height <= 0) {
            const char* reason = stbi_failure_reason();
            SetError(err, reason != nullptr ? reason : "Failed to decode image");
            return std::nullopt;
        }
        DecodedImage image = FromFloatRgba(pixels, width, height, static_cast<u32>(std::max(channels, 1)));
        stbi_image_free(pixels);
        return image;
    }

    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height,
                                            &channels, 4);
    if (pixels == nullptr || width <= 0 || height <= 0) {
        const char* reason = stbi_failure_reason();
        SetError(err, reason != nullptr ? reason : "Failed to decode image");
        return std::nullopt;
    }
    DecodedImage image;
    image.width = static_cast<u32>(width);
    image.height = static_cast<u32>(height);
    image.channels = static_cast<u32>(std::clamp(channels, 1, 4));
    image.isFloat = false;
    const usize count = static_cast<usize>(width) * static_cast<usize>(height) * 4u;
    image.pixels8.assign(pixels, pixels + count);
    stbi_image_free(pixels);
    return image;
}

}  // namespace

std::optional<DecodedImage> DecodeImageMemory(std::span<const u8> bytes, std::string_view ext, std::string* err) {
    if (err != nullptr) {
        *err = {};
    }
    if (bytes.empty()) {
        SetError(err, "Image file is empty");
        return std::nullopt;
    }
    const std::string kind = NormalizeExt(ext);
    if (kind == "exr") {
        return DecodeExr(bytes, err);
    }
    if (kind == "hdr") {
        return DecodeStb(bytes, true, err);
    }
    if (kind == "png" || kind == "jpg" || kind == "jpeg" || kind == "tga" || kind == "bmp" || kind == "psd") {
        return DecodeStb(bytes, false, err);
    }
    SetError(err, "Unsupported image '" + std::string(ext) + "'");
    return std::nullopt;
}

std::optional<DecodedImage> DecodeImageFile(const std::filesystem::path& file, std::string* err) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        SetError(err, "Cannot read '" + file.filename().string() + "'");
        return std::nullopt;
    }
    const auto bytes = std::vector<u8>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return DecodeImageMemory(bytes, file.extension().string(), err);
}

}  // namespace Luma::Import
