#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Asset/AssetPath.h"
#include "Luma/Asset/AssetRegistry.h"
#include "Luma/Core/Types.h"
#include "Luma/Plugins/ModuleManager.h"
#include "Luma/Plugins/Plugin.h"
#include "Luma/Plugins/PluginDiscovery.h"
#include "Luma/Serialization/SerialValue.h"

namespace Luma {
class Project;
class Scene;
}

namespace Luma::Plugins {

enum class PluginState : u8 {
    Discovered,
    Invalid,
    Disabled,
    Unavailable,
    Resolved,
    Started,
    Failed,
    Stopped,
    NotInstalled,
};

enum class PluginOrigin : u8 { BuiltIn, Project };

const char* ToString(PluginState state);
const char* ToString(PluginOrigin origin);

struct PluginStatus {
    PluginManifest manifest;
    PluginDescriptor descriptor;
    PluginOrigin origin = PluginOrigin::BuiltIn;
    PluginState state = PluginState::Discovered;
    std::vector<std::string> diagnostics;
    bool compiledIn = false;
    bool enabledInProject = false;
    bool restartRequired = false;
};

class PluginManager {
public:
    explicit PluginManager(CompiledPluginTable& compiled);

    void SetRegistries(ComponentRegistry* components,
                       SceneSystemRegistry* sceneSystems = nullptr);

    // Asset and editor registries. `services.hasEditor` must be true in
    // LumaEditor; leave it false in LumaEngine so Editor() is null.
    void SetHostServices(PluginHostServices services);

    // Reads manifests. Does not construct plugins.
    void Discover(std::span<const std::filesystem::path> roots);

    // nullptr: every discovered plugin is treated as enabled (tests).
    // A project without a "plugins" section enables manifests with
    // enabledByDefault. Schema 1: enabled unless listed in `disabled`;
    // listed in `enabled` turns a plugin on; unknown ids are kept as
    // NotInstalled.
    void ApplyProject(const Project* project);

    // Restrict the enabled set after ApplyProject. Ids not in the list are
    // Disabled even if discovered.
    void RestrictEnabled(std::span<const std::string> ids);

    // Resolve, then start every Resolved plugin that this host may run.
    // Returns false when any plugin Failed during this call. Independent
    // plugins still start after a failure.
    bool StartAll(PluginHostKind host);

    void ShutdownAll();

    std::vector<PluginDescriptor> LoadableDescriptors() const;
    void ApplyModuleStatuses(const ModuleManager& modules);

    std::vector<PluginStatus> Statuses() const;
    const PluginStatus* Find(std::string_view id) const;

    const std::vector<std::string>& LastStartOrder() const { return m_startOrder; }
    const std::vector<std::string>& LastShutdownOrder() const { return m_shutdownOrder; }

    const std::vector<ManifestError>& DiscoveryErrors() const { return m_discovery.errors; }
    const std::vector<std::filesystem::path>& SearchRoots() const { return m_searchRoots; }

    // Enable/disable requests from Project Settings. `alsoEnable` is required
    // dependencies that are currently off (never optional, never unrelated).
    // `blockedBy` is why the change cannot be applied. `dependents` and
    // `affectedComponents` are filled for disable. `restart` is true when the
    // running set would not match the project after Commit.
    struct ChangePlan {
        std::string targetId;
        bool enable = true;
        std::vector<std::string> alsoEnable;
        std::vector<std::string> blockedBy;
        std::vector<std::string> dependents;
        std::vector<std::string> affectedComponents;
        bool restart = true;
        bool beta = false;
        bool experimental = false;
    };

    ChangePlan PlanEnable(std::string_view id) const;
    ChangePlan PlanDisable(std::string_view id, const Scene* openScene) const;
    bool Commit(Project& project, const ChangePlan& plan, std::string* error);

    bool RestartRequired() const;

    // Mounts `/<Name>/` for active plugins with CanContainContent and loads
    // `Config/Default<Name>.json` as the default settings layer. Safe with
    // null `paths` / `registry`. Does not run plugin code.
    void MountContentAndConfig(AssetPathResolver* paths = nullptr,
                               AssetRegistry* registry = nullptr);
    void UnmountContentAndConfig();
    const SerialValue* PluginDefaultConfig(std::string_view pluginName) const;

private:
    struct Slot {
        PluginStatus status;
        PluginDescriptor descriptor;
        std::vector<std::string> discoveryDiagnostics;
        std::unique_ptr<IPlugin> instance;
        std::unique_ptr<PluginContext> context;
    };

    void Resolve();
    bool MayRunOn(PluginKind kind, PluginHostKind host) const;
    void MarkUnavailable(Slot& slot, std::string message);
    void RollbackFailed(Slot& slot, std::string message);
    std::vector<std::string> DependentsOf(std::string_view id) const;
    std::set<std::string> TransitiveDependents(std::string_view id) const;
    Slot* SlotById(std::string_view id);
    const Slot* SlotById(std::string_view id) const;
    bool IsEnabled(std::string_view id) const;
    void SyncEnabledFlags();
    void DropNotInstalledSlots();
    void AddNotInstalled(const std::string& id, bool enabledInProject);
    void CollectRequiredEnable(const std::string& id, ChangePlan& plan,
                               std::set<std::string>& visiting) const;
    bool ShouldMountContent(const Slot& slot) const;
    bool IsContentOnly(const Slot& slot) const;
    void LoadDefaultConfig(const Slot& slot);

    CompiledPluginTable& m_compiled;
    AssetPathResolver* m_mountedPaths = nullptr;
    AssetRegistry* m_mountedRegistry = nullptr;
    std::map<std::string, SerialValue> m_pluginDefaults;
    PluginHostServices m_hostServices;
    ComponentRegistry* m_components = nullptr;
    SceneSystemRegistry* m_sceneSystems = nullptr;
    DiscoveryResult m_discovery;
    std::vector<std::filesystem::path> m_searchRoots;
    std::map<std::string, Slot> m_slots;
    std::set<std::string> m_enabled;
    bool m_enableAllDiscovered = true;
    PluginHostKind m_host = PluginHostKind::Runtime;
    std::vector<std::string> m_resolveOrder;
    std::vector<std::string> m_startOrder;
    std::vector<std::string> m_shutdownOrder;
};

}  // namespace Luma::Plugins
