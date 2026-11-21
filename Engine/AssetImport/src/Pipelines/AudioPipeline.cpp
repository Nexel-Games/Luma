#include "Luma/AssetImport/Pipelines/AudioPipeline.h"

namespace Luma::Import {

void DefineAudioPipeline(SettingsBlock& block) {
    auto add = [&](SettingField field, SerialValue value) {
        field.category = "Audio";
        block.Define(std::move(field), std::move(value));
    };

    SettingField loop;
    loop.key = "loop";
    loop.label = "Loop";
    loop.type = FieldType::Bool;
    add(std::move(loop), false);

    SettingField volume;
    volume.key = "volume";
    volume.label = "Volume";
    volume.type = FieldType::Float;
    volume.min = 0.0;
    volume.max = 4.0;
    add(std::move(volume), 1.0);

    SettingField pitch;
    pitch.key = "pitch";
    pitch.label = "Pitch";
    pitch.type = FieldType::Float;
    pitch.min = 0.1;
    pitch.max = 4.0;
    add(std::move(pitch), 1.0);
}

void AppendSoundRequests(const ImportScene& scene, const SettingsBlock& common,
                         const std::filesystem::path& dest, std::vector<AssetRequest>& out) {
    const bool subfolders = common.GetBool("subfolders");
    const std::string prefix = common.GetString("prefixSound");
    for (NodeId id = 0; id < scene.nodes.size(); ++id) {
        const ImportNode& node = scene.nodes[id];
        if (node.kind != NodeKind::Sound || !node.enabled) {
            continue;
        }
        std::string ext = node.sourceFile.extension().string();
        for (char& c : ext) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        if (ext.empty()) {
            ext = ".wav";
        }
        AssetRequest request;
        request.type = AssetType::Sound;
        request.sources = {id};
        request.destFile = PlannedFile(dest, subfolders, "Sounds", PrefixedName(prefix, node.name), ext);
        out.push_back(std::move(request));
    }
}

}  // namespace Luma::Import
