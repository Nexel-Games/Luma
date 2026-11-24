#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/Plugins/Plugin.h"
#include "Luma/Plugins/PluginDescriptor.h"
#include "Luma/Plugins/PluginManifest.h"

// Finds Plugins/<Id>/<Id>.lplugin under each search root. Roots are the
// Plugins directories themselves (engine Plugins/ and project Plugins/), not
// the engine or project root. A missing root is skipped: a project is not
// required to have a Plugins folder. Discovery reads manifests only.

namespace Luma::Plugins {

// Factories for plugins compiled into this binary. DiscoverPlugins consults
// the table to set `compiledIn` and never calls Instantiate.
class CompiledPluginTable : public IPluginLoader {
public:
    using Factory = std::function<std::unique_ptr<IPlugin>()>;

    // `factory` runs only when Instantiate/Load is called. Empty factories and
    // duplicate or illegal ids fail.
    bool Register(std::string id, Factory factory, std::string* error = nullptr);

    bool IsCompiledIn(std::string_view id) const;
    bool IsAvailable(std::string_view id) const override { return IsCompiledIn(id); }

    // Runs the factory registered for `id`. Returns nullptr when none is
    // registered. The call count increments only after the factory returns.
    std::unique_ptr<IPlugin> Instantiate(std::string_view id, std::string* error = nullptr);
    std::unique_ptr<IPlugin> Load(std::string_view id, std::string* error) override {
        return Instantiate(id, error);
    }

    i32 InstantiateCount() const { return m_instantiateCount; }

    // Process-wide table that `LUMA_REGISTER_PLUGIN` writes into. Hosts pass
    // this to PluginManager so statically linked plugins are compiled-in.
    static CompiledPluginTable& Global();

private:
    std::map<std::string, Factory, std::less<>> m_factories;
    i32 m_instantiateCount = 0;
};

// Registers `Type` under the manifest id. The id is a string literal
// ("Luma.Sample.Runtime"). Static libraries must be whole-archived into the
// host or this object file is dropped.
#define LUMA_REGISTER_PLUGIN(IdLiteral, Type)                                 \
    namespace {                                                               \
    [[maybe_unused]] const bool luma_plugin_reg_##Type =                      \
        ::Luma::Plugins::CompiledPluginTable::Global().Register(              \
            IdLiteral, [] { return std::make_unique<Type>(); });              \
    }

struct DiscoveredPlugin {
    PluginManifest manifest;
    PluginDescriptor descriptor;
    std::filesystem::path searchRoot;
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
