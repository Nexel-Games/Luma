#pragma once

#include <functional>
#include <string>
#include <vector>

#include "PanelContext.h"
#include "Luma/Slate/Context.h"
#include "Luma/Slate/Types.h"

// Project Settings window — UE5-style dockable panel with a left sidebar
// navigation tree and a content area on the right. Categories include:
//   - Project (General, Maps & Modes)
//   - Rendering (General, Shadows, Post Processing, Quality)
//   - Editor (General)
//   - Platforms (Windows)
//
// Each category has sub-categories that show different setting controls.
// Settings are stored in a simple struct and persisted to project.ini.

namespace Luma::Editor::Panels {

// Rendering settings that control the deferred renderer
struct RenderingSettings {
    // General
    bool msaaEnabled = true;
    int msaaSamples = 4;         // 1, 2, 4, 8
    bool fxaaEnabled = true;
    bool bloomEnabled = true;
    float bloomIntensity = 0.3f;
    int bloomPasses = 5;         // 3-7

    // Tone Mapping
    int toneMappingMode = 0;     // 0=ACES, 1=Reinhard, 2=Linear
    float exposure = 1.0f;
    bool autoExposure = false;

    // Shadows
    bool shadowsEnabled = true;
    int shadowMapSize = 2048;    // 512, 1024, 2048, 4096
    int cascadeCount = 4;        // 1-4
    float shadowDistance = 80.0f;
    float shadowSoftness = 1.6f;

    // Post Processing
    bool ssaoEnabled = false;
    float ssaoIntensity = 1.0f;
    bool screenSpaceReflections = false;
    bool motionBlur = false;
    float motionBlurAmount = 0.5f;
};

// Project settings
struct ProjectSettingsData {
    std::string projectName = "My Project";
    std::string projectVersion = "1.0.0";
    std::string startupScene = "Scenes/Main.lscene";
    RenderingSettings rendering;
};

class ProjectSettingsPanel {
public:
    ProjectSettingsPanel() = default;

    // Draw the project settings window. Returns true if the window is open.
    bool Draw(Slate::Context& ui, f32 width, f32 height);

    // Open/close the window
    void Open() { m_isOpen = true; }
    void Close() { m_isOpen = false; }
    bool IsOpen() const { return m_isOpen; }

    // Access settings
    ProjectSettingsData& GetSettings() { return m_settings; }
    const ProjectSettingsData& GetSettings() const { return m_settings; }

    // Save/load settings to project.ini
    bool SaveSettings(const std::string& path);
    bool LoadSettings(const std::string& path);

private:
    // Sidebar categories
    struct SidebarItem {
        std::string label;
        int id;
        bool isHeader = false;
        bool isExpanded = true;
        std::vector<SidebarItem> children;
    };

    void BuildSidebar();
    void DrawSidebar(Slate::Context& ui, const Slate::Rect& rect);
    void DrawContent(Slate::Context& ui, const Slate::Rect& rect);

    // Content renderers for each category
    void DrawRenderingGeneral(Slate::Context& ui, const Slate::Rect& rect);
    void DrawRenderingShadows(Slate::Context& ui, const Slate::Rect& rect);
    void DrawRenderingPostProcess(Slate::Context& ui, const Slate::Rect& rect);
    void DrawRenderingQuality(Slate::Context& ui, const Slate::Rect& rect);
    void DrawProjectGeneral(Slate::Context& ui, const Slate::Rect& rect);
    void DrawEditorGeneral(Slate::Context& ui, const Slate::Rect& rect);

    bool m_isOpen = false;
    int m_selectedCategory = 0;
    float m_sidebarWidth = 220.0f;
    float m_scrollY = 0.0f;
    ProjectSettingsData m_settings;
    std::vector<SidebarItem> m_sidebar;
};

}  // namespace Luma::Editor::Panels
