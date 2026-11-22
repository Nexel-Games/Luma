#pragma once

#include "Luma/AssetImport/Translators/ModelTranslator.h"

namespace Luma::Import {

class ImageTranslator final : public ITranslator {
public:
    std::string_view Name() const override;
    std::vector<std::string> Extensions() const override;
    // One File node and one Texture node. Fails when the file does not decode.
    bool Translate(const std::filesystem::path& file, ImportScene& out, std::string* error) override;
};

}  // namespace Luma::Import
