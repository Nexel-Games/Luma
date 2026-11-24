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

fs::path RelativeUnder(const fs::path& root, const fs::path& file) {
    if (root.empty()) return {};
    const fs::path r = Normalize(root);
    const fs::path f = Normalize(file);
    auto rIt = r.begin();
    auto fIt = f.begin();
    for (; rIt != r.end(); ++rIt, ++fIt) {
        if (fIt == f.end()) return {};
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

bool ContainsName(const std::vector<std::string>& names, std::string_view name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

struct ParsedPath {
    enum class Kind { None, Game, Engine, Plugin };
    Kind kind = Kind::None;
    std::string plugin;
    std::string_view rest;
};

ParsedPath ParseVirtual(std::string_view virtualPath) {
    ParsedPath out;
    if (!virtualPath.starts_with('/')) return out;
    const std::string_view body = virtualPath.substr(1);
    const auto slash = body.find('/');
    const std::string_view head = slash == std::string_view::npos ? body : body.substr(0, slash);
    out.rest = slash == std::string_view::npos ? std::string_view{} : body.substr(slash + 1);
    if (head == "Game") {
        out.kind = ParsedPath::Kind::Game;
    } else if (head == "Engine") {
        out.kind = ParsedPath::Kind::Engine;
    } else if (!head.empty()) {
        out.kind = ParsedPath::Kind::Plugin;
        out.plugin = std::string(head);
    }
    return out;
}

std::optional<fs::path> FindByStem(const fs::path& stemPath) {
    std::error_code ec;
    if (stemPath.has_extension() && fs::is_regular_file(stemPath, ec)) return stemPath;
    const fs::path dir = stemPath.parent_path();
    const std::wstring stem = stemPath.filename().wstring();
    if (!fs::is_directory(dir, ec)) return std::nullopt;
    std::optional<fs::path> found;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().stem().wstring() != stem) continue;
        if (entry.path().extension() == ".meta") continue;
        if (found) return std::nullopt;
        found = entry.path();
    }
    return found;
}

}  // namespace

void AssetPathResolver::SetGameRoot(const fs::path& root) { m_game = root; }
void AssetPathResolver::SetEngineRoot(const fs::path& root) { m_engine = root; }

bool AssetPathResolver::MountPlugin(std::string_view name, const fs::path& contentRoot) {
    if (name.empty() || name == "Game" || name == "Engine") return false;
    AddKnownPlugin(name);
    UnmountPlugin(name);
    m_plugins.push_back(PluginMount{std::string(name), contentRoot});
    return true;
}

void AssetPathResolver::UnmountPlugin(std::string_view name) {
    std::erase_if(m_plugins, [&](const PluginMount& m) { return m.name == name; });
}

void AssetPathResolver::UnmountAllPlugins() { m_plugins.clear(); }

void AssetPathResolver::AddKnownPlugin(std::string_view name) {
    if (name.empty() || ContainsName(m_knownPlugins, name)) return;
    m_knownPlugins.emplace_back(name);
}

void AssetPathResolver::ClearKnownPlugins() { m_knownPlugins.clear(); }

bool AssetPathResolver::IsPluginMounted(std::string_view name) const {
    for (const PluginMount& m : m_plugins) {
        if (m.name == name) return true;
    }
    return false;
}

bool AssetPathResolver::IsKnownPlugin(std::string_view name) const {
    return ContainsName(m_knownPlugins, name);
}

std::optional<std::string> AssetPathResolver::PluginNameOf(std::string_view virtualPath) const {
    const ParsedPath parsed = ParseVirtual(virtualPath);
    if (parsed.kind != ParsedPath::Kind::Plugin) return std::nullopt;
    return parsed.plugin;
}

AssetPathResolver::Status AssetPathResolver::Classify(std::string_view virtualPath) const {
    const ParsedPath parsed = ParseVirtual(virtualPath);
    if (parsed.kind == ParsedPath::Kind::None) return Status::UnknownRoot;
    if (parsed.kind == ParsedPath::Kind::Game) {
        if (m_game.empty() || parsed.rest.empty()) return Status::UnknownRoot;
        return ToFile(virtualPath) ? Status::Ok : Status::NotFound;
    }
    if (parsed.kind == ParsedPath::Kind::Engine) {
        if (m_engine.empty() || parsed.rest.empty()) return Status::UnknownRoot;
        return ToFile(virtualPath) ? Status::Ok : Status::NotFound;
    }
    if (!IsPluginMounted(parsed.plugin)) {
        if (IsKnownPlugin(parsed.plugin)) return Status::MissingPlugin;
        return Status::UnknownRoot;
    }
    if (parsed.rest.empty()) return Status::NotFound;
    auto file = ToFile(virtualPath);
    if (file) return Status::Ok;
    const fs::path* root = nullptr;
    for (const PluginMount& m : m_plugins) {
        if (m.name == parsed.plugin) root = &m.root;
    }
    if (!root) return Status::MissingPlugin;
    const fs::path stemPath = *root / fs::path(std::string(parsed.rest));
    std::error_code ec;
    const fs::path dir = stemPath.parent_path();
    if (!fs::is_directory(dir, ec)) return Status::NotFound;
    std::optional<fs::path> found;
    int matches = 0;
    const std::wstring stem = stemPath.filename().wstring();
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().stem().wstring() != stem) continue;
        if (entry.path().extension() == ".meta") continue;
        ++matches;
        found = entry.path();
    }
    if (matches > 1) return Status::Ambiguous;
    return found ? Status::Ok : Status::NotFound;
}

std::optional<fs::path> AssetPathResolver::ToFile(std::string_view virtualPath) const {
    const ParsedPath parsed = ParseVirtual(virtualPath);
    const fs::path* root = nullptr;
    if (parsed.kind == ParsedPath::Kind::Game) {
        root = &m_game;
    } else if (parsed.kind == ParsedPath::Kind::Engine) {
        root = &m_engine;
    } else if (parsed.kind == ParsedPath::Kind::Plugin) {
        if (!IsPluginMounted(parsed.plugin)) return std::nullopt;
        for (const PluginMount& m : m_plugins) {
            if (m.name == parsed.plugin) {
                root = &m.root;
                break;
            }
        }
    }
    if (!root || root->empty() || parsed.rest.empty()) return std::nullopt;
    return FindByStem(*root / fs::path(std::string(parsed.rest)));
}

std::optional<fs::path> AssetPathResolver::ToFile(
    std::string_view virtualPath, std::initializer_list<std::string_view> extensions) const {
    return ToFileAny(virtualPath, extensions);
}

std::optional<fs::path> AssetPathResolver::MakeFilePath(std::string_view virtualPath,
                                                        std::string_view extension) const {
    const ParsedPath parsed = ParseVirtual(virtualPath);
    const fs::path* root = nullptr;
    if (parsed.kind == ParsedPath::Kind::Game) {
        root = &m_game;
    } else if (parsed.kind == ParsedPath::Kind::Engine) {
        root = &m_engine;
    } else if (parsed.kind == ParsedPath::Kind::Plugin) {
        if (!IsPluginMounted(parsed.plugin)) return std::nullopt;
        for (const PluginMount& m : m_plugins) {
            if (m.name == parsed.plugin) {
                root = &m.root;
                break;
            }
        }
    }
    if (!root || root->empty() || parsed.rest.empty()) return std::nullopt;
    fs::path file = *root / fs::path(std::string(parsed.rest));
    file += ".";
    file += std::string(extension);
    return file;
}

std::optional<std::string> AssetPathResolver::ToVirtual(const fs::path& file) const {
    if (fs::path rel = RelativeUnder(m_game, file); !rel.empty()) {
        return ToVirtualString(kGamePrefix, rel);
    }
    if (fs::path rel = RelativeUnder(m_engine, file); !rel.empty()) {
        return ToVirtualString(kEnginePrefix, rel);
    }
    for (const PluginMount& m : m_plugins) {
        if (fs::path rel = RelativeUnder(m.root, file); !rel.empty()) {
            return ToVirtualString("/" + m.name + "/", rel);
        }
    }
    return std::nullopt;
}

}  // namespace Luma
