#include "Luma/Asset/TextureColorSpace.h"

#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace Luma {
namespace {

// Short forms from the naming convention plus the full words texture
// libraries spell out (T_Rock_Roughness, Metallic, Displacement).
constexpr std::array<std::string_view, 18> kDataTokens = {
    "normal", "nor", "nrm", "rough", "roughness", "metal", "metallic", "metalness",
    "ao", "occlusion", "orm", "arm", "mask", "opacity", "height", "disp",
    "displacement", "normals"};

bool IsDelimiter(char c) { return c == '_' || c == '-' || c == '.' || c == ' '; }

bool IsDataToken(const std::string& token) {
    for (std::string_view t : kDataTokens) {
        if (token == t) return true;
    }
    return false;
}

}  // namespace

bool IsTextureSRGB(const std::filesystem::path& file, const LumaTextureMetadata* meta) {
    if (meta) return meta->isSRGB;

    // The extension is not part of the name.
    const std::string name = file.stem().string();
    std::string token;
    for (usize i = 0; i <= name.size(); ++i) {
        if (i == name.size() || IsDelimiter(name[i])) {
            if (IsDataToken(token)) return false;
            token.clear();
        } else {
            token.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(name[i]))));
        }
    }
    return true;
}

}  // namespace Luma
