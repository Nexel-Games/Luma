#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Luma/Asset/Import/Translators/ModelTranslator.h"

namespace Luma::Import {

// Owns the model, image, and audio translators. Find returns a non-owning
// pointer that stays valid for the registry's lifetime.
class TranslatorRegistry {
public:
    static TranslatorRegistry CreateDefault();

    ITranslator* Find(const std::filesystem::path& file) const;
    // Lowercase extensions including the dot, for a file-picker filter.
    std::vector<std::string> SupportedExtensions() const;

private:
    std::vector<std::unique_ptr<ITranslator>> m_translators;
};

}  // namespace Luma::Import
