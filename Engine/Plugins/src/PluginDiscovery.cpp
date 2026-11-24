#include "Luma/Plugins/PluginDiscovery.h"
#include "Luma/Plugins/Module.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>

#include "Luma/Core/Log.h"
#include "Luma/Core/Version.h"

namespace Luma::Plugins {
namespace fs = std::filesystem;

namespace {

std::string Join(const std::vector<std::string>& parts) {
    std::string out;
    for (usize i = 0; i < parts.size(); ++i) {
        if (i) out += ", ";
        out += parts[i];
    }
    return out;
}

void AddError(DiscoveryResult& result, const fs::path& file, std::string field,
              std::string message) {
    LUMA_USER_ERROR("Plugins", "{}", message);
    result.errors.push_back(ManifestError{file, std::move(field), std::move(message)});
}

std::optional<std::string> ReadFile(const fs::path& path, std::string* error) {
    std::error_code ec;
    if (fs::is_directory(path, ec) || ec) {
        *error = ec ? "could not stat manifest (" + ec.message() + ")"
                    : "manifest path is a directory, not a file";
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        *error = "could not open manifest for reading";
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    if (in.bad()) {
        *error = "failed while reading manifest";
        return std::nullopt;
    }
    return ss.str();
}

void ScanFolder(const fs::path& folder, const fs::path& searchRoot,
                const CompiledPluginTable* compiled,
                std::vector<DiscoveredPlugin>& found, DiscoveryResult& result) {
    const std::string folderName = folder.filename().string();
    const fs::path manifestPath = folder / (folderName + ".lplugin");
    std::error_code ec;
    if (!fs::exists(manifestPath, ec) || ec) {
        std::string foundOther;
        if (fs::is_directory(folder, ec) && !ec) {
            fs::directory_iterator nested(folder, ec);
            if (!ec) {
                for (const fs::directory_entry& entry : nested) {
                    if (entry.path().extension() == ".lplugin") {
                        foundOther = entry.path().filename().string();
                        break;
                    }
                }
            }
        }
        std::string message = "plugin folder \"" + folderName + "\" has no " + folderName +
                              ".lplugin";
        if (!foundOther.empty()) {
            message += " (found " + foundOther +
                       " instead; the folder name, file name, and id must match)";
        }
        AddError(result, manifestPath, "file", message);
        return;
    }

    std::string readError;
    auto text = ReadFile(manifestPath, &readError);
    if (!text) {
        AddError(result, manifestPath, "file",
                 "could not read \"" + manifestPath.string() + "\": " + readError);
        return;
    }

    const usize errorsBefore = result.errors.size();
    auto descriptor = ParseDescriptor(*text, manifestPath, result.errors);
    for (usize i = errorsBefore; i < result.errors.size(); ++i) {
        const ManifestError& err = result.errors[i];
        std::string message = manifestPath.string();
        message += ": ";
        if (!err.field.empty()) {
            message += err.field;
            message += ": ";
        }
        message += err.message;
        LUMA_USER_ERROR("Plugins", "{}", message);
    }
    if (!descriptor) return;

    if (descriptor->name != folderName) {
        AddError(result, manifestPath, "Name",
                 "folder \"" + folderName + "\" declares name \"" + descriptor->name +
                     "\". The folder name, file name, and plugin name must match.");
        return;
    }

    DiscoveredPlugin plugin;
    plugin.descriptor = std::move(*descriptor);
    plugin.searchRoot = searchRoot;
    plugin.manifest = ToManifest(plugin.descriptor);
    plugin.compiledIn = (compiled && compiled->IsCompiledIn(plugin.manifest.id));
    if (!plugin.compiledIn) {
        for (const ModuleDescriptor& module : plugin.descriptor.modules) {
            if (StaticModuleBackend::Global().IsAvailable(module.name) ||
                DllModuleBackend::Global().IsAvailable(module.name)) {
                plugin.compiledIn = true;
                break;
            }
        }
    }

    std::string rangeError;
    plugin.engineCompatible =
        SatisfiesRange(EngineVersion(), plugin.manifest.engineVersionRange, &rangeError);
    if (!plugin.engineCompatible) {
        std::string message = plugin.manifest.id + " " + plugin.manifest.version.ToString() +
                              " requires engine " + plugin.manifest.engineVersionRange +
                              ", but this engine is " + EngineVersion().ToString();
        if (!rangeError.empty()) message += " (" + rangeError + ")";
        message += ". Update the engine or change engineVersionRange.";
        plugin.diagnostics.push_back(message);
        LUMA_USER_WARN("Plugins", "{}", message);
    }

    plugin.platformCompatible = plugin.manifest.platforms.empty();
    for (const std::string& platform : plugin.manifest.platforms) {
        if (platform == HostPlatform()) plugin.platformCompatible = true;
    }
    if (!plugin.platformCompatible) {
        std::string message = plugin.manifest.id + " does not support " +
                              std::string(HostPlatform()) + " (manifest allows " +
                              Join(plugin.manifest.platforms) +
                              "). Add this platform to the manifest or do not ship the plugin here.";
        plugin.diagnostics.push_back(message);
        LUMA_USER_WARN("Plugins", "{}", message);
    }

    found.push_back(std::move(plugin));
}

}  // namespace

CompiledPluginTable& CompiledPluginTable::Global() {
    static CompiledPluginTable table;
    return table;
}

bool CompiledPluginTable::Register(std::string id, Factory factory, std::string* error) {
    auto fail = [&](std::string message) {
        if (error) *error = std::move(message);
        return false;
    };
    if (!IsValidPluginId(id)) return fail("plugin id \"" + id + "\" is not valid");
    if (!factory) return fail("plugin \"" + id + "\" registered an empty factory");
    if (m_factories.find(id) != m_factories.end()) {
        return fail("plugin \"" + id + "\" already has a compiled factory");
    }
    m_factories.emplace(std::move(id), std::move(factory));
    return true;
}

bool CompiledPluginTable::IsCompiledIn(std::string_view id) const {
    return m_factories.find(id) != m_factories.end();
}

std::unique_ptr<IPlugin> CompiledPluginTable::Instantiate(std::string_view id,
                                                         std::string* error) {
    auto it = m_factories.find(id);
    if (it == m_factories.end()) {
        if (error) *error = "no compiled plugin factory for \"" + std::string(id) + "\"";
        return nullptr;
    }
    std::unique_ptr<IPlugin> plugin = it->second();
    ++m_instantiateCount;
    return plugin;
}

DiscoveryResult DiscoverPlugins(std::span<const fs::path> roots,
                                const CompiledPluginTable* compiled) {
    DiscoveryResult result;
    std::vector<DiscoveredPlugin> found;
    for (const fs::path& root : roots) {
        std::error_code ec;
        if (!fs::exists(root, ec)) continue;
        if (ec || !fs::is_directory(root, ec) || ec) {
            AddError(result, root, "root",
                     "plugin root \"" + root.string() + "\" is not a directory" +
                         (ec ? " (" + ec.message() + ")" : ""));
            continue;
        }
        fs::directory_iterator it(root, ec);
        if (ec) {
            AddError(result, root, "root",
                     "could not read plugin root \"" + root.string() + "\": " + ec.message());
            continue;
        }
        for (const fs::directory_entry& entry : it) {
            std::error_code entryEc;
            if (entry.is_directory(entryEc) && !entryEc) {
                ScanFolder(entry.path(), root, compiled, found, result);
            } else if (entry.path().extension() == ".lplugin") {
                AddError(result, entry.path(), "file",
                         "manifest \"" + entry.path().filename().string() +
                             "\" must live in Plugins/<Id>/<Id>.lplugin, not directly in the root");
            }
        }
    }

    std::map<std::string, std::vector<fs::path>> byId;
    for (const DiscoveredPlugin& plugin : found) {
        byId[plugin.manifest.id].push_back(plugin.manifest.manifestPath);
    }
    std::vector<std::string> duplicates;
    for (const auto& [id, paths] : byId) {
        if (paths.size() < 2) continue;
        duplicates.push_back(id);
        std::string message = "plugin id \"" + id + "\" is declared by \"" + paths[0].string() + "\"";
        for (usize i = 1; i < paths.size(); ++i) {
            message += " and \"" + paths[i].string() + "\"";
        }
        message += ". Each plugin id must be unique. Rename one of the plugins.";
        AddError(result, paths[0], "id", std::move(message));
    }

    for (DiscoveredPlugin& plugin : found) {
        if (std::find(duplicates.begin(), duplicates.end(), plugin.manifest.id) != duplicates.end()) {
            continue;
        }
        result.plugins.push_back(std::move(plugin));
    }
    std::sort(result.plugins.begin(), result.plugins.end(),
              [](const DiscoveredPlugin& a, const DiscoveredPlugin& b) {
                  return a.manifest.id < b.manifest.id;
              });

    LUMA_LOG_INFO("Plugins", "discovered {} plugin(s), {} error(s)", result.plugins.size(),
                  result.errors.size());
    return result;
}

}  // namespace Luma::Plugins
