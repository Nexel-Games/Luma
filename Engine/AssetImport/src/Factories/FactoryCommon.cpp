#include "Luma/AssetImport/Factories/FactoryCommon.h"

#include "Luma/Asset/AssetId.h"
#include "Luma/AssetImport/Factories/MaterialFactory.h"
#include "Luma/AssetImport/Factories/MeshFactory.h"
#include "Luma/AssetImport/Factories/SoundFactory.h"
#include "Luma/AssetImport/Factories/TextureFactory.h"
#include "Luma/AssetImport/ImportMetadata.h"
#include "Luma/AssetImport/SourceWatch.h"
#include "Luma/Serialization/Json.h"

#include <fstream>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace Luma::Import {
namespace {

FactoryResult Failed(std::string error) {
    FactoryResult result;
    result.error = std::move(error);
    return result;
}

const ImportNode* NodeAt(const ImportScene& scene, NodeId id) {
    if (id >= scene.nodes.size()) {
        return nullptr;
    }
    return &scene.nodes[id];
}

std::string TextureExtension(const ImportNode& node) {
    std::string ext = node.textureEmbedded.empty() ? node.textureFile.extension().string() : node.textureEmbeddedExt;
    if (!ext.empty() && ext.front() != '.') {
        ext.insert(ext.begin(), '.');
    }
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    if (ext.empty() || ext == ".psd") {
        return ".png";
    }
    return ext;
}

}  // namespace

bool ImportCancelled(const FactoryContext& context) {
    return context.cancelled && context.cancelled();
}

std::filesystem::path ImportTempPath(const std::filesystem::path& dest) {
    return std::filesystem::path(dest.string() + ".tmp");
}

bool ReplaceFileAtomic(const std::filesystem::path& from, const std::filesystem::path& to, std::string* err) {
    std::error_code error;
    std::filesystem::create_directories(to.parent_path(), error);
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0) {
        return true;
    }
    std::filesystem::remove(from, error);
    if (err != nullptr) {
        *err = "Could not replace '" + to.string() + "'";
    }
    return false;
}

bool WriteFileAtomic(const std::filesystem::path& dest, const void* data, std::size_t size, std::string* err) {
    const std::filesystem::path temp = ImportTempPath(dest);
    std::error_code error;
    std::filesystem::create_directories(dest.parent_path(), error);
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (err != nullptr) {
                *err = "Could not write '" + temp.string() + "'";
            }
            return false;
        }
        if (size > 0) {
            out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        }
        if (!out) {
            std::filesystem::remove(temp, error);
            if (err != nullptr) {
                *err = "Could not write '" + temp.string() + "'";
            }
            return false;
        }
    }
    return ReplaceFileAtomic(temp, dest, err);
}

std::string VirtualPathFor(const FactoryContext& context, const std::filesystem::path& file) {
    if (const auto path = context.paths.ToVirtual(file)) {
        return *path;
    }
    return {};
}

std::filesystem::path PlannedAssetFile(const FactoryContext& context, const AssetRequest& request,
                                       const ImportNode& node) {
    const bool subfolders = context.settings.Common().GetBool("subfolders");
    std::filesystem::path dest = request.destFile.parent_path();
    if (subfolders) {
        dest = dest.parent_path();
    }
    const SettingsBlock& common = context.settings.Common();
    switch (node.kind) {
        case NodeKind::Mesh:
            return PlannedFile(dest, subfolders, SubfolderFor(common, AssetType::Mesh),
                               PrefixedName(common.GetString("prefixMesh"), node.name), ".lmesh");
        case NodeKind::Material:
            return PlannedFile(dest, subfolders, SubfolderFor(common, AssetType::Material),
                               PrefixedName(common.GetString("prefixMaterial"), node.name), ".lmat");
        case NodeKind::Texture:
            return PlannedFile(dest, subfolders, SubfolderFor(common, AssetType::Texture),
                               PrefixedName(common.GetString("prefixTexture"), node.name), TextureExtension(node));
        case NodeKind::Sound: {
            std::string ext = node.sourceFile.extension().string();
            for (char& c : ext) {
                if (c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            if (ext.empty()) {
                ext = ".wav";
            }
            return PlannedFile(dest, subfolders, SubfolderFor(common, AssetType::Sound), PrefixedName(common.GetString("prefixSound"), node.name),
                               ext);
        }
        case NodeKind::File:
            break;
    }
    return request.destFile;
}

FactoryResult FinishAsset(const FactoryContext& context, const AssetRequest& request,
                          const std::filesystem::path& file, const ImportNode* node, std::string importer,
                          SerialValue settings) {
    if (ImportCancelled(context)) {
        std::error_code error;
        std::filesystem::remove(file, error);
        std::filesystem::remove(ImportTempPath(file), error);
        return Failed("Import was cancelled");
    }

    ImportMetadata meta;
    meta.guid = MakeAssetIdFromKey(std::filesystem::absolute(file).string());
    if (const auto existing = ReadMetadata(file)) {
        if (existing->guid.IsValid()) {
            meta.guid = existing->guid;
        }
    }
    meta.type = request.type;
    meta.importer = std::move(importer);
    meta.importerVersion = 1;
    meta.settings = std::move(settings);
    if (node != nullptr) {
        meta.sourceFile = node->sourceFile.string();
        meta.sourceHash = HashFileCached(node->sourceFile);
        meta.sourceNodeName = node->name;
        std::error_code error;
        const auto stamp = std::filesystem::last_write_time(node->sourceFile, error);
        if (!error) {
            meta.sourceMtime = static_cast<i64>(stamp.time_since_epoch().count());
        }
        const auto size = std::filesystem::file_size(node->sourceFile, error);
        if (!error) {
            meta.sourceSize = static_cast<i64>(size);
        }
    }

    SerialValue root = SerialValue::MakeObject();
    root["guid"] = ToString(meta.guid);
    root["type"] = AssetTypeName(meta.type);
    root["sourceFile"] = meta.sourceFile;
    root["sourceHash"] = meta.sourceHash;
    root["sourceMtime"] = meta.sourceMtime;
    root["sourceSize"] = meta.sourceSize;
    root["importer"] = meta.importer;
    root["importerVersion"] = static_cast<i64>(meta.importerVersion);
    root["sourceNodeName"] = meta.sourceNodeName;
    root["settings"] = meta.settings;

    const std::string json = WriteJson(root, true);
    std::string error;
    if (!WriteFileAtomic(MetadataPathFor(file), json.data(), json.size(), &error)) {
        return Failed(error.empty() ? "Could not write metadata" : error);
    }

    FactoryResult result;
    result.ok = true;
    result.file = file;
    return result;
}

FactoryResult CreateAsset(const AssetRequest& request, const FactoryContext& context) {
    if (ImportCancelled(context)) {
        return Failed("Import was cancelled");
    }
    if (request.policy == ConflictPolicy::Skip && request.exists) {
        FactoryResult result;
        result.ok = true;
        result.skipped = true;
        result.file = request.destFile;
        return result;
    }
    switch (request.type) {
        case AssetType::Mesh:
            return CreateMeshAsset(request, context);
        case AssetType::Material:
            return CreateMaterialAsset(request, context);
        case AssetType::Texture:
            return CreateTextureAsset(request, context);
        case AssetType::Sound:
            return CreateSoundAsset(request, context);
        default:
            return Failed("This asset type cannot be imported");
    }
}

}  // namespace Luma::Import
