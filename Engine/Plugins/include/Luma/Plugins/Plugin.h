#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Registration.h"
#include "Luma/Core/Types.h"
#include "Luma/Plugins/PluginManifest.h"
#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/SceneSystems.h"

// Native plugin instance, host, and the context a plugin may use during
// Startup/Shutdown. Registrations made through Track() are released on
// shutdown and on a failed Startup. Main-thread only.

namespace Luma {
class AssetTypeRegistry;
}

namespace Luma::Import {
class AssetFactoryRegistry;
class TranslatorRegistry;
}

namespace Luma::Editor {
class MenuRegistry;
class PanelRegistry;
class SettingsPageRegistry;
class InspectorDrawerRegistry;
class EditorImageRegistry;
}

namespace Luma::Plugins {

enum class PluginHostKind : u8 { Runtime, Editor };

const char* ToString(PluginHostKind host);

// Editor-only registries. Every pointer is null in a runtime host (LumaEngine).
struct PluginEditorServices {
    Editor::MenuRegistry* menus = nullptr;
    Editor::PanelRegistry* panels = nullptr;
    Editor::SettingsPageRegistry* settingsPages = nullptr;
    Editor::InspectorDrawerRegistry* inspectorDrawers = nullptr;
    Editor::EditorImageRegistry* images = nullptr;
    // Opens (or focuses) a registered panel id. Bound by the editor host.
    std::function<void(std::string_view panelId)> openPanel;
};

// What the host passes into every plugin. Asset pointers may be null in tests.
struct PluginHostServices {
    ComponentRegistry* components = nullptr;
    SceneSystemRegistry* sceneSystems = nullptr;
    AssetTypeRegistry* assetTypes = nullptr;
    Import::AssetFactoryRegistry* assetFactories = nullptr;
    Import::TranslatorRegistry* translators = nullptr;
    PluginEditorServices editor;
    bool hasEditor = false;
};

class PluginContext {
public:
    PluginContext(const PluginManifest& manifest, PluginHostKind host,
                  const PluginHostServices& services = {});

    const PluginManifest& Manifest() const { return m_manifest; }
    PluginHostKind Host() const { return m_host; }

    // Null when the host did not supply that registry (runtime has no editor
    // services; tests may omit scene systems or asset registries).
    ComponentRegistry* Components() const { return m_components; }
    SceneSystemRegistry* SceneSystems() const { return m_sceneSystems; }
    AssetTypeRegistry* AssetTypes() const { return m_assetTypes; }
    Import::AssetFactoryRegistry* AssetFactories() const { return m_assetFactories; }
    Import::TranslatorRegistry* Translators() const { return m_translators; }
    // Null in LumaEngine and in tests that do not pass editor services.
    const PluginEditorServices* Editor() const { return m_hasEditor ? &m_editor : nullptr; }

    // Takes ownership of a live token. Inactive tokens are ignored.
    void Track(RegistrationToken token);
    usize TrackedCount() const { return m_tokens.Size(); }

    // Unregisters everything this plugin tracked, in reverse order.
    void ReleaseAll();

private:
    const PluginManifest& m_manifest;
    PluginHostKind m_host = PluginHostKind::Runtime;
    ComponentRegistry* m_components = nullptr;
    SceneSystemRegistry* m_sceneSystems = nullptr;
    AssetTypeRegistry* m_assetTypes = nullptr;
    Import::AssetFactoryRegistry* m_assetFactories = nullptr;
    Import::TranslatorRegistry* m_translators = nullptr;
    PluginEditorServices m_editor;
    bool m_hasEditor = false;
    RegistrationSet m_tokens;
};

class IPlugin {
public:
    virtual ~IPlugin() = default;
    // Register into ctx. Return false and set `error` on failure. Throwing is
    // treated as failure. Do not assume Shutdown runs after a failed Startup.
    virtual bool Startup(PluginContext& ctx, std::string& error) = 0;
    virtual void Shutdown(PluginContext& ctx) = 0;
};

// How a compiled-in plugin is constructed. A dynamic loader can implement the
// same interface later; none is provided in this phase.
class IPluginLoader {
public:
    virtual ~IPluginLoader() = default;
    virtual bool IsAvailable(std::string_view id) const = 0;
    virtual std::unique_ptr<IPlugin> Load(std::string_view id, std::string* error) = 0;
};

}  // namespace Luma::Plugins
