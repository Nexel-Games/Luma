#include "Luma/Plugins/PluginManager.h"

#include <algorithm>
#include <exception>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>
#include <sstream>
#include <utility>

#include "Luma/Core/Log.h"
#include "Luma/Project/Project.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Serialization/Json.h"

namespace Luma::Plugins {
namespace {

std::vector<std::string> SplitCycle(const std::vector<std::string>& stack,
                                    std::string_view backEdgeTo) {
    auto start = std::find(stack.begin(), stack.end(), backEdgeTo);
    std::vector<std::string> cycle;
    if (start == stack.end()) {
        cycle.push_back(std::string(backEdgeTo));
        return cycle;
    }
    cycle.assign(start, stack.end());
    cycle.emplace_back(backEdgeTo);
    return cycle;
}

std::string FormatCycle(const std::vector<std::string>& cycle) {
    std::ostringstream ss;
    ss << "plugin dependency cycle: ";
    for (usize i = 0; i < cycle.size(); ++i) {
        if (i) ss << " -> ";
        ss << cycle[i];
    }
    ss << ". Break the cycle in the manifests.";
    return ss.str();
}

}  // namespace

const char* ToString(PluginState state) {
    switch (state) {
        case PluginState::Discovered: return "Discovered";
        case PluginState::Invalid: return "Invalid";
        case PluginState::Disabled: return "Disabled";
        case PluginState::Unavailable: return "Unavailable";
        case PluginState::Resolved: return "Resolved";
        case PluginState::Started: return "Started";
        case PluginState::Failed: return "Failed";
        case PluginState::Stopped: return "Stopped";
        case PluginState::NotInstalled: return "Not installed";
    }
    return "Discovered";
}

const char* ToString(PluginOrigin origin) {
    switch (origin) {
        case PluginOrigin::BuiltIn: return "Built-In";
        case PluginOrigin::Project: return "Project";
    }
    return "Built-In";
}

PluginManager::PluginManager(CompiledPluginTable& compiled) : m_compiled(compiled) {}

void PluginManager::SetRegistries(ComponentRegistry* components,
                                  SceneSystemRegistry* sceneSystems) {
    m_hostServices.components = components;
    m_hostServices.sceneSystems = sceneSystems;
    m_components = components;
    m_sceneSystems = sceneSystems;
}

void PluginManager::SetHostServices(PluginHostServices services) {
    m_hostServices = std::move(services);
    m_components = m_hostServices.components;
    m_sceneSystems = m_hostServices.sceneSystems;
}

PluginManager::Slot* PluginManager::SlotById(std::string_view id) {
    auto it = m_slots.find(std::string(id));
    return it == m_slots.end() ? nullptr : &it->second;
}

const PluginManager::Slot* PluginManager::SlotById(std::string_view id) const {
    auto it = m_slots.find(std::string(id));
    return it == m_slots.end() ? nullptr : &it->second;
}

bool PluginManager::IsEnabled(std::string_view id) const {
    return m_enableAllDiscovered || m_enabled.count(std::string(id)) != 0;
}

void PluginManager::SyncEnabledFlags() {
    for (auto& [id, slot] : m_slots) {
        slot.status.enabledInProject = IsEnabled(id);
        const bool running = slot.status.state == PluginState::Started;
        if (slot.status.state == PluginState::NotInstalled) {
            slot.status.restartRequired = false;
        } else {
            slot.status.restartRequired = slot.status.enabledInProject != running;
        }
    }
}

void PluginManager::DropNotInstalledSlots() {
    for (auto it = m_slots.begin(); it != m_slots.end();) {
        if (it->second.status.state == PluginState::NotInstalled) {
            it = m_slots.erase(it);
        } else {
            ++it;
        }
    }
}

void PluginManager::AddNotInstalled(const std::string& id, bool enabledInProject) {
    if (SlotById(id)) return;
    Slot slot;
    slot.status.manifest.id = id;
    slot.status.manifest.displayName = id;
    slot.status.state = PluginState::NotInstalled;
    slot.status.compiledIn = false;
    slot.status.enabledInProject = enabledInProject;
    slot.status.descriptor.name = id;
    slot.status.descriptor.friendlyName = id;
    slot.status.origin = PluginOrigin::Project;
    slot.status.diagnostics.push_back(
        id + " is listed in the project but is not installed. Install it under Plugins/" +
        id + " or remove it from the project's plugins list.");
    m_slots.emplace(id, std::move(slot));
    if (enabledInProject) m_enabled.insert(id);
}

void PluginManager::MarkUnavailable(Slot& slot, std::string message) {
    slot.status.state = PluginState::Unavailable;
    slot.status.diagnostics.push_back(message);
    LUMA_USER_ERROR("Plugins", "{}", message);
}

void PluginManager::Discover(std::span<const std::filesystem::path> roots) {
    ShutdownAll();
    m_slots.clear();
    m_enabled.clear();
    m_enableAllDiscovered = true;
    m_startOrder.clear();
    m_shutdownOrder.clear();
    m_resolveOrder.clear();
    m_searchRoots.assign(roots.begin(), roots.end());
    DllModuleBackend::Global().SetSearchRoots(roots);
    m_discovery = DiscoverPlugins(roots, &m_compiled);
    auto originOf = [&](const std::filesystem::path& searchRoot) {
        if (m_searchRoots.size() >= 2 && searchRoot == m_searchRoots[1]) {
            return PluginOrigin::Project;
        }
        return PluginOrigin::BuiltIn;
    };
    for (DiscoveredPlugin& found : m_discovery.plugins) {
        Slot slot;
        slot.status.manifest = found.manifest;
        slot.descriptor = found.descriptor;
        slot.status.descriptor = found.descriptor;
        slot.status.origin = originOf(found.searchRoot);
        slot.status.compiledIn = found.compiledIn;
        slot.status.state = PluginState::Discovered;
        slot.discoveryDiagnostics = found.diagnostics;
        slot.status.diagnostics = found.diagnostics;
        m_slots.emplace(found.manifest.id, std::move(slot));
    }
    SyncEnabledFlags();
}

void PluginManager::ApplyProject(const Project* project) {
    DropNotInstalledSlots();
    m_enabled.clear();
    if (!project) {
        m_enableAllDiscovered = true;
        SyncEnabledFlags();
        return;
    }
    m_enableAllDiscovered = false;
    const SerialValue* plugins = project->Find("plugins");
    std::vector<std::string> listedEnabled;
    std::vector<std::string> listedDisabled;
    if (plugins && plugins->IsObject()) {
        if (const SerialValue* enabled = plugins->Find("enabled");
            enabled && enabled->IsArray()) {
            for (const SerialValue& v : enabled->Elements()) {
                if (v.IsString()) listedEnabled.push_back(v.AsString());
            }
        }
        if (const SerialValue* disabled = plugins->Find("disabled");
            disabled && disabled->IsArray()) {
            for (const SerialValue& v : disabled->Elements()) {
                if (v.IsString()) listedDisabled.push_back(v.AsString());
            }
        }
    }
    std::set<std::string> skip(listedDisabled.begin(), listedDisabled.end());
    std::set<std::string> want(listedEnabled.begin(), listedEnabled.end());
    const bool hasSection = plugins && plugins->IsObject();
    for (auto& [id, slot] : m_slots) {
        if (skip.count(id)) continue;
        if (want.count(id) || slot.status.manifest.enabledByDefault || !hasSection) {
            if (!hasSection && !slot.status.manifest.enabledByDefault) continue;
            m_enabled.insert(id);
        }
    }
    if (!hasSection) {
        SyncEnabledFlags();
        return;
    }
    auto known = [&](const std::string& id) { return SlotById(id) != nullptr; };
    for (const std::string& id : listedEnabled) {
        if (!known(id) && !skip.count(id)) AddNotInstalled(id, true);
    }
    for (const std::string& id : listedDisabled) {
        if (!known(id)) AddNotInstalled(id, false);
    }
    SyncEnabledFlags();
}

void PluginManager::RestrictEnabled(std::span<const std::string> ids) {
    m_enableAllDiscovered = false;
    m_enabled.clear();
    for (const std::string& id : ids) m_enabled.insert(id);
    SyncEnabledFlags();
}

bool PluginManager::MayRunOn(PluginKind kind, PluginHostKind host) const {
    if (host == PluginHostKind::Editor) return true;
    return kind != PluginKind::Editor;
}

void PluginManager::Resolve() {
    for (auto& [id, slot] : m_slots) {
        const bool enabled =
            m_enableAllDiscovered || m_enabled.count(id) != 0;
        slot.status.enabledInProject = enabled;
        if (slot.status.state == PluginState::Started) continue;
        if (slot.status.state == PluginState::NotInstalled) continue;
        slot.instance.reset();
        slot.context.reset();
        slot.status.diagnostics = slot.discoveryDiagnostics;
        if (!enabled) {
            slot.status.state = PluginState::Disabled;
            continue;
        }
        slot.status.state = PluginState::Discovered;
        // Keep discovery diagnostics; do not clear them.
        if (!slot.status.compiledIn && !IsContentOnly(slot)) {
            MarkUnavailable(slot, id + " is not built in this binary. It cannot start until "
                                     "it is compiled in (see LUMA_PLUGIN flags in a later "
                                     "phase) or removed from the enabled set.");
            continue;
        }
        if (!slot.status.manifest.engineVersionRange.empty()) {
            std::string rangeError;
            if (!SatisfiesRange(Luma::EngineVersion(),
                                slot.status.manifest.engineVersionRange, &rangeError)) {
                MarkUnavailable(slot, id + " " + slot.status.manifest.version.ToString() +
                                         " requires engine " +
                                         slot.status.manifest.engineVersionRange +
                                         ", but this engine is " +
                                         Luma::EngineVersion().ToString() +
                                         ". Update the engine or change engineVersionRange.");
                continue;
            }
        }
        bool platformOk = slot.status.manifest.platforms.empty();
        for (const std::string& p : slot.status.manifest.platforms) {
            if (p == HostPlatform()) platformOk = true;
        }
        if (!platformOk) {
            MarkUnavailable(slot, id + " does not support " + std::string(HostPlatform()) +
                                     ". Add this platform to the manifest or do not enable "
                                     "the plugin here.");
            continue;
        }
        if (!MayRunOn(slot.status.manifest.kind, m_host)) {
            MarkUnavailable(slot, id + " is Editor-only and this process is a Runtime host.");
            continue;
        }
        slot.status.state = PluginState::Resolved;
    }

    auto requiredMissing = [&](Slot& slot) -> bool {
        if (slot.status.state != PluginState::Resolved) return false;
        for (const PluginDependency& dep : slot.status.manifest.dependencies) {
            if (dep.optional) continue;
            const Slot* other = SlotById(dep.id);
            if (!other) {
                MarkUnavailable(slot, slot.status.manifest.id + " requires " + dep.id +
                                         " " + dep.versionRange +
                                         ", which is not installed. Install that plugin or "
                                         "disable " +
                                         slot.status.manifest.id + ".");
                return true;
            }
            std::string rangeError;
            if (!SatisfiesRange(other->status.manifest.version, dep.versionRange, &rangeError)) {
                MarkUnavailable(slot, slot.status.manifest.id + " requires " + dep.id + " " +
                                         dep.versionRange + ", but " + dep.id + " " +
                                         other->status.manifest.version.ToString() +
                                         " is installed. Install a matching version or "
                                         "change the dependency range.");
                return true;
            }
            if (!other->status.enabledInProject) {
                MarkUnavailable(slot, slot.status.manifest.id + " requires " + dep.id +
                                         ", which is disabled. Enable " + dep.id +
                                         " as well, or disable " + slot.status.manifest.id +
                                         ".");
                return true;
            }
            if (other->status.state == PluginState::Unavailable ||
                other->status.state == PluginState::Failed) {
                MarkUnavailable(slot, slot.status.manifest.id + " cannot start because " +
                                         dep.id + " is unavailable.");
                return true;
            }
        }
        return false;
    };

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& [id, slot] : m_slots) {
            if (requiredMissing(slot)) changed = true;
        }
    }

    std::map<std::string, std::vector<std::string>> depsOf;
    std::set<std::string> candidates;
    for (auto& [id, slot] : m_slots) {
        if (slot.status.state != PluginState::Resolved) continue;
        candidates.insert(id);
        for (const PluginDependency& dep : slot.status.manifest.dependencies) {
            if (dep.optional) {
                const Slot* other = SlotById(dep.id);
                if (!other || other->status.state != PluginState::Resolved) continue;
            }
            depsOf[id].push_back(dep.id);
        }
    }

    std::vector<std::string> cycleMessage;
    std::map<std::string, u8> color;  // 0 white, 1 gray, 2 black
    std::vector<std::string> stack;
    std::function<void(const std::string&)> dfs = [&](const std::string& id) {
        color[id] = 1;
        stack.push_back(id);
        for (const std::string& dep : depsOf[id]) {
            if (candidates.count(dep) == 0) continue;
            if (color[dep] == 1) {
                cycleMessage = SplitCycle(stack, dep);
                return;
            }
            if (color[dep] == 0) {
                dfs(dep);
                if (!cycleMessage.empty()) return;
            }
        }
        stack.pop_back();
        color[id] = 2;
    };
    for (const std::string& id : candidates) {
        if (color[id] == 0) dfs(id);
        if (!cycleMessage.empty()) break;
    }
    if (!cycleMessage.empty()) {
        const std::string message = FormatCycle(cycleMessage);
        std::set<std::string> inCycle(cycleMessage.begin(), cycleMessage.end());
        for (const std::string& id : inCycle) {
            if (Slot* slot = SlotById(id)) {
                if (slot->status.state == PluginState::Resolved) MarkUnavailable(*slot, message);
            }
        }
    }

    changed = true;
    while (changed) {
        changed = false;
        for (auto& [id, slot] : m_slots) {
            if (requiredMissing(slot)) changed = true;
        }
    }
}

std::vector<std::string> PluginManager::DependentsOf(std::string_view id) const {
    std::vector<std::string> out;
    for (const auto& [otherId, slot] : m_slots) {
        for (const PluginDependency& dep : slot.status.manifest.dependencies) {
            if (!dep.optional && dep.id == id) out.push_back(otherId);
        }
    }
    return out;
}

std::set<std::string> PluginManager::TransitiveDependents(std::string_view id) const {
    std::set<std::string> out;
    std::vector<std::string> stack = DependentsOf(id);
    while (!stack.empty()) {
        const std::string next = std::move(stack.back());
        stack.pop_back();
        if (!out.insert(next).second) continue;
        for (const std::string& child : DependentsOf(next)) stack.push_back(child);
    }
    return out;
}

bool PluginManager::StartAll(PluginHostKind host) {
    m_host = host;
    m_startOrder.clear();
    m_shutdownOrder.clear();
    Resolve();

    std::map<std::string, std::vector<std::string>> adj;
    std::map<std::string, i32> indegree;
    std::set<std::string> ready;
    for (auto& [id, slot] : m_slots) {
        if (slot.status.state != PluginState::Resolved) continue;
        indegree.emplace(id, 0);
    }
    for (auto& [id, slot] : m_slots) {
        if (slot.status.state != PluginState::Resolved) continue;
        for (const PluginDependency& dep : slot.status.manifest.dependencies) {
            if (indegree.count(dep.id) == 0) continue;
            adj[dep.id].push_back(id);
            indegree[id]++;
        }
    }
    for (auto& [id, deg] : indegree) {
        if (deg == 0) ready.insert(id);
    }
    std::vector<std::string> order;
    while (!ready.empty()) {
        const std::string id = *ready.begin();
        ready.erase(ready.begin());
        order.push_back(id);
        for (const std::string& to : adj[id]) {
            if (--indegree[to] == 0) ready.insert(to);
        }
    }
    if (order.size() != indegree.size()) {
        for (auto& [id, slot] : m_slots) {
            if (slot.status.state == PluginState::Resolved &&
                std::find(order.begin(), order.end(), id) == order.end()) {
                MarkUnavailable(slot, "plugin dependency cycle involving \"" + id +
                                         "\". Break the cycle in the manifests.");
            }
        }
    }
    m_resolveOrder = order;

    auto hasCompiledModules = [](const PluginDescriptor& descriptor) {
        for (const ModuleDescriptor& module : descriptor.modules) {
            if (StaticModuleBackend::Global().IsAvailable(module.name) ||
                DllModuleBackend::Global().IsAvailable(module.name)) {
                return true;
            }
        }
        return false;
    };

    std::set<std::string> blocked;
    bool anyFailed = false;
    for (const std::string& id : order) {
        Slot* slot = SlotById(id);
        if (!slot || slot->status.state != PluginState::Resolved) continue;
        if (blocked.count(id)) {
            MarkUnavailable(*slot, id + " did not start because a plugin it depends on failed.");
            continue;
        }

        const bool moduleOnly =
            !m_compiled.IsAvailable(id) && hasCompiledModules(slot->descriptor);
        if (moduleOnly) continue;
        if (IsContentOnly(*slot)) {
            slot->status.state = PluginState::Started;
            m_startOrder.push_back(id);
            continue;
        }

        std::string loadError;
        slot->instance = m_compiled.Instantiate(id, &loadError);
        if (!slot->instance) {
            const std::string message =
                id + " failed to construct" +
                (loadError.empty() ? "." : (": " + loadError));
            RollbackFailed(*slot, message);
            anyFailed = true;
            for (const std::string& dep : TransitiveDependents(id)) blocked.insert(dep);
            continue;
        }

        slot->context = std::make_unique<PluginContext>(slot->status.manifest, host,
                                                        m_hostServices);
        std::string error;
        bool ok = false;
        try {
            ok = slot->instance->Startup(*slot->context, error);
        } catch (const std::exception& ex) {
            ok = false;
            error = ex.what();
        } catch (...) {
            ok = false;
            error = "threw an unknown exception";
        }
        if (!ok) {
            const std::string message =
                id + " failed to start" + (error.empty() ? "." : (": " + error));
            RollbackFailed(*slot, message);
            anyFailed = true;
            for (const std::string& dep : TransitiveDependents(id)) blocked.insert(dep);
            continue;
        }
        slot->status.state = PluginState::Started;
        m_startOrder.push_back(id);
        LUMA_LOG_INFO("Plugins", "started {}", id);
    }
    for (const auto& [id, slot] : m_slots) {
        if (!slot.status.enabledInProject) continue;
        if (slot.status.state == PluginState::Started) continue;
        const bool waitingForModules =
            slot.status.state == PluginState::Resolved && hasCompiledModules(slot.descriptor);
        if (!waitingForModules) anyFailed = true;
    }
    SyncEnabledFlags();
    return !anyFailed;
}

void PluginManager::RollbackFailed(Slot& slot, std::string message) {
    if (slot.context) slot.context->ReleaseAll();
    slot.instance.reset();
    slot.context.reset();
    slot.status.state = PluginState::Failed;
    slot.status.diagnostics.push_back(message);
    LUMA_USER_ERROR("Plugins", "{}", message);
}

void PluginManager::ShutdownAll() {
    m_shutdownOrder.clear();
    for (auto it = m_startOrder.rbegin(); it != m_startOrder.rend(); ++it) {
        Slot* slot = SlotById(*it);
        if (!slot || slot->status.state != PluginState::Started) continue;
        if (slot->instance && slot->context) {
            try {
                slot->instance->Shutdown(*slot->context);
            } catch (const std::exception& ex) {
                LUMA_USER_ERROR("Plugins", "{} threw during Shutdown: {}", *it, ex.what());
            } catch (...) {
                LUMA_USER_ERROR("Plugins", "{} threw during Shutdown.", *it);
            }
        }
        if (slot->context) slot->context->ReleaseAll();
        slot->instance.reset();
        slot->context.reset();
        slot->status.state = PluginState::Stopped;
        m_shutdownOrder.push_back(*it);
        LUMA_LOG_INFO("Plugins", "stopped {}", *it);
    }
    m_startOrder.clear();
    SyncEnabledFlags();
    UnmountContentAndConfig();
}

void PluginManager::CollectRequiredEnable(const std::string& id, ChangePlan& plan,
                                          std::set<std::string>& visiting) const {
    if (!visiting.insert(id).second) return;
    const Slot* slot = SlotById(id);
    if (!slot || slot->status.state == PluginState::NotInstalled) {
        plan.blockedBy.push_back(id + " is not installed");
        return;
    }
    for (const PluginDependency& dep : slot->status.manifest.dependencies) {
        if (dep.optional) continue;
        const Slot* other = SlotById(dep.id);
        if (!other || other->status.state == PluginState::NotInstalled) {
            plan.blockedBy.push_back(id + " requires " + dep.id + " " + dep.versionRange +
                                     ", which is not installed");
            continue;
        }
        std::string rangeError;
        if (!SatisfiesRange(other->status.manifest.version, dep.versionRange, &rangeError)) {
            plan.blockedBy.push_back(id + " requires " + dep.id + " " + dep.versionRange +
                                     ", but " + dep.id + " " +
                                     other->status.manifest.version.ToString() + " is installed");
            continue;
        }
        CollectRequiredEnable(dep.id, plan, visiting);
        if (!IsEnabled(dep.id) &&
            std::find(plan.alsoEnable.begin(), plan.alsoEnable.end(), dep.id) ==
                plan.alsoEnable.end() &&
            dep.id != plan.targetId) {
            plan.alsoEnable.push_back(dep.id);
        }
    }
}

PluginManager::ChangePlan PluginManager::PlanEnable(std::string_view id) const {
    ChangePlan plan;
    plan.targetId = std::string(id);
    plan.enable = true;
    const Slot* slot = SlotById(id);
    if (!slot || slot->status.state == PluginState::NotInstalled) {
        plan.blockedBy.push_back(plan.targetId + " is not installed");
        plan.restart = false;
        return plan;
    }
    std::set<std::string> visiting;
    CollectRequiredEnable(plan.targetId, plan, visiting);
    const bool alreadyOn = IsEnabled(plan.targetId);
    plan.restart = !alreadyOn || slot->status.state != PluginState::Started ||
                   !plan.alsoEnable.empty();
    if (alreadyOn && plan.alsoEnable.empty() && plan.blockedBy.empty()) {
        plan.restart = slot->status.restartRequired;
    }
    plan.beta = slot->descriptor.isBetaVersion;
    plan.experimental = slot->descriptor.isExperimentalVersion;
    return plan;
}

PluginManager::ChangePlan PluginManager::PlanDisable(std::string_view id,
                                                     const Scene* openScene) const {
    ChangePlan plan;
    plan.targetId = std::string(id);
    plan.enable = false;
    const Slot* slot = SlotById(id);
    if (!slot) {
        plan.blockedBy.push_back(plan.targetId + " is not installed");
        plan.restart = false;
        return plan;
    }
    std::set<std::string> deps = TransitiveDependents(plan.targetId);
    for (const std::string& dep : deps) {
        if (IsEnabled(dep)) plan.dependents.push_back(dep);
    }
    std::sort(plan.dependents.begin(), plan.dependents.end());
    std::set<std::string> owners;
    owners.insert(plan.targetId);
    for (const std::string& dep : plan.dependents) owners.insert(dep);
    if (openScene && m_components) {
        auto view = openScene->Registry().view<const NameComponent>();
        for (Entity entity : view) {
            const std::string& name = view.get<const NameComponent>(entity).name;
            for (const ComponentDesc* desc : m_components->All()) {
                if (!desc || owners.count(desc->owner) == 0 || !desc->has) continue;
                if (desc->has(*openScene, entity)) {
                    plan.affectedComponents.push_back(desc->id + " on " + name);
                }
            }
            if (const auto* unresolved =
                    openScene->Registry().try_get<UnresolvedComponents>(entity)) {
                for (const UnresolvedComponent& payload : unresolved->payloads) {
                    if (owners.count(payload.owner) != 0) {
                        plan.affectedComponents.push_back(payload.id + " on " + name);
                    }
                }
            }
        }
    }
    const bool alreadyOff = !IsEnabled(plan.targetId);
    plan.restart = !alreadyOff && slot->status.state == PluginState::Started;
    if (!alreadyOff && slot->status.state != PluginState::Started) {
        plan.restart = true;
    }
    if (alreadyOff) plan.restart = slot->status.restartRequired;
    return plan;
}

bool PluginManager::Commit(Project& project, const ChangePlan& plan, std::string* error) {
    auto fail = [&](std::string message) {
        LUMA_USER_ERROR("Plugins", "{}", message);
        if (error) *error = std::move(message);
        return false;
    };
    if (plan.targetId.empty()) return fail("Commit was called without a plugin id.");
    if (!plan.blockedBy.empty()) {
        return fail(plan.targetId + " cannot be changed: " + plan.blockedBy.front());
    }
    if (!project.IsWritable()) {
        return fail("The project file is read-only (" + project.ProjectFile().string() +
                    "). Make the file writable to change which plugins are enabled.");
    }

    SerialValue plugins = SerialValue::MakeObject();
    if (const SerialValue* existing = project.Find("plugins");
        existing && existing->IsObject()) {
        plugins = *existing;
    }
    plugins["schema"] = 1;

    auto readIds = [](const SerialValue* arr) {
        std::vector<std::string> out;
        if (!arr || !arr->IsArray()) return out;
        for (const SerialValue& v : arr->Elements()) {
            if (v.IsString()) out.push_back(v.AsString());
        }
        return out;
    };
    std::vector<std::string> enabled = readIds(plugins.Find("enabled"));
    std::vector<std::string> disabled = readIds(plugins.Find("disabled"));

    auto enableOne = [&](const std::string& id) {
        std::erase(disabled, id);
        if (std::find(enabled.begin(), enabled.end(), id) == enabled.end()) {
            enabled.push_back(id);
        }
    };
    auto disableOne = [&](const std::string& id) {
        std::erase(enabled, id);
        if (std::find(disabled.begin(), disabled.end(), id) == disabled.end()) {
            disabled.push_back(id);
        }
    };

    if (plan.enable) {
        enableOne(plan.targetId);
        for (const std::string& id : plan.alsoEnable) enableOne(id);
    } else {
        disableOne(plan.targetId);
    }

    auto writeIds = [](const std::vector<std::string>& ids) {
        SerialValue arr = SerialValue::MakeArray();
        for (const std::string& id : ids) arr.PushBack(id);
        return arr;
    };
    plugins["enabled"] = writeIds(enabled);
    plugins["disabled"] = writeIds(disabled);

    std::string setError;
    if (!project.Set("plugins", std::move(plugins), &setError)) {
        return fail(setError.empty() ? "Could not write the plugins section." : setError);
    }
    if (!project.Save(&setError)) {
        return fail(setError.empty() ? "Could not save the project file." : setError);
    }
    ApplyProject(&project);
    return true;
}

std::vector<PluginDescriptor> PluginManager::LoadableDescriptors() const {
    std::vector<PluginDescriptor> out;
    auto push = [&](const std::string& id) {
        const Slot* slot = SlotById(id);
        if (!slot) return;
        if (slot->status.state != PluginState::Resolved &&
            slot->status.state != PluginState::Started) {
            return;
        }
        if (slot->descriptor.name.empty()) return;
        out.push_back(slot->descriptor);
    };
    if (!m_resolveOrder.empty()) {
        for (const std::string& id : m_resolveOrder) push(id);
    } else {
        for (const auto& [id, slot] : m_slots) push(id);
    }
    return out;
}

void PluginManager::ApplyModuleStatuses(const ModuleManager& modules) {
    std::map<std::string, std::vector<ModuleStatus>> byPlugin;
    for (const ModuleStatus& status : modules.Statuses()) {
        byPlugin[status.pluginName].push_back(status);
    }
    for (auto& [id, slot] : m_slots) {
        auto it = byPlugin.find(id);
        if (it == byPlugin.end()) continue;
        bool anyFailed = false;
        bool anyLoaded = false;
        bool anyPending = false;
        std::string failMessage;
        for (const ModuleStatus& status : it->second) {
            if (status.state == ModuleRunState::Failed) {
                anyFailed = true;
                if (failMessage.empty()) failMessage = status.error;
            } else if (status.state == ModuleRunState::Loaded) {
                anyLoaded = true;
            } else if (status.state == ModuleRunState::Pending) {
                anyPending = true;
            }
        }
        if (anyFailed) {
            if (slot.status.state == PluginState::Started) {
                RollbackFailed(slot, failMessage.empty() ? (id + " module failed.") : failMessage);
            } else {
                slot.status.state = PluginState::Failed;
                if (!failMessage.empty()) slot.status.diagnostics.push_back(failMessage);
            }
            continue;
        }
        if (anyPending) continue;
        if (anyLoaded) {
            if (slot.status.state == PluginState::Resolved) {
                slot.status.state = PluginState::Started;
                if (std::find(m_startOrder.begin(), m_startOrder.end(), id) == m_startOrder.end()) {
                    m_startOrder.push_back(id);
                }
                LUMA_LOG_INFO("Plugins", "started {} (modules)", id);
            }
        } else if (slot.status.state == PluginState::Resolved && !IsContentOnly(slot)) {
            MarkUnavailable(slot, id + " has no modules that load in this process.");
        }
    }
    SyncEnabledFlags();
}

bool PluginManager::RestartRequired() const {
    for (const auto& [id, slot] : m_slots) {
        if (slot.status.restartRequired) return true;
    }
    return false;
}

std::vector<PluginStatus> PluginManager::Statuses() const {
    std::vector<PluginStatus> out;
    out.reserve(m_slots.size());
    for (const auto& [id, slot] : m_slots) out.push_back(slot.status);
    return out;
}

const PluginStatus* PluginManager::Find(std::string_view id) const {
    const Slot* slot = SlotById(id);
    return slot ? &slot->status : nullptr;
}

bool PluginManager::IsContentOnly(const Slot& slot) const {
    return slot.descriptor.canContainContent && slot.descriptor.modules.empty();
}

bool PluginManager::ShouldMountContent(const Slot& slot) const {
    if (!slot.descriptor.canContainContent) return false;
    if (slot.status.state != PluginState::Resolved &&
        slot.status.state != PluginState::Started) {
        return false;
    }
    const std::string& name = slot.status.manifest.id;
    return name != "Game" && name != "Engine";
}

void PluginManager::LoadDefaultConfig(const Slot& slot) {
    if (slot.descriptor.descriptorPath.empty()) return;
    const std::string& name = slot.status.manifest.id;
    const std::filesystem::path file =
        slot.descriptor.descriptorPath.parent_path() / "Config" / ("Default" + name + ".json");
    SerialValue doc = SerialValue::MakeObject();
    std::error_code ec;
    if (std::filesystem::is_regular_file(file, ec)) {
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            LUMA_USER_ERROR("Plugins", "{}: could not read default config {}", name,
                            file.string());
        } else {
            const std::string text((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
            std::string parseError;
            auto parsed = ParseJson(text, &parseError);
            if (!parsed || !parsed->IsObject()) {
                LUMA_USER_ERROR("Plugins",
                                "{}: Config/Default{}.json is not a JSON object ({})", name,
                                name, parsed ? std::string("not an object") : parseError);
            } else {
                doc = std::move(*parsed);
            }
        }
    }
    m_pluginDefaults[name] = std::move(doc);
}

void PluginManager::UnmountContentAndConfig() {
    if (m_mountedPaths) {
        m_mountedPaths->UnmountAllPlugins();
        m_mountedPaths->ClearKnownPlugins();
        m_mountedPaths = nullptr;
    }
    if (m_mountedRegistry) {
        const std::vector<ContentRoot> roots = m_mountedRegistry->ContentRoots();
        for (const ContentRoot& root : roots) {
            if (root.kind == ContentRootKind::Plugin) m_mountedRegistry->RemoveRoot(root.path);
        }
        m_mountedRegistry = nullptr;
    }
    m_pluginDefaults.clear();
}

void PluginManager::MountContentAndConfig(AssetPathResolver* paths, AssetRegistry* registry) {
    UnmountContentAndConfig();
    Resolve();
    m_mountedPaths = paths;
    m_mountedRegistry = registry;
    if (paths) {
        for (const auto& [id, slot] : m_slots) {
            if (slot.status.state == PluginState::NotInstalled) continue;
            paths->AddKnownPlugin(id);
        }
    }
    for (auto& [id, slot] : m_slots) {
        LoadDefaultConfig(slot);
        if (!ShouldMountContent(slot)) continue;
        const std::filesystem::path content =
            slot.descriptor.descriptorPath.parent_path() / "Content";
        if (paths) paths->MountPlugin(id, content);
        if (registry) registry->AddRoot(content, ContentRootKind::Plugin, id);
        LUMA_LOG_INFO("Plugins", "mounted content /{}/ -> {}", id, content.string());
    }
}

const SerialValue* PluginManager::PluginDefaultConfig(std::string_view pluginName) const {
    auto it = m_pluginDefaults.find(std::string(pluginName));
    if (it == m_pluginDefaults.end()) return nullptr;
    return &it->second;
}

}  // namespace Luma::Plugins
