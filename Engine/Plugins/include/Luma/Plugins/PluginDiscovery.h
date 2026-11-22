#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/Plugins/PluginManifest.h"

// Finds Plugins/<Id>/<Id>.lplugin under each search root. Roots are the
// Plugins directories themselves (engine Plugins/ and project Plugins/), not
// the engine or project root. A missing root is skipped: a project is not
// required to have a Plugins folder. Discovery reads manifests only.

namespace Luma::Plugins {

// Factories for plugins compiled into this binary. DiscoverPlugins consults
// the table to set `compiledIn` and never calls Instantiate.
class CompiledPluginTable {
public:
    // `factory` runs only when Instantiate is called. Empty factories and
    // duplicate or illegal ids fail.
    bool Register(std::string id, std::function<void()> factory,
                  std::string* error = nullptr);

    bool IsCompiledIn(std::string_view id) const;

    // Runs the factory registered for `id`. Returns false when none is
    // registered. The call count increments only after the factory returns.
    bool Instantiate(std::string_view id, std::string* error = nullptr);

    i32 InstantiateCount() const { return m_instantiateCount; }

private:
    std::map<std::string, std::function<void()>, std::less<>> m_factories;
    i32 m_instantiateCount = 0;
};

struct DiscoveredPlugin {
    PluginManifest manifest;
    bool compiledIn = false;
    bool engineCompatible = false;
    bool platformCompatible = false;
    std::vector<std::string> diagnostics;
};

struct DiscoveryResult {
    // Successfully parsed, unique manifests, ordered by id. Incompatible
    // engine versions and platforms stay in this list with diagnostics set.
    std::vector<DiscoveredPlugin> plugins;
    // Broken manifests, unreadable files, folder/id mismatches, duplicate ids.
    std::vector<ManifestError> errors;
};

// `compiled` may be null. It is not mutated and its factories are not called.
DiscoveryResult DiscoverPlugins(std::span<const std::filesystem::path> roots,
                                const CompiledPluginTable* compiled = nullptr);

}  // namespace Luma::Plugins
