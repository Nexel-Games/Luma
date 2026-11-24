#include "Luma/Asset/AssetRegistry.h"
#include "Luma/Asset/AssetScanner.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <system_error>

#include "Luma/Asset/AssetMetadata.h"
#include "Luma/Core/Log.h"

namespace Luma {

namespace {
i64 FileTimeToUnixSeconds(std::filesystem::file_time_type ft) {
    using namespace std::chrono;
    // file_time_type's epoch differs by platform (Windows: 1601-01-01,
    // POSIX: 1970-01-01). We don't need exact wall clock — only a stable
    // count for change detection — so we approximate by subtracting a
    // known anchor. For MSVC where file_clock::to_sys isn't available,
    // we use the time-since-epoch of file_clock's own now() and treat
    // it as monotonic.
    static const auto anchor = std::filesystem::file_time_type::clock::now();
    if (ft < anchor) return 0;
    auto dur = ft - anchor;
    return duration_cast<seconds>(dur).count();
}

std::string StemString(const std::filesystem::path& p) {
    auto stem = p.stem().string();
    return stem;
}
}  // namespace

std::filesystem::path AssetRegistry::Normalize(
    const std::filesystem::path& p) const {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(p, ec);
    if (ec) return p.lexically_normal();
    return canonical;
}

bool AssetRegistry::IsUnderAnyRoot(const std::filesystem::path& p) const {
    auto norm = Normalize(p);
    for (const auto& root : m_roots) {
        auto r = Normalize(root);
        auto rel = norm.lexically_relative(r);
        auto relStr = rel.string();
        if (!relStr.empty() && relStr.front() != '.') return true;
    }
    return false;
}

AssetId AssetRegistry::MakeId(const std::filesystem::path& p) const {
    auto norm = Normalize(p);
    std::string key = m_salt + "|" + norm.string();
    return MakeAssetIdFromKey(key);
}

namespace {
// Index key for a folder: lexically normal, no trailing separator.
std::string FolderKey(const std::filesystem::path& dir) {
    std::filesystem::path p = dir.lexically_normal();
    if (!p.has_filename() && p.has_parent_path() && p != p.root_path()) p = p.parent_path();
    return p.make_preferred().string();
}
}  // namespace

void AssetRegistry::IndexAdd(const AssetData& data) {
    m_children[FolderKey(data.packagePath.parent_path())].push_back(data.id);
}

void AssetRegistry::IndexRemove(const AssetId& id, const std::filesystem::path& packagePath) {
    auto it = m_children.find(FolderKey(packagePath.parent_path()));
    if (it == m_children.end()) return;
    std::erase(it->second, id);
    if (it->second.empty()) m_children.erase(it);
}

const std::vector<AssetId>* AssetRegistry::ChildrenOf(const std::filesystem::path& dir) const {
    if (auto it = m_children.find(FolderKey(dir)); it != m_children.end()) return &it->second;
    // Not in lexical form (relative path, "..", different casing of a
    // symlinked root): resolve through the filesystem once.
    if (auto it = m_children.find(FolderKey(Normalize(dir))); it != m_children.end()) return &it->second;
    return nullptr;
}

const std::filesystem::path& AssetRegistry::PluginBrowserGroupPath() {
    static const std::filesystem::path k("Luma.PluginContent");
    return k;
}

void AssetRegistry::AddRoot(const std::filesystem::path& root) {
    AddRoot(root, ContentRootKind::Project, {});
}

void AssetRegistry::AddRoot(const std::filesystem::path& root, ContentRootKind kind,
                            std::string label) {
    auto norm = Normalize(root);
    if (std::find(m_roots.begin(), m_roots.end(), norm) != m_roots.end()) return;
    m_roots.push_back(norm);
    ContentRoot info;
    info.path = norm;
    info.kind = kind;
    info.label = std::move(label);
    if (info.label.empty()) info.label = norm.filename().string();
    m_rootInfos.push_back(std::move(info));
}

bool AssetRegistry::RemoveRoot(const std::filesystem::path& root) {
    auto norm = Normalize(root);
    auto it = std::find(m_roots.begin(), m_roots.end(), norm);
    if (it == m_roots.end()) return false;
    RemoveUnder(norm);
    std::erase(m_roots, norm);
    std::erase_if(m_rootInfos, [&](const ContentRoot& info) { return info.path == norm; });
    return true;
}

bool AssetRegistry::IsPluginPath(const std::filesystem::path& path) const {
    auto norm = Normalize(path);
    if (norm == PluginBrowserGroupPath()) return true;
    for (const ContentRoot& info : m_rootInfos) {
        if (info.kind != ContentRootKind::Plugin) continue;
        if (norm == info.path) return true;
        auto rel = norm.lexically_relative(info.path);
        auto relStr = rel.string();
        if (!relStr.empty() && relStr.front() != '.') return true;
    }
    return false;
}

void AssetRegistry::Scan() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_byId.clear();
    m_byPath.clear();
    m_children.clear();

    for (const auto& root : m_roots) {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec)) continue;
        std::filesystem::recursive_directory_iterator it(root, ec);
        if (ec) {
            LUMA_LOG_WARN("AssetRegistry", "scan failed for {}: {}",
                          root.string(), ec.message());
            continue;
        }
        std::filesystem::recursive_directory_iterator end;
        std::vector<AssetData> pendingFiles;
        std::vector<AssetData> pendingDirs;

        for (; it != end; it.increment(ec)) {
            if (ec) {
                LUMA_LOG_WARN("AssetRegistry", "walk error: {}", ec.message());
                break;
            }
            const auto& entry = *it;
            auto abs = Normalize(entry.path());
            std::error_code ec2;
            auto status = entry.symlink_status(ec2);
            (void)ec2;
            bool isDir = std::filesystem::is_directory(status);
            // Same rules as AssetScanner: .meta/.tmp sidecars, editor temp
            // files and dot folders (skipped with everything under them).
            if (AssetScanner::ShouldIgnore(abs.lexically_relative(Normalize(root)))) {
                if (isDir) it.disable_recursion_pending();
                continue;
            }
            std::string ext = LowercaseExtension(abs);

            AssetData data;
            data.id = MakeId(abs);
            data.packagePath = abs;
            data.assetName = StemString(abs);
            data.extension = ext;
            if (entry.exists(ec)) {
                data.mtime = isDir ? 0 : FileTimeToUnixSeconds(entry.last_write_time(ec));
                if (!isDir) data.size = entry.file_size(ec);
            }

            if (isDir) {
                data.type = AssetType::Folder;
                pendingDirs.push_back(std::move(data));
            } else {
                data.type = AssetTypeFromExtension(ext);
                pendingFiles.push_back(std::move(data));
            }
        }

        // Insert folders first so the directory rows show up before files
        // for sorting / tree-building purposes in the Content Browser.
        for (auto& d : pendingDirs) {
            auto id = d.id;
            m_byPath[d.packagePath.string()] = id;
            if (m_byId.emplace(id, d).second) IndexAdd(d);
        }
        for (auto& f : pendingFiles) {
            auto id = f.id;
            m_byPath[f.packagePath.string()] = id;
            if (m_byId.emplace(id, f).second) IndexAdd(f);
        }
    }
}

void AssetRegistry::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_byId.clear();
    m_byPath.clear();
    m_children.clear();
}

usize AssetRegistry::Size() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_byId.size();
}

const AssetData* AssetRegistry::Lookup(const AssetId& id) const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_byId.find(id);
    return it == m_byId.end() ? nullptr : &it->second;
}

const AssetData* AssetRegistry::LookupByPath(
    const std::filesystem::path& path) const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto norm = Normalize(path);
    auto it = m_byPath.find(norm.string());
    if (it == m_byPath.end()) return nullptr;
    auto jt = m_byId.find(it->second);
    return jt == m_byId.end() ? nullptr : &jt->second;
}

std::vector<const AssetData*> AssetRegistry::All() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<const AssetData*> out;
    out.reserve(m_byId.size());
    for (const auto& [id, data] : m_byId) out.push_back(&data);
    return out;
}

std::vector<const AssetData*> AssetRegistry::FilterByType(AssetType type) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<const AssetData*> out;
    for (const auto& [id, data] : m_byId) {
        if (data.type == type) out.push_back(&data);
    }
    return out;
}

std::vector<const AssetData*> AssetRegistry::FilterByDirectory(
    const std::filesystem::path& dir) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto normDir = Normalize(dir);
    std::vector<const AssetData*> out;
    for (const auto& [id, data] : m_byId) {
        if (data.IsFolder()) continue;
        auto rel = data.packagePath.lexically_relative(normDir);
        auto relStr = rel.string();
        if (!relStr.empty() && relStr.front() != '.' &&
            relStr.find("..") == std::string::npos) {
            out.push_back(&data);
        }
    }
    return out;
}

std::vector<const AssetData*> AssetRegistry::FilterByName(
    const std::string& nameSubstring) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<const AssetData*> out;
    if (nameSubstring.empty()) {
        out.reserve(m_byId.size());
        for (const auto& [id, data] : m_byId) out.push_back(&data);
        return out;
    }
    std::string needle = nameSubstring;
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    for (const auto& [id, data] : m_byId) {
        std::string hay = data.assetName;
        std::transform(hay.begin(), hay.end(), hay.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (hay.find(needle) != std::string::npos) out.push_back(&data);
    }
    return out;
}

std::vector<const AssetData*> AssetRegistry::Filter(
    std::optional<AssetType> type,
    std::optional<std::filesystem::path> dir,
    const std::string& nameSubstring) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<const AssetData*> out;
    std::string needle;
    if (!nameSubstring.empty()) {
        needle = nameSubstring;
        std::transform(needle.begin(), needle.end(), needle.begin(),
                       [](unsigned char c) { return std::tolower(c); });
    }
    auto accept = [&](const AssetData& data) {
        if (type.has_value() && data.type != *type) return;
        if (!needle.empty()) {
            std::string hay = data.assetName;
            std::transform(hay.begin(), hay.end(), hay.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (hay.find(needle) == std::string::npos) return;
        }
        out.push_back(&data);
    };
    if (dir.has_value()) {
        // Direct children of `dir` only: read them from the path tree.
        if (const std::vector<AssetId>* children = ChildrenOf(*dir)) {
            out.reserve(children->size());
            for (const AssetId& id : *children) {
                if (auto it = m_byId.find(id); it != m_byId.end()) accept(it->second);
            }
        }
        return out;
    }
    for (const auto& [id, data] : m_byId) accept(data);
    return out;
}

void AssetRegistry::RefreshPath(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!IsUnderAnyRoot(path)) return;
    auto abs = Normalize(path);
    for (const auto& root : m_roots) {
        auto rel = abs.lexically_relative(Normalize(root));
        if (!rel.empty() && *rel.begin() != ".." && AssetScanner::ShouldIgnore(rel)) return;
    }
    std::error_code ec;
    if (!std::filesystem::exists(abs, ec)) {
        // Path went away — drop any entry under it.
        auto it = m_byPath.find(abs.string());
        if (it != m_byPath.end()) {
            IndexRemove(it->second, abs);
            m_byId.erase(it->second);
            m_byPath.erase(it);
        }
        return;
    }
    AssetData data;
    data.id = MakeId(abs);
    data.packagePath = abs;
    data.assetName = StemString(abs);
    data.extension = LowercaseExtension(abs);
    auto status = std::filesystem::status(abs, ec);
    bool isDir = std::filesystem::is_directory(status);
    if (isDir) {
        data.type = AssetType::Folder;
    } else {
        data.type = AssetTypeFromExtension(data.extension);
        data.mtime = FileTimeToUnixSeconds(
            std::filesystem::last_write_time(abs, ec));
        data.size = std::filesystem::file_size(abs, ec);
    }
    auto id = data.id;
    m_byPath[abs.string()] = id;
    if (auto existing = m_byId.find(id); existing != m_byId.end()) {
        IndexRemove(id, existing->second.packagePath);
    }
    IndexAdd(data);
    m_byId[id] = std::move(data);
}

void AssetRegistry::RemoveUnder(const std::filesystem::path& dir) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto normDir = Normalize(dir);
    std::vector<std::string> pathsToDrop;
    for (auto& [path, id] : m_byPath) {
        std::filesystem::path p(path);
        auto rel = p.lexically_relative(normDir);
        auto relStr = rel.string();
        if (p == normDir || (!relStr.empty() && relStr.front() != '.' &&
                             relStr.find("..") == std::string::npos)) {
            pathsToDrop.push_back(path);
        }
    }
    for (auto& p : pathsToDrop) {
        auto id = m_byPath[p];
        IndexRemove(id, std::filesystem::path(p));
        m_byId.erase(id);
        m_byPath.erase(p);
    }
}

std::string AssetRegistry::DisplayPathFor(
    const std::filesystem::path& path) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto norm = Normalize(path);
    auto normalizeSeparators = [](std::string s) {
        std::replace(s.begin(), s.end(), '\\', '/');
        return s;
    };
    for (const ContentRoot& info : m_rootInfos) {
        auto r = Normalize(info.path);
        const std::string prefix =
            info.kind == ContentRootKind::Plugin ? ("Plugins/" + info.label + "/") : "Assets/";
        if (norm == r) return prefix;
        auto rel = norm.lexically_relative(r);
        auto relStr = normalizeSeparators(rel.string());
        if (relStr.empty() || relStr.front() == '.' ||
            relStr.find("..") != std::string::npos) {
            continue;
        }
        return prefix + relStr;
    }
    return normalizeSeparators(norm.string());
}

const AssetMetadata* AssetRegistry::GetMetadata(const AssetId& id) const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_metadata.find(id);
    if (it != m_metadata.end()) {
        return &it->second;
    }
    return nullptr;
}

void AssetRegistry::SaveMetadata(const AssetId& id, const AssetMetadata& metadata) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_metadata[id] = metadata;
}

}  // namespace Luma
