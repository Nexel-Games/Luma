#include "Luma/Asset/Import/TranslatorRegistry.h"

#include "Luma/Asset/Import/Translators/AudioTranslator.h"
#include "Luma/Asset/Import/Translators/ImageTranslator.h"

namespace Luma::Import {

TranslatorRegistry TranslatorRegistry::CreateDefault() {
    TranslatorRegistry registry;
    registry.m_translators.push_back(std::make_unique<ModelTranslator>());
    registry.m_translators.push_back(std::make_unique<ImageTranslator>());
    registry.m_translators.push_back(std::make_unique<AudioTranslator>());
    return registry;
}

ITranslator* TranslatorRegistry::Find(const std::filesystem::path& file) const {
    for (const std::unique_ptr<ITranslator>& translator : m_translators) {
        if (translator->CanTranslate(file)) {
            return translator.get();
        }
    }
    return nullptr;
}

std::vector<std::string> TranslatorRegistry::SupportedExtensions() const {
    static constexpr const char* kExtensions[] = {".gltf", ".glb", ".fbx", ".obj", ".png", ".jpg", ".jpeg",
                                                   ".tga",  ".bmp", ".psd", ".hdr", ".exr", ".wav", ".mp3"};
    std::vector<std::string> extensions;
    for (const char* extension : kExtensions) {
        // CanTranslate looks at the extension, so the file does not need to exist.
        if (Find(std::string("asset") + extension) != nullptr) {
            extensions.emplace_back(extension);
        }
    }
    return extensions;
}

}  // namespace Luma::Import
