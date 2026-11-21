#include "Luma/AssetImport/Translators/AudioTranslator.h"

#include "Luma/Audio/AudioDecode.h"
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

std::string_view AudioTranslator::Name() const {
    return "Luma.Import.Audio";
}

bool AudioTranslator::CanTranslate(const std::filesystem::path& file) const {
    const std::string ext = ExtensionOf(file);
    return ext == ".wav" || ext == ".mp3";
}

bool AudioTranslator::Translate(const std::filesystem::path& file, ImportScene& out, std::string* error) {
    if (error != nullptr) {
        *error = {};
    }
    if (!CanTranslate(file)) {
        const std::string message = "Unsupported sound '" + file.filename().string() + "'";
        if (error != nullptr) {
            *error = message;
        }
        LUMA_USER_ERROR("Import", "{}", message);
        return false;
    }

    std::string decodeError;
    const auto info = Luma::Audio::ProbeSound(file, &decodeError);
    if (!info) {
        const std::string message =
            decodeError.empty() ? "Failed to decode '" + file.filename().string() + "'" : decodeError;
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

    ImportNode sound;
    sound.kind = NodeKind::Sound;
    sound.name = file.stem().string();
    sound.sourceFile = file;
    sound.parent = fileId;
    sound.soundChannels = info->channels;
    sound.soundSampleRate = info->sampleRate;
    sound.soundSeconds = info->seconds;
    out.AddNode(std::move(sound));

    LUMA_LOG_INFO("Import", "Translated sound {} ({} ch, {} Hz, {:.3f}s)", file.filename().string(),
                  info->channels, info->sampleRate, info->seconds);
    return true;
}

}  // namespace Luma::Import
