#pragma once

#include <filesystem>
#include "Luma/Core/Registration.h"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Luma/AssetImport/Translators/ModelTranslator.h"

namespace Luma::Import {

// Main-thread registry. A translator pointer is valid while its registration is live.
class TranslatorRegistry {
public:
    TranslatorRegistry() = default;
    TranslatorRegistry(const TranslatorRegistry&) = delete;
    TranslatorRegistry(TranslatorRegistry&&) noexcept = default;
    static TranslatorRegistry CreateDefault();
    RegistrationToken Register(std::unique_ptr<ITranslator> translator, std::string owner,
                               std::string* error = nullptr);
    std::shared_ptr<ITranslator> Acquire(const std::filesystem::path& file) const;
    ITranslator* Find(const std::filesystem::path& file) const;
    std::vector<std::string> SupportedExtensions() const;

private:
    struct Entry {
        std::shared_ptr<ITranslator> translator;
        std::string owner;
        std::vector<std::string> extensions;
    };
    struct Entries {
        std::mutex mutex;
        std::vector<Entry> items;
    };
    std::shared_ptr<Entries> m_entries = std::make_shared<Entries>();
    RegistrationSet m_builtins;
};

TranslatorRegistry& Translators();

}  // namespace Luma::Import
