#pragma once

#include "Luma/Asset/Import/Translators/ModelTranslator.h"

namespace Luma::Import {

class AudioTranslator final : public ITranslator {
public:
    std::string_view Name() const override;
    bool CanTranslate(const std::filesystem::path& file) const override;
    // One File node and one Sound node. Fails when the file does not decode.
    bool Translate(const std::filesystem::path& file, ImportScene& out, std::string* error) override;
};

}  // namespace Luma::Import
