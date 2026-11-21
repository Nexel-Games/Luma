#include "Luma/AssetImport/Factories/SoundFactory.h"

#include <fstream>
#include <vector>

#include "Luma/Audio/AudioDecode.h"

namespace Luma::Import {
namespace {

FactoryResult Failed(std::string error) {
    FactoryResult result;
    result.error = std::move(error);
    return result;
}

}  // namespace

FactoryResult CreateSoundAsset(const AssetRequest& request, const FactoryContext& context) {
    if (request.sources.empty() || request.sources.front() >= context.scene.nodes.size()) {
        return Failed("Sound request has no source node");
    }
    const ImportNode& node = context.scene.nodes[request.sources.front()];
    std::ifstream in(node.sourceFile, std::ios::binary);
    if (!in) {
        return Failed("Could not read '" + node.sourceFile.filename().string() + "'");
    }
    const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (ImportCancelled(context)) {
        return Failed("Import was cancelled");
    }
    std::string error;
    if (!WriteFileAtomic(request.destFile, bytes.data(), bytes.size(), &error)) {
        return Failed(error.empty() ? "Could not write the sound" : error);
    }

    SerialValue settings = context.settings.ToJson();
    if (const auto info = Luma::Audio::ProbeSound(node.sourceFile, &error)) {
        settings["channels"] = static_cast<i64>(info->channels);
        settings["sampleRate"] = static_cast<i64>(info->sampleRate);
        settings["seconds"] = info->seconds;
    }
    return FinishAsset(context, request, request.destFile, &node, "Luma.Import.Audio", std::move(settings));
}

}  // namespace Luma::Import
