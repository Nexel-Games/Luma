#include "Luma/Plugins/ModuleManager.h"

#include <algorithm>
#include <exception>

#include "Luma/Core/Log.h"

namespace Luma::Plugins {
namespace {

const LoadingPhase kPhases[] = {
    LoadingPhase::EarliestPossible, LoadingPhase::PostConfigInit, LoadingPhase::PreDefault,
    LoadingPhase::Default,          LoadingPhase::PostDefault,    LoadingPhase::PostEngineInit,
};

bool RequiredRef(const PluginReference& ref) { return ref.enabled && !ref.optional; }

}  // namespace

ModuleManager::ModuleManager(IModuleBackend& backend) : m_backend(backend) {}

void ModuleManager::SetHostServices(PluginHostServices services) { m_services = std::move(services); }

void ModuleManager::SetTarget(PluginTarget target, bool shipping) {
    m_target = target;
    m_shipping = shipping;
}

void ModuleManager::SetActivePlugins(std::span<const PluginDescriptor> plugins) {
    ShutdownAll();
    m_plugins.assign(plugins.begin(), plugins.end());
    m_failedPlugins.clear();
}

ModuleManager::Loaded* ModuleManager::Slot(std::string_view name) {
    auto it = m_loaded.find(std::string(name));
    return it == m_loaded.end() ? nullptr : &it->second;
}

const ModuleManager::Loaded* ModuleManager::Slot(std::string_view name) const {
    auto it = m_loaded.find(std::string(name));
    return it == m_loaded.end() ? nullptr : &it->second;
}

const PluginDescriptor* ModuleManager::PluginOf(std::string_view pluginName) const {
    for (const PluginDescriptor& p : m_plugins) {
        if (p.name == pluginName) return &p;
    }
    return nullptr;
}

bool ModuleManager::PluginFailed(std::string_view pluginName) const {
    return m_failedPlugins.count(std::string(pluginName)) != 0;
}

std::vector<std::string> ModuleManager::DependentPlugins(std::string_view pluginName) const {
    std::vector<std::string> out;
    std::vector<std::string> stack{std::string(pluginName)};
    std::set<std::string> seen;
    while (!stack.empty()) {
        const std::string current = std::move(stack.back());
        stack.pop_back();
        for (const PluginDescriptor& p : m_plugins) {
            if (p.name == current || seen.count(p.name)) continue;
            for (const PluginReference& ref : p.plugins) {
                if (!RequiredRef(ref) || ref.name != current) continue;
                if (seen.insert(p.name).second) {
                    out.push_back(p.name);
                    stack.push_back(p.name);
                }
            }
        }
    }
    return out;
}

void ModuleManager::RollbackPlugin(std::string_view pluginName, std::string message) {
    std::set<std::string> roll{std::string(pluginName)};
    for (const std::string& dep : DependentPlugins(pluginName)) roll.insert(dep);
    LUMA_USER_ERROR("Plugins", "{}", message);

    std::vector<std::string> reverse = m_loadOrder;
    std::reverse(reverse.begin(), reverse.end());
    for (const std::string& name : reverse) {
        Loaded* loaded = Slot(name);
        if (!loaded || roll.count(loaded->status.pluginName) == 0) continue;
        if (loaded->instance && loaded->context) {
            try {
                loaded->instance->ShutdownModule(*loaded->context);
            } catch (const std::exception& ex) {
                LUMA_USER_ERROR("Plugins", "{} threw during ShutdownModule: {}", name, ex.what());
            } catch (...) {
                LUMA_USER_ERROR("Plugins", "{} threw during ShutdownModule.", name);
            }
        }
        if (loaded->context) loaded->context->ReleaseAll();
        loaded->instance.reset();
        loaded->context.reset();
        m_backend.Unload(name);
        loaded->status.state = ModuleRunState::Failed;
        if (loaded->status.error.empty()) loaded->status.error = message;
        m_loadOrder.erase(std::remove(m_loadOrder.begin(), m_loadOrder.end(), name),
                          m_loadOrder.end());
        LUMA_LOG_INFO("Plugins", "rolled back module {}", name);
    }
    for (const std::string& id : roll) m_failedPlugins.insert(id);
}

bool ModuleManager::StartOne(const PluginDescriptor& plugin, const ModuleDescriptor& module,
                             std::string* error) {
    if (IsLoaded(module.name)) return true;
    if (PluginFailed(plugin.name)) {
        if (error) *error = plugin.name + " already failed";
        return false;
    }

    Loaded loaded;
    loaded.status.name = module.name;
    loaded.status.pluginName = plugin.name;
    loaded.status.phase = module.loadingPhase;
    loaded.status.state = ModuleRunState::Pending;

    std::string createError;
    loaded.instance = m_backend.Create(module.name, &createError);
    if (!loaded.instance) {
        const std::string message = module.name + " failed to construct" +
                                    (createError.empty() ? "." : (": " + createError)) +
                                    " Rebuild the plugin for this engine.";
        if (error) *error = message;
        loaded.status.state = ModuleRunState::Failed;
        loaded.status.error = message;
        m_loaded[module.name] = std::move(loaded);
        RollbackPlugin(plugin.name, message);
        return false;
    }

    loaded.context = std::make_unique<ModuleContext>(module.name, &plugin, m_services);
    try {
        loaded.instance->StartupModule(*loaded.context);
    } catch (const std::exception& ex) {
        loaded.context->Fail(ex.what());
    } catch (...) {
        loaded.context->Fail("threw an unknown exception");
    }
    if (loaded.context->Failed()) {
        const std::string message =
            module.name + " failed to start: " + loaded.context->FailMessage() +
            ". Fix the module or disable " + plugin.name + ".";
        if (error) *error = message;
        loaded.context->ReleaseAll();
        loaded.instance.reset();
        loaded.context.reset();
        m_backend.Unload(module.name);
        loaded.status.state = ModuleRunState::Failed;
        loaded.status.error = message;
        m_loaded[module.name] = std::move(loaded);
        RollbackPlugin(plugin.name, message);
        return false;
    }

    loaded.status.state = ModuleRunState::Loaded;
    m_loadOrder.push_back(module.name);
    auto [it, inserted] = m_loaded.emplace(module.name, std::move(loaded));
    (void)inserted;
    LUMA_LOG_INFO("Plugins", "started module {} ({})", module.name, plugin.name);
    return true;
}

void ModuleManager::LoadPhase(LoadingPhase phase) {
    for (const PluginDescriptor& plugin : m_plugins) {
        if (PluginFailed(plugin.name)) continue;
        for (const ModuleDescriptor& module : plugin.modules) {
            if (module.loadingPhase != phase) continue;
            if (!ModuleLoadsOn(module.type, m_target, m_shipping)) {
                Loaded skip;
                skip.status.name = module.name;
                skip.status.pluginName = plugin.name;
                skip.status.phase = phase;
                skip.status.state = ModuleRunState::Skipped;
                m_loaded.emplace(module.name, std::move(skip));
                continue;
            }
            if (!module.platformAllowList.empty()) {
                bool ok = false;
                for (const std::string& p : module.platformAllowList) {
                    if (p == HostPlatform()) ok = true;
                }
                if (!ok) {
                    Loaded skip;
                    skip.status.name = module.name;
                    skip.status.pluginName = plugin.name;
                    skip.status.phase = phase;
                    skip.status.state = ModuleRunState::Skipped;
                    m_loaded.emplace(module.name, std::move(skip));
                    continue;
                }
            }
            if (!m_backend.IsAvailable(module.name)) {
                const std::string message =
                    plugin.name + " module \"" + module.name +
                    "\" is not built for this binary. Rebuild with LUMA_PLUGIN flags "
                    "or disable the plugin.";
                Loaded failed;
                failed.status.name = module.name;
                failed.status.pluginName = plugin.name;
                failed.status.phase = phase;
                failed.status.state = ModuleRunState::Failed;
                failed.status.error = message;
                m_loaded[module.name] = std::move(failed);
                RollbackPlugin(plugin.name, message);
                break;
            }
            if (!StartOne(plugin, module, nullptr)) break;
        }
    }
}

void ModuleManager::LoadAllPhases() {
    for (LoadingPhase phase : kPhases) LoadPhase(phase);
}

IModuleInterface* ModuleManager::LoadModule(std::string_view name, std::string* error) {
    if (Loaded* existing = Slot(name)) {
        if (existing->status.state == ModuleRunState::Loaded) return existing->instance.get();
        if (error) *error = existing->status.error.empty()
                                ? (std::string(name) + " is not loaded")
                                : existing->status.error;
        return nullptr;
    }
    for (const PluginDescriptor& plugin : m_plugins) {
        for (const ModuleDescriptor& module : plugin.modules) {
            if (module.name != name) continue;
            if (!ModuleLoadsOn(module.type, m_target, m_shipping)) {
                if (error) *error = std::string(name) + " does not load on this target.";
                return nullptr;
            }
            if (!StartOne(plugin, module, error)) return nullptr;
            Loaded* loaded = Slot(name);
            return loaded ? loaded->instance.get() : nullptr;
        }
    }
    if (error) *error = "module \"" + std::string(name) + "\" is not in an Active plugin.";
    return nullptr;
}

bool ModuleManager::IsLoaded(std::string_view name) const {
    const Loaded* loaded = Slot(name);
    return loaded && loaded->status.state == ModuleRunState::Loaded;
}

IModuleInterface* ModuleManager::FindModule(std::string_view name) const {
    const Loaded* loaded = Slot(name);
    return loaded && loaded->status.state == ModuleRunState::Loaded ? loaded->instance.get()
                                                                    : nullptr;
}

void ModuleManager::ShutdownAll() {
    m_shutdownOrder.clear();
    std::vector<std::string> reverse = m_loadOrder;
    std::reverse(reverse.begin(), reverse.end());
    for (const std::string& name : reverse) {
        Loaded* loaded = Slot(name);
        if (!loaded || loaded->status.state != ModuleRunState::Loaded) continue;
        if (loaded->instance && loaded->context) {
            try {
                loaded->instance->ShutdownModule(*loaded->context);
            } catch (const std::exception& ex) {
                LUMA_USER_ERROR("Plugins", "{} threw during ShutdownModule: {}", name, ex.what());
            } catch (...) {
                LUMA_USER_ERROR("Plugins", "{} threw during ShutdownModule.", name);
            }
        }
        if (loaded->context) loaded->context->ReleaseAll();
        loaded->instance.reset();
        loaded->context.reset();
        m_backend.Unload(name);
        loaded->status.state = ModuleRunState::Pending;
        m_shutdownOrder.push_back(name);
        LUMA_LOG_INFO("Plugins", "stopped module {}", name);
    }
    m_loadOrder.clear();
    m_loaded.clear();
    m_failedPlugins.clear();
}

std::vector<ModuleStatus> ModuleManager::Statuses() const {
    std::vector<ModuleStatus> out;
    out.reserve(m_loaded.size());
    for (const auto& [name, loaded] : m_loaded) out.push_back(loaded.status);
    return out;
}

}  // namespace Luma::Plugins
