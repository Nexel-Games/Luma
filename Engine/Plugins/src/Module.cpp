#include "Luma/Plugins/Module.h"

#include "Luma/Asset/AssetTypeRegistry.h"
#include "Luma/AssetImport/Factories/FactoryCommon.h"
#include "Luma/AssetImport/TranslatorRegistry.h"
#include "Luma/Core/Assert.h"
#include "Luma/Core/Log.h"

namespace Luma::Plugins {

ModuleContext::ModuleContext(std::string name, const PluginDescriptor* plugin,
                             const PluginHostServices& services)
    : m_name(std::move(name)), m_plugin(plugin), m_services(services) {}

std::filesystem::path ModuleContext::PluginBaseDir() const {
    if (!m_plugin) return {};
    return m_plugin->descriptorPath.parent_path();
}

ComponentRegistry& ModuleContext::Components() {
    LUMA_ASSERT(m_services.components, "ModuleContext has no component registry");
    return *m_services.components;
}

SceneSystemRegistry& ModuleContext::SceneSystems() {
    LUMA_ASSERT(m_services.sceneSystems, "ModuleContext has no scene-system registry");
    return *m_services.sceneSystems;
}

EntityTemplateRegistry& ModuleContext::EntityTemplates() { return Luma::EntityTemplates(); }

AssetTypeRegistry& ModuleContext::AssetTypes() {
    LUMA_ASSERT(m_services.assetTypes, "ModuleContext has no asset-type registry");
    return *m_services.assetTypes;
}

Import::AssetFactoryRegistry& ModuleContext::AssetFactories() {
    LUMA_ASSERT(m_services.assetFactories, "ModuleContext has no asset-factory registry");
    return *m_services.assetFactories;
}

Import::TranslatorRegistry& ModuleContext::Translators() {
    LUMA_ASSERT(m_services.translators, "ModuleContext has no translator registry");
    return *m_services.translators;
}

PluginEditorServices* ModuleContext::Editor() {
    return m_services.hasEditor ? &m_services.editor : nullptr;
}

void ModuleContext::Track(RegistrationToken token) { m_tokens.Add(std::move(token)); }

void ModuleContext::ReleaseAll() { m_tokens.Reset(); }

void ModuleContext::Fail(std::string message) {
    m_failed = true;
    m_failMessage = std::move(message);
    LUMA_USER_ERROR("Plugins", "{}: {}", m_name, m_failMessage);
}

void ModuleContext::Log(LogLevel level, std::string_view message) const {
    Log::Print(level, "Plugins", "[{}] {}", m_name, message);
}

bool StaticModuleBackend::Register(std::string name, Factory factory, std::string* error) {
    auto fail = [&](std::string message) {
        if (error) *error = std::move(message);
        return false;
    };
    if (!IsValidPluginId(name)) return fail("module name \"" + name + "\" is not valid");
    if (!factory) return fail("module \"" + name + "\" registered an empty factory");
    if (m_factories.find(name) != m_factories.end()) {
        return fail("module \"" + name + "\" already has a compiled factory");
    }
    m_factories.emplace(std::move(name), std::move(factory));
    return true;
}

bool StaticModuleBackend::IsAvailable(std::string_view name) const {
    return m_factories.find(name) != m_factories.end();
}

std::unique_ptr<IModuleInterface> StaticModuleBackend::Create(std::string_view name,
                                                              std::string* error) {
    auto it = m_factories.find(name);
    if (it == m_factories.end()) {
        if (error) *error = "no compiled module factory for \"" + std::string(name) + "\"";
        return nullptr;
    }
    return it->second();
}

StaticModuleBackend& StaticModuleBackend::Global() {
    static StaticModuleBackend backend;
    return backend;
}

}  // namespace Luma::Plugins
