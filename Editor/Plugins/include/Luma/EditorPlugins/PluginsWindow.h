#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Registration.h"
#include "Luma/Editor/MenuRegistry.h"
#include "Luma/Plugins/PluginDescriptor.h"
#include "Luma/Plugins/PluginManager.h"
#include "Luma/Plugins/PluginWizard.h"
#include "Luma/Project/Project.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Slate/Context.h"

// Edit → Plugins browser: a window like the asset editors (floats, docks into
// the window row). Enable/disable is per-project and restart-required. Add
// opens the New Plugin wizard. Edit... opens the descriptor editor for
// project plugins.

namespace Luma::EditorPlugins {

inline constexpr const char* kPluginsWindowId = "plugins";

std::string LowerAscii(std::string_view text);
bool PluginMatchesQuery(const Plugins::PluginStatus& status, std::string_view query);
std::string PluginStatusLine(const Plugins::PluginStatus& status);
std::string PluginCategory(const Plugins::PluginStatus& status);

class PluginsWindow {
public:
    void SetFolders(std::filesystem::path templateRoot, std::filesystem::path enginePlugins,
                    std::filesystem::path projectPlugins);

    void Draw(Slate::Context& ui, const Slate::Rect& area, Plugins::PluginManager& manager,
              Project* project, const Scene* openScene);

    bool TakeRestartNow();

    std::function<void(std::string_view url)> openUrl;
    // Artwork: the Luma logo (header) and the icon of plugins without
    // Resources/Icon128.png. `loadImage` loads a plugin's own icon (cached).
    TextureHandle logo = 0;
    TextureHandle defaultPluginIcon = 0;
    std::function<TextureHandle(const std::filesystem::path&)> loadImage;
    // The engine's branding mark ("LUMA PLUGINS", like the Material Editor's
    // "LUMA MATERIAL"): Rubik Iso, faint, bottom-right of the list.
    const Slate::Font* brandFont = nullptr;
    // Window actions for the Window menu (the host's window manager).
    std::function<void()> onDock;
    std::function<void()> onFloat;
    std::function<void()> onMinimize;
    std::function<void()> onClose;

private:
    void DrawMenuBar(Slate::Context& ui, const Slate::Rect& bar, Plugins::PluginManager& manager,
                     Project* project, const Scene* openScene, bool writable);
    void DrawHeader(Slate::Context& ui, const Slate::Rect& header, const std::vector<Plugins::PluginStatus>& all);
    void OpenWizard();
    void OpenDescriptorEditor(const Plugins::PluginStatus& status);
    void RequestToggle(Plugins::PluginManager& manager, const Plugins::PluginStatus& status, bool enable,
                       const Scene* openScene);
    void DrawConfirm(Slate::Context& ui, Plugins::PluginManager& manager, Project* project);
    void DrawAbout(Slate::Context& ui, Plugins::PluginManager& manager);
    TextureHandle IconFor(const Plugins::PluginStatus& status);
    const Plugins::PluginStatus* Selected(const std::vector<Plugins::PluginStatus>& all) const;
    void DrawWizard(Slate::Context& ui, Plugins::PluginManager& manager, Project* project);
    void DrawDescriptorEditor(Slate::Context& ui, Plugins::PluginManager& manager, Project* project);
    void Rediscover(Plugins::PluginManager& manager, Project* project);

    std::string m_search;
    std::string m_treeKey = "all";  // "all", "builtin", "project", "builtin/<Category>", ...
    std::string m_selected;         // plugin id of the selected card
    f32 m_listScroll = 0.0f;
    f32 m_treeScroll = 0.0f;
    bool m_showEnabled = true;
    bool m_showDisabled = true;
    bool m_showPreview = true;  // Beta / Experimental
    bool m_aboutOpen = false;
    std::map<std::string, TextureHandle> m_icons;  // descriptor path -> icon (0 = none)
    std::string m_message;
    bool m_messageError = false;
    bool m_restartNow = false;
    std::optional<Plugins::PluginManager::ChangePlan> m_pending;

    bool m_wizardOpen = false;
    int m_wizardTemplate = 0;
    int m_wizardLocation = 0;  // 0 project, 1 engine
    std::string m_wizardName;
    std::string m_wizardFriendly;
    std::string m_wizardAuthor;
    std::string m_wizardDescription;
    std::string m_wizardCategory = "Other";

    bool m_editOpen = false;
    Plugins::PluginDescriptor m_edit;
    std::string m_editVersionName;
    std::string m_editIconPath;

    std::filesystem::path m_templateRoot;
    std::filesystem::path m_enginePlugins;
    std::filesystem::path m_projectPlugins;
};

// Edit → Plugins; `openWindow` opens (or focuses) the Plugins window.
void RegisterPluginsWindow(RegistrationSet& registrations, Editor::MenuRegistry& menus,
                           std::function<void()> openWindow);

}  // namespace Luma::EditorPlugins
