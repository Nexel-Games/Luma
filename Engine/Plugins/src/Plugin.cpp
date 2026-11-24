#include "Luma/Plugins/Plugin.h"

namespace Luma::Plugins {

const char* ToString(PluginHostKind host) {
    switch (host) {
        case PluginHostKind::Runtime: return "Runtime";
        case PluginHostKind::Editor: return "Editor";
    }
    return "Runtime";
}

PluginContext::PluginContext(const PluginManifest& manifest, PluginHostKind host,
                             const PluginHostServices& services)
    : m_manifest(manifest),
      m_host(host),
      m_components(services.components),
      m_sceneSystems(services.sceneSystems),
      m_assetTypes(services.assetTypes),
      m_assetFactories(services.assetFactories),
      m_translators(services.translators),
      m_editor(services.editor),
      m_hasEditor(services.hasEditor) {}

void PluginContext::Track(RegistrationToken token) { m_tokens.Add(std::move(token)); }

void PluginContext::ReleaseAll() { m_tokens.Reset(); }

}  // namespace Luma::Plugins
