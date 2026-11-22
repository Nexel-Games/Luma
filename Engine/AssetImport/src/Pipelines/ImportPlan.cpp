#include "Luma/Asset/AssetTypeRegistry.h"
#include "Luma/AssetImport/Pipelines/ImportPlan.h"

#include "Luma/AssetImport/Pipelines/AudioPipeline.h"
#include "Luma/AssetImport/Pipelines/CommonPipeline.h"
#include "Luma/AssetImport/Pipelines/MaterialPipeline.h"
#include "Luma/AssetImport/Pipelines/MeshPipeline.h"
#include "Luma/AssetImport/Pipelines/TexturePipeline.h"

#include <cctype>
#include <unordered_set>

namespace Luma::Import {

bool EnabledMeshUses(const ImportScene& scene, NodeId material) {
    for (const ImportNode& node : scene.nodes) {
        if (node.kind != NodeKind::Mesh || !node.enabled) {
            continue;
        }
        for (NodeId id : node.meshMaterials) {
            if (id == material) {
                return true;
            }
        }
    }
    return false;
}

bool MaterialUsesTexture(const MaterialDesc& material, NodeId texture) {
    return material.baseColorTex.texture == texture || material.normalTex.texture == texture ||
           material.metallicRoughnessTex.texture == texture || material.occlusionTex.texture == texture ||
           material.emissiveTex.texture == texture || material.opacityTex.texture == texture;
}

bool TextureIsStandalone(const ImportScene& scene, const ImportNode& texture) {
    for (const ImportNode& node : scene.nodes) {
        if (node.kind == NodeKind::Mesh && node.parent == texture.parent) {
            return false;
        }
    }
    return true;
}

namespace {

ConflictPolicy PolicyFrom(std::string_view label) {
    if (label == "Overwrite") {
        return ConflictPolicy::Overwrite;
    }
    if (label == "Skip") {
        return ConflictPolicy::Skip;
    }
    return ConflictPolicy::KeepBoth;
}

std::filesystem::path WithSuffix(const std::filesystem::path& file, int index) {
    const std::string name = file.stem().string() + "_" + std::to_string(index) + file.extension().string();
    return file.parent_path() / name;
}

bool PathExists(const std::filesystem::path& file) {
    std::error_code error;
    return std::filesystem::exists(file, error);
}

void ApplyConflicts(ImportPlan& plan, const SettingsBlock& common, const SettingsBlock& materials) {
    const ConflictPolicy policy = PolicyFrom(common.GetString("conflict"));
    const bool reuseExisting = materials.GetString("materialMode") == "Use Existing";
    std::unordered_set<std::string> taken;
    for (AssetRequest& request : plan.requests) {
        const bool lookup = reuseExisting && request.type == AssetType::Material && PathExists(request.destFile);
        if (lookup) {
            request.exists = true;
            request.policy = ConflictPolicy::Skip;
            taken.insert(request.destFile.generic_string());
            continue;
        }
        request.policy = policy;
        if (policy == ConflictPolicy::KeepBoth) {
            std::filesystem::path file = request.destFile;
            int index = 1;
            while (PathExists(file) || taken.contains(file.generic_string())) {
                file = WithSuffix(request.destFile, index++);
            }
            request.destFile = file;
            request.exists = false;
        } else {
            request.exists = PathExists(request.destFile);
        }
        taken.insert(request.destFile.generic_string());
    }
}

}  // namespace

std::string SanitizeAssetName(std::string_view name) {
    std::string out;
    bool pending = false;
    for (unsigned char c : name) {
        if (std::isalnum(c)) {
            if (pending && !out.empty()) {
                out.push_back('_');
            }
            pending = false;
            out.push_back(static_cast<char>(c));
        } else {
            pending = true;
        }
    }
    if (out.empty()) {
        return "Asset";
    }
    return out;
}

std::string PrefixedName(std::string_view prefix, std::string_view stem) {
    const std::string clean = SanitizeAssetName(stem);
    if (!prefix.empty() && clean.starts_with(prefix)) {
        return clean;
    }
    return std::string(prefix) + clean;
}

std::string SubfolderName(std::string_view requested) {
    // One folder level: path pieces are joined with '_', and "."/".." pieces
    // (which would leave the destination) are dropped.
    auto trimmed = [](std::string s) {
        const auto first = s.find_first_not_of(" .");
        if (first == std::string::npos) return std::string{};
        const auto last = s.find_last_not_of(" .");
        return s.substr(first, last - first + 1);
    };
    std::string out;
    std::string piece;
    auto flush = [&] {
        std::string part = trimmed(piece);
        piece.clear();
        if (part.empty()) return;
        if (!out.empty()) out.push_back('_');
        out += part;
    };
    for (const char c : requested) {
        if (c == '/' || c == '\\') {
            flush();
            continue;
        }
        const bool forbidden = c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
                               c == '|' || static_cast<unsigned char>(c) < 32;
        piece.push_back(forbidden ? '_' : c);
    }
    flush();
    return out;
}

std::string SubfolderFor(const SettingsBlock& common, AssetType type) {
    const auto desc = AssetTypes().FindBuiltin(type);
    const std::string fallback = desc && !desc->importFolder.empty() ? desc->importFolder : "Meshes";
    const std::string key = "folder" + fallback;
    std::string name = SubfolderName(common.GetString(key));
    return name.empty() ? std::string(fallback) : name;
}

std::filesystem::path PlannedFile(const std::filesystem::path& destFolder, bool subfolders,
                                  std::string_view subfolder, std::string_view name, std::string_view extension) {
    std::filesystem::path dir = destFolder;
    if (subfolders) {
        dir /= std::string(subfolder);
    }
    std::string ext(extension);
    if (!ext.empty() && ext.front() != '.') {
        ext.insert(ext.begin(), '.');
    }
    return dir / (std::string(name) + ext);
}

PipelineStack::PipelineStack() {
    DefineCommonPipeline(m_common);
    DefineMeshPipeline(m_meshes);
    DefineMaterialPipeline(m_materials);
    DefineTexturePipeline(m_textures);
    DefineAudioPipeline(m_audio);
}

SettingsBlock& PipelineStack::Common() { return m_common; }
SettingsBlock& PipelineStack::Meshes() { return m_meshes; }
SettingsBlock& PipelineStack::Materials() { return m_materials; }
SettingsBlock& PipelineStack::Textures() { return m_textures; }
SettingsBlock& PipelineStack::Audio() { return m_audio; }
const SettingsBlock& PipelineStack::Common() const { return m_common; }
const SettingsBlock& PipelineStack::Meshes() const { return m_meshes; }
const SettingsBlock& PipelineStack::Materials() const { return m_materials; }
const SettingsBlock& PipelineStack::Textures() const { return m_textures; }
const SettingsBlock& PipelineStack::Audio() const { return m_audio; }

std::vector<SettingsBlock*> PipelineStack::Blocks() {
    return {&m_common, &m_meshes, &m_materials, &m_textures, &m_audio};
}

ImportPlan PipelineStack::BuildPlan(const ImportScene& scene, const std::filesystem::path& destFolder) const {
    ImportPlan plan;
    AppendMeshRequests(scene, m_common, m_meshes, destFolder, plan.requests);
    AppendMaterialRequests(scene, m_common, m_materials, destFolder, plan.requests, plan.warnings);
    AppendTextureRequests(scene, m_common, m_materials, m_textures, destFolder, plan.requests, plan.warnings);
    AppendSoundRequests(scene, m_common, destFolder, plan.requests);
    ApplyConflicts(plan, m_common, m_materials);
    return plan;
}

SerialValue PipelineStack::ToJson() const {
    SerialValue json = SerialValue::MakeObject();
    json["Common"] = m_common.ToJson();
    json["Meshes"] = m_meshes.ToJson();
    json["Materials"] = m_materials.ToJson();
    json["Textures"] = m_textures.ToJson();
    json["Audio"] = m_audio.ToJson();
    return json;
}

void PipelineStack::FromJson(const SerialValue& json) {
    if (const SerialValue* common = json.Find("Common")) {
        m_common.FromJson(*common);
    }
    if (const SerialValue* meshes = json.Find("Meshes")) {
        m_meshes.FromJson(*meshes);
    }
    if (const SerialValue* materials = json.Find("Materials")) {
        m_materials.FromJson(*materials);
    }
    if (const SerialValue* textures = json.Find("Textures")) {
        m_textures.FromJson(*textures);
    }
    if (const SerialValue* audio = json.Find("Audio")) {
        m_audio.FromJson(*audio);
    }
}

}  // namespace Luma::Import
