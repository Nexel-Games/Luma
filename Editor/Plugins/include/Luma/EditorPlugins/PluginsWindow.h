#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Registration.h"
#include "Luma/Editor/MenuRegistry.h"
#include "Luma/Editor/PanelRegistry.h"
#include "Luma/Plugins/PluginDescriptor.h"
#include "Luma/Plugins/PluginManager.h"
#include "Luma/Plugins/PluginWizard.h"
#include "Luma/Project/Project.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Slate/Context.h"

// Edit → Plugins browser (dockable). Enable/disable is per-project and
// restart-required. Add opens the New Plugin wizard. Edit... opens the
// descriptor editor for project plugins.

namespace Luma::EditorPlugins {

inline constexpr const char* kPluginsPanelId = "plugins";

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

private:
    void DrawWizard(Slate::Context& ui, Plugins::PluginManager& manager, Project* project);
    void DrawDescriptorEditor(Slate::Context& ui, Plugins::PluginManager& manager, Project* project);
    void Rediscover(Plugins::PluginManager& manager, Project* project);

    std::string m_search;
    std::string m_treeKey = "builtin";
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

void RegisterPluginsWindow(RegistrationSet& registrations, Editor::MenuRegistry& menus,
                           Editor::PanelRegistry& panels, PluginsWindow& window,
                           Plugins::PluginManager& manager, std::function<Project*()> project,
                           std::function<const Scene*()> openScene,
                           std::function<void()> openPanel);

}  // namespace Luma::EditorPlugins
