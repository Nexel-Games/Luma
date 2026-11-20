#include "Luma/Asset/AssetPath.h"

#include <algorithm>
#include <system_error>

namespace Luma {
namespace {

namespace fs = std::filesystem;

fs::path Normalize(const fs::path& p) {
    std::error_code ec;
    fs::path abs = fs::weakly_canonical(fs::absolute(p, ec), ec);
    if (ec) abs = fs::absolute(p).lexically_normal();
    return abs.make_preferred();
}

// Relative path of `file` under `root`, or empty when it is not inside.
fs::path RelativeUnder(const fs::path& root, const fs::path& file) {
    if (root.empty()) return {};
    const fs::path r = Normalize(root);
    const fs::path f = Normalize(file);
    auto rIt = r.begin();
    auto fIt = f.begin();
    for (; rIt != r.end(); ++rIt, ++fIt) {
        if (fIt == f.end()) return {};
        // Windows paths compare case-insensitively.
        std::wstring a = rIt->wstring(), b = fIt->wstring();
        std::transform(a.begin(), a.end(), a.begin(), ::towlower);
        std::transform(b.begin(), b.end(), b.begin(), ::towlower);
        if (a != b) return {};
    }
    fs::path rel;
    for (; fIt != f.end(); ++fIt) rel /= *fIt;
    return rel;
}

std::string ToVirtualString(std::string_view prefix, fs::path rel) {
    rel.replace_extension();
    return std::string(prefix) + rel.generic_string();
}

}  // namespace

void AssetPathResolver::SetGameRoot(const fs::path& root) { m_game = root; }
void AssetPathResolver::SetEngineRoot(const fs::path& root) { m_engine = root; }

std::optional<fs::path> AssetPathResolver::ToFile(std::string_view virtualPath) const {
    const fs::path* root = nullptr;
    std::string_view rest;
    if (virtualPath.starts_with(kGamePrefix)) {
        root = &m_game;
        rest = virtualPath.substr(kGamePrefix.size());
    } else if (virtualPath.starts_with(kEnginePrefix)) {
        root = &m_engine;
        rest = virtualPath.substr(kEnginePrefix.size());
    }
    if (!root || root->empty() || rest.empty()) return std::nullopt;

    const fs::path stemPath = *root / fs::path(std::string(rest));
    std::error_code ec;
    // Exact file (virtual path given with an extension).
    if (stemPath.has_extension() && fs::is_regular_file(stemPath, ec)) return stemPath;
    const fs::path dir = stemPath.parent_path();
    const std::wstring stem = stemPath.filename().wstring();
    if (!fs::is_directory(dir, ec)) return std::nullopt;
    std::optional<fs::path> found;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().stem().wstring() != stem) continue;
        // Import sidecars (.meta) share the stem; they are not the asset.
        if (entry.path().extension() == ".meta") continue;
        if (found) return std::nullopt;  // ambiguous
        found = entry.path();
    }
    return found;
}

std::optional<std::string> AssetPathResolver::ToVirtual(const fs::path& file) const {
    if (fs::path rel = RelativeUnder(m_game, file); !rel.empty()) {
        return ToVirtualString(kGamePrefix, rel);
    }
    if (fs::path rel = RelativeUnder(m_engine, file); !rel.empty()) {
        return ToVirtualString(kEnginePrefix, rel);
    }
    return std::nullopt;
}

}  // namespace Luma
