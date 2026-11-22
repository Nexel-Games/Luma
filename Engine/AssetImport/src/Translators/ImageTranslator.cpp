#include "Luma/AssetImport/Translators/ImageTranslator.h"

#include "Luma/Asset/ImageDecode.h"
#include "Luma/Asset/TextureColorSpace.h"
#include "Luma/Core/Log.h"

#include <cctype>

namespace Luma::Import {
namespace {

std::string ExtensionOf(const std::filesystem::path& file) {
    std::string ext = file.extension().string();
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext;
}

}  // namespace

std::string_view ImageTranslator::Name() const {
    return "Luma.Import.Image";
}

std::vector<std::string> ImageTranslator::Extensions() const {
    return {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".psd", ".hdr", ".exr"};
}

bool ImageTranslator::Translate(const std::filesystem::path& file, ImportScene& out, std::string* error) {
    if (error != nullptr) {
        *error = {};
    }
    if (!CanTranslate(file)) {
        const std::string message = "Unsupported image '" + file.filename().string() + "'";
        if (error != nullptr) {
            *error = message;
        }
        LUMA_USER_ERROR("Import", "{}", message);
        return false;
    }

    std::string decodeError;
    const auto image = DecodeImageFile(file, &decodeError);
    if (!image) {
        const std::string message = decodeError.empty()
                                        ? "Failed to decode '" + file.filename().string() + "'"
                                        : decodeError;
        if (error != nullptr) {
            *error = message;
        }
        LUMA_USER_ERROR("Import", "{}", message);
        return false;
    }

    ImportNode fileNode;
    fileNode.kind = NodeKind::File;
    fileNode.name = file.filename().string();
    fileNode.sourceFile = file;
    const NodeId fileId = out.AddNode(std::move(fileNode));

    ImportNode texture;
    texture.kind = NodeKind::Texture;
    texture.name = file.stem().string();
    texture.sourceFile = file;
    texture.parent = fileId;
    texture.textureFile = file;
    // HDR and EXR are linear. Other files follow the texture naming rules.
    texture.textureIsLinear = image->isFloat || !IsTextureSRGB(file, nullptr);
    out.AddNode(std::move(texture));

    LUMA_LOG_INFO("Import", "Translated image {} ({}x{}, {})", file.filename().string(), image->width,
                  image->height, image->isFloat ? "float" : "8-bit");
    return true;
}

}  // namespace Luma::Import
