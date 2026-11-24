#pragma once

#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/Plugins/Module.h"
#include "Luma/Plugins/PluginDescriptor.h"

namespace Luma::Plugins {

enum class ModuleRunState : u8 { Pending, Skipped, Loaded, Failed };

struct ModuleStatus {
    std::string name;
    std::string pluginName;
    LoadingPhase phase = LoadingPhase::Default;
    ModuleRunState state = ModuleRunState::Pending;
    std::string error;
};

class ModuleManager {
public:
    explicit ModuleManager(IModuleBackend& backend = StaticModuleBackend::Global());

    void SetHostServices(PluginHostServices services);
    void SetTarget(PluginTarget target, bool shipping = false);

    // Active plugins in dependency order (ties already broken by name).
    void SetActivePlugins(std::span<const PluginDescriptor> plugins);

    void LoadPhase(LoadingPhase phase);
    void LoadAllPhases();

    IModuleInterface* LoadModule(std::string_view name, std::string* error = nullptr);
    bool IsLoaded(std::string_view name) const;
    IModuleInterface* FindModule(std::string_view name) const;

    template <typename T>
    T& GetChecked(std::string_view name);

    void ShutdownAll();
    std::vector<ModuleStatus> Statuses() const;
    const std::vector<std::string>& LoadOrder() const { return m_loadOrder; }
    const std::vector<std::string>& ShutdownOrder() const { return m_shutdownOrder; }

private:
    struct Loaded {
        ModuleStatus status;
        std::unique_ptr<IModuleInterface> instance;
        std::unique_ptr<ModuleContext> context;
    };

    Loaded* Slot(std::string_view name);
    const Loaded* Slot(std::string_view name) const;
    const PluginDescriptor* PluginOf(std::string_view pluginName) const;
    bool PluginFailed(std::string_view pluginName) const;
    std::vector<std::string> DependentPlugins(std::string_view pluginName) const;
    void RollbackPlugin(std::string_view pluginName, std::string message);
    bool StartOne(const PluginDescriptor& plugin, const ModuleDescriptor& module,
                  std::string* error);

    IModuleBackend& m_backend;
    PluginHostServices m_services;
    PluginTarget m_target = PluginTarget::Editor;
    bool m_shipping = false;
    std::vector<PluginDescriptor> m_plugins;
    std::map<std::string, Loaded> m_loaded;
    std::vector<std::string> m_loadOrder;
    std::vector<std::string> m_shutdownOrder;
    std::set<std::string> m_failedPlugins;
};

template <typename T>
T& ModuleManager::GetChecked(std::string_view name) {
    IModuleInterface* module = FindModule(name);
    T* typed = dynamic_cast<T*>(module);
    if (!typed) {
        LUMA_USER_ERROR("Plugins", "GetChecked(\"{}\") is not a loaded module of that type. "
                                    "Load the providing plugin first.",
                        std::string(name));
        std::abort();
    }
    return *typed;
}

}  // namespace Luma::Plugins
