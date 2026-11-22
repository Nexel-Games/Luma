#include <algorithm>
#include "Luma/Core/Log.h"
#include "Luma/Asset/AssetTypeRegistry.h"
#include "Luma/AssetImport/TranslatorRegistry.h"

#include "Luma/AssetImport/Translators/AudioTranslator.h"
#include "Luma/AssetImport/Translators/ImageTranslator.h"

namespace Luma::Import {

bool ITranslator::CanTranslate(const std::filesystem::path& file) const {
    const auto ext = NormalizeAssetExtension(file.extension().string());
    for (const auto& claimed : Extensions())
        if (!ext.empty() && NormalizeAssetExtension(claimed) == ext) return true;
    return false;
}

RegistrationToken TranslatorRegistry::Register(std::unique_ptr<ITranslator> translator, std::string owner,
                                              std::string* error) {
    auto reject = [&](std::string reason) {
        if (error) *error = reason;
        LUMA_LOG_ERROR("Import", "{}", reason);
        return RegistrationToken{};
    };
    if (!translator || owner.empty() || translator->Name().empty())
        return reject("A translator requires a name and owner");
    std::vector<std::string> extensions;
    for (const auto& value : translator->Extensions()) {
        auto ext = NormalizeAssetExtension(value);
        if (ext.empty()) return reject("Invalid translator extension: " + value);
        ext.insert(ext.begin(), '.');
        if (std::find(extensions.begin(), extensions.end(), ext) != extensions.end())
            return reject("Duplicate translator extension: " + ext);
        extensions.push_back(std::move(ext));
    }
    if (extensions.empty()) return reject("A translator requires at least one extension");
    std::lock_guard lock(m_entries->mutex);
    for (const auto& entry : m_entries->items) {
        if (entry.translator->Name() == translator->Name()) return reject("Duplicate translator name");
        for (const auto& ext : extensions)
            if (std::find(entry.extensions.begin(), entry.extensions.end(), ext) != entry.extensions.end())
                return reject("Duplicate translator extension: " + ext);
    }
    auto* key = translator.get();
    m_entries->items.push_back({std::move(translator), std::move(owner), std::move(extensions)});
    return RegistrationToken(m_entries, [entries = m_entries.get(), key] {
        std::lock_guard guard(entries->mutex);
        std::erase_if(entries->items, [key](const Entry& entry) { return entry.translator.get() == key; });
    });
}

TranslatorRegistry TranslatorRegistry::CreateDefault() {
    TranslatorRegistry registry;
    registry.m_builtins.Add(registry.Register(std::make_unique<ModelTranslator>(), "Engine"));
    registry.m_builtins.Add(registry.Register(std::make_unique<ImageTranslator>(), "Engine"));
    registry.m_builtins.Add(registry.Register(std::make_unique<AudioTranslator>(), "Engine"));
    return registry;
}

TranslatorRegistry& Translators() {
    static auto registry = TranslatorRegistry::CreateDefault();
    return registry;
}

std::shared_ptr<ITranslator> TranslatorRegistry::Acquire(const std::filesystem::path& file) const {
    const std::string ext = "." + NormalizeAssetExtension(file.extension().string());
    std::lock_guard lock(m_entries->mutex);
    for (const auto& entry : m_entries->items)
        if (std::find(entry.extensions.begin(), entry.extensions.end(), ext) != entry.extensions.end())
            return entry.translator;
    return nullptr;
}

ITranslator* TranslatorRegistry::Find(const std::filesystem::path& file) const {
    return Acquire(file).get();
}

std::vector<std::string> TranslatorRegistry::SupportedExtensions() const {
    std::vector<std::string> result;
    std::lock_guard lock(m_entries->mutex);
    for (const auto& entry : m_entries->items)
        result.insert(result.end(), entry.extensions.begin(), entry.extensions.end());
    return result;
}

}  // namespace Luma::Import
