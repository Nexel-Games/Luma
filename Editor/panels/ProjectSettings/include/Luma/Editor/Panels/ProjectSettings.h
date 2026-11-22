#pragma once

#include "Luma/Scene/SceneSystems.h"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Registration.h"
#include "Luma/Editor/Panels/ProjectSettingsStore.h"
#include "Luma/Editor/SettingsPageRegistry.h"
#include "Luma/Project/Project.h"
#include "Luma/Slate/Context.h"
#include "Luma/Slate/Types.h"

// Project Settings window (Edit → Project Settings), Unreal-style: a modal
// window with the registered pages grouped by category in a left column and the
// selected page on the right. Every page comes from the SettingsPageRegistry;
// edits mark the page dirty and are written to the project's Config/ files by
// ProjectSettingsStore (Save, or when the window closes).

namespace Luma::Editor::Panels {

class ProjectSettingsPanel {
public:
    explicit ProjectSettingsPanel(const SettingsPageRegistry& pages);

    // The store the window saves through (owned by the editor; null = no
    // project, nothing to edit).
    void SetStore(ProjectSettingsStore* store) { m_store = store; }

    void Open();
    // Closes the window, saving dirty pages first.
    void Close();
    bool IsOpen() const { return m_isOpen; }

    // Draws the modal window when open.
    void Draw(Slate::Context& ui, f32 width, f32 height);

    std::vector<std::string> Categories() const { return m_pages.Categories(); }
    const std::string& SelectedPage() const { return m_selected; }
    void SelectPage(std::string id) { m_selected = std::move(id); }

private:
    void EnsureSelection();
    void DrawSidebar(Slate::Context& ui, const Slate::Rect& rect);
    void SaveNow();

    const SettingsPageRegistry& m_pages;
    ProjectSettingsStore* m_store = nullptr;
    bool m_isOpen = false;
    std::string m_selected;
    std::string m_status;       // last save result shown in the footer
    bool m_statusError = false;
    f32 m_scroll = 0.0f;
};

// --- Project > Maps & Modes (built-in page) -------------------------------------

inline constexpr std::string_view kMapsAndModesPageId = "Project.MapsAndModes";

struct MapsAndModesSettings {
    // Project-relative scene the editor opens (empty = the descriptor's startup
    // scene, which is also what the game starts with).
    std::string editorStartupScene;
};

// The scene the editor opens for `project`: the Maps & Modes override when it
// names an existing scene file, else the project's startup scene.
std::filesystem::path EditorStartupScenePath(const Project& project,
                                             const MapsAndModesSettings& settings);

// Registers the Maps & Modes page (Config/Editor.json). `listScenes` returns the
// project-relative scene paths offered in the picker.
RegistrationToken RegisterMapsAndModesPage(SettingsPageRegistry& pages,
                                           MapsAndModesSettings& settings,
                                           std::function<std::vector<std::string>()> listScenes);

RegistrationToken RegisterSimulationPage(SettingsPageRegistry& pages, SimulationSettings& settings);

}  // namespace Luma::Editor::Panels
