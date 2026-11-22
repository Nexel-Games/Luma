#include "Luma/AssetImport/ImportMetadata.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#include "Luma/Serialization/Json.h"

namespace Luma::Import {
namespace {

constexpr u64 kFnvOffset = 14695981039346656037ull;
constexpr u64 kFnvPrime = 1099511628211ull;

std::string ToHex(u64 value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}

AssetType TypeFromName(std::string_view name) {
    for (u8 i = 0; i < static_cast<u8>(AssetType::Count); ++i) {
        const auto type = static_cast<AssetType>(i);
        if (AssetTypeName(type) == name) {
            return type;
        }
    }
    return AssetType::Unknown;
}

}  // namespace

std::filesystem::path MetadataPathFor(const std::filesystem::path& assetFile) {
    return std::filesystem::path(assetFile.string() + ".meta");
}

bool RenameAssetFile(const std::filesystem::path& from, const std::filesystem::path& to,
                     std::string* errorOrWarning) {
    auto report = [&](std::string text) {
        if (errorOrWarning != nullptr) *errorOrWarning = std::move(text);
    };
    report({});
    std::error_code io;
    if (std::filesystem::exists(to, io)) {
        report("'" + to.filename().string() + "' already exists");
        return false;
    }
    std::filesystem::rename(from, to, io);
    if (io) {
        report(io.message());
        return false;
    }
    const std::filesystem::path oldMeta = MetadataPathFor(from);
    if (std::filesystem::exists(oldMeta, io)) {
        std::filesystem::rename(oldMeta, MetadataPathFor(to), io);
        if (io) report("its import data (.meta) could not be renamed: " + io.message());
    }
    return true;
}

bool MoveAssetToFolder(const std::filesystem::path& from, const std::filesystem::path& folder,
                       std::filesystem::path* movedTo, std::string* error) {
    auto fail = [&](std::string text) {
        if (error != nullptr) *error = std::move(text);
        return false;
    };
    if (error != nullptr) error->clear();
    if (movedTo != nullptr) movedTo->clear();
    std::error_code io;
    if (from.empty() || !std::filesystem::exists(from, io)) return fail("The asset is no longer on disk");
    if (folder.empty() || !std::filesystem::is_directory(folder, io)) return fail("Drop onto a folder");

    const std::filesystem::path src = std::filesystem::weakly_canonical(from, io);
    io.clear();
    const std::filesystem::path destDir = std::filesystem::weakly_canonical(folder, io);
    if (src.empty() || destDir.empty()) return fail("Could not resolve that folder");
    if (src.parent_path() == destDir) return false;

    if (std::filesystem::is_directory(src, io)) {
        const std::filesystem::path rel = destDir.lexically_relative(src);
        const std::string relStr = rel.generic_string();
        const bool inside = !relStr.empty() && relStr != "." && rel.is_relative() &&
                            relStr.rfind("..", 0) != 0;
        if (relStr == "." || inside) return fail("Can't move a folder into itself");
    }

    const std::filesystem::path dest = destDir / src.filename();
    if (!RenameAssetFile(src, dest, error)) return false;
    if (movedTo != nullptr) *movedTo = std::filesystem::weakly_canonical(dest);
    return true;
}

std::string HashFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return {};
    }
    u64 hash = kFnvOffset;
    std::vector<char> buffer(1 << 20);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = in.gcount();
        for (std::streamsize i = 0; i < got; ++i) {
            hash ^= static_cast<u8>(buffer[static_cast<usize>(i)]);
            hash *= kFnvPrime;
        }
    }
    return ToHex(hash);
}

bool WriteMetadata(const std::filesystem::path& assetFile, const ImportMetadata& meta,
                   std::string* err) {
    SerialValue root = SerialValue::MakeObject();
    root["guid"] = ToString(meta.guid);
    root["type"] = AssetTypeName(meta.type);
    root["sourceFile"] = meta.sourceFile;
    root["sourceHash"] = meta.sourceHash;
    root["sourceMtime"] = meta.sourceMtime;
    root["sourceSize"] = meta.sourceSize;
    root["importer"] = meta.importer;
    root["importerVersion"] = meta.importerVersion;
    root["sourceNodeName"] = meta.sourceNodeName;
    root["settings"] = meta.settings;

    const auto path = MetadataPathFor(assetFile);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (err != nullptr) {
            *err = "failed to write " + path.string();
        }
        return false;
    }
    out << WriteJson(root, true);
    if (!out) {
        if (err != nullptr) {
            *err = "failed to write " + path.string();
        }
        return false;
    }
    if (err != nullptr) {
        err->clear();
    }
    return true;
}

std::optional<ImportMetadata> ReadMetadata(const std::filesystem::path& assetFile) {
    std::ifstream in(MetadataPathFor(assetFile), std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto json = ParseJson(text);
    if (!json || !json->IsObject()) {
        return std::nullopt;
    }
    ImportMetadata meta;
    if (const SerialValue* guid = json->Find("guid")) {
        meta.guid = FromString(guid->AsString());
    }
    if (const SerialValue* type = json->Find("type")) {
        meta.type = TypeFromName(type->AsString());
    }
    if (const SerialValue* source = json->Find("sourceFile")) {
        meta.sourceFile = source->AsString();
    }
    if (const SerialValue* hash = json->Find("sourceHash")) {
        meta.sourceHash = hash->AsString();
    }
    if (const SerialValue* mtime = json->Find("sourceMtime")) {
        meta.sourceMtime = mtime->AsInt();
    }
    if (const SerialValue* size = json->Find("sourceSize")) {
        meta.sourceSize = size->AsInt();
    }
    if (const SerialValue* importer = json->Find("importer")) {
        meta.importer = importer->AsString();
    }
    if (const SerialValue* version = json->Find("importerVersion")) {
        meta.importerVersion = static_cast<u32>(version->AsInt());
    }
    if (const SerialValue* node = json->Find("sourceNodeName")) {
        meta.sourceNodeName = node->AsString();
    }
    if (const SerialValue* settings = json->Find("settings")) {
        meta.settings = *settings;
    }
    return meta;
}

}  // namespace Luma::Import
