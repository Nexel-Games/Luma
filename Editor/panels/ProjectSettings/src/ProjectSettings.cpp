#include "Luma/Editor/Panels/ProjectSettings.h"

#include <algorithm>
#include <cstdio>

#include "Luma/Slate/Icons.h"

namespace Luma::Editor::Panels {

namespace {
// Spin-box options: drag speed, hard limits, optional unit suffix.
Slate::SpinBoxOptions Num(f64 speed, std::optional<f64> lo, std::optional<f64> hi,
                          std::string_view suffix = {}) {
    Slate::SpinBoxOptions o;
    o.speed = speed;
    o.min = lo;
    o.max = hi;
    o.suffix = suffix;
    return o;
}
// 0..1 amount with a slider fill bar.
Slate::SpinBoxOptions Amount01() {
    Slate::SpinBoxOptions o = Num(0.005, 0.0, 1.0);
    o.sliderMin = 0.0;
    o.sliderMax = 1.0;
    return o;
}
}  // namespace

using Slate::Align;
using Slate::Color;
using Slate::Rect;
using Slate::Vec2;

// Category IDs
enum CategoryId {
    // Project
    CatProjectGeneral = 0,
    CatProjectMapsModes,

    // Rendering
    CatRenderingGeneral = 100,
    CatRenderingShadows,
    CatRenderingPostProcess,
    CatRenderingQuality,

    // Editor
    CatEditorGeneral = 200,

    // Platforms
    CatPlatformsWindows = 300,
};

void ProjectSettingsPanel::BuildSidebar() {
    m_sidebar.clear();

    // Project
    SidebarItem project{"Project", -1, true, true, {}};
    project.children.push_back({"General", CatProjectGeneral});
    project.children.push_back({"Maps & Modes", CatProjectMapsModes});
    m_sidebar.push_back(project);

    // Rendering
    SidebarItem rendering{"Rendering", -1, true, true, {}};
    rendering.children.push_back({"General", CatRenderingGeneral});
    rendering.children.push_back({"Shadows", CatRenderingShadows});
    rendering.children.push_back({"Post Processing", CatRenderingPostProcess});
    rendering.children.push_back({"Quality", CatRenderingQuality});
    m_sidebar.push_back(rendering);

    // Editor
    SidebarItem editor{"Editor", -1, true, true, {}};
    editor.children.push_back({"General", CatEditorGeneral});
    m_sidebar.push_back(editor);

    // Platforms
    SidebarItem platforms{"Platforms", -1, true, true, {}};
    platforms.children.push_back({"Windows", CatPlatformsWindows});
    m_sidebar.push_back(platforms);
}

bool ProjectSettingsPanel::Draw(Slate::Context& ui, f32 width, f32 height) {
    if (!m_isOpen) return false;

    Slate::Theme& t = ui.theme();

    // Build sidebar on first draw
    if (m_sidebar.empty()) BuildSidebar();

    // Modal-style overlay (dim background)
    ui.Panel({0, 0, width, height}, Color(0, 0, 0, 160));

    // Window rect (centered, with margin)
    f32 margin = 40.0f;
    f32 winW = width - margin * 2.0f;
    f32 winH = height - margin * 2.0f;
    f32 winX = margin;
    f32 winY = margin;
    Rect window{winX, winY, winW, winH};

    // Window background + border
    ui.PanelRounded(window, t.surface0, 8.0f);
    ui.PanelRoundedBordered(window, t.surface0, t.outline, 8.0f, 1.0f);

    // Title bar
    Rect titleBar{winX, winY, winW, 40.0f};
    ui.Panel(titleBar, t.surface2);
    ui.Panel({winX, winY + 39.0f, winW, 1.0f}, t.outline);

    // Title text
    ui.Heading({winX + 16.0f, winY + 8.0f, 300.0f, 24.0f},
               "Project Settings", t.text);

    // Close button (X)
    Rect closeBtn{winX + winW - 36.0f, winY + 8.0f, 24.0f, 24.0f};
    if (ui.IconTextButton(Slate::Context::ID("ps.close"), closeBtn, Slate::Icon::Close, {}, t.text)) {
        m_isOpen = false;
        return false;
    }

    // Content area (below title bar)
    Rect content{winX, winY + 40.0f, winW, winH - 40.0f};

    // Sidebar
    Rect sidebar{content.x, content.y, m_sidebarWidth, content.h};
    DrawSidebar(ui, sidebar);

    // Separator
    ui.Panel({sidebar.Right(), content.y, 1.0f, content.h}, t.outline);

    // Main content
    Rect mainContent{sidebar.Right() + 1.0f, content.y,
                     content.w - m_sidebarWidth - 1.0f, content.h};
    DrawContent(ui, mainContent);

    return true;
}

void ProjectSettingsPanel::DrawSidebar(Slate::Context& ui, const Slate::Rect& rect) {
    Slate::Theme& t = ui.theme();

    // Sidebar background
    ui.Panel(rect, t.surface1);

    f32 y = rect.y + 8.0f;
    f32 itemH = 28.0f;
    f32 headerH = 32.0f;
    f32 indent = 12.0f;

    for (auto& section : m_sidebar) {
        // Section header (clickable to expand/collapse)
        Rect headerRect{rect.x, y, rect.w, headerH};
        bool hovered = headerRect.Contains(ui.mouse());
        if (hovered) {
            ui.Panel(headerRect, t.surface2);
        }

        // Expand/collapse arrow
        Slate::DrawIcon(ui, {rect.x + 8.0f, y + 6.0f, 16.0f, 20.0f},
                        section.isExpanded ? Slate::Icon::ChevronDown : Slate::Icon::ChevronRight, t.textDim);
        ui.LabelIn({rect.x + 28.0f, y + 6.0f, rect.w - 36.0f, 20.0f},
                 section.label, t.text);

        if (hovered && ui.mousePressed(0)) {
            section.isExpanded = !section.isExpanded;
        }
        y += headerH;

        // Children
        if (section.isExpanded) {
            for (auto& child : section.children) {
                Rect itemRect{rect.x + indent, y, rect.w - indent, itemH};
                bool isSelected = (child.id == m_selectedCategory);
                bool isHovered = itemRect.Contains(ui.mouse());

                if (isSelected) {
                    ui.Panel(itemRect, t.accent);
                    ui.LabelIn({rect.x + indent + 8.0f, y + 4.0f,
                              rect.w - indent - 16.0f, 20.0f},
                             child.label, Color(255, 255, 255));
                } else if (isHovered) {
                    ui.Panel(itemRect, t.surface2);
                    ui.LabelIn({rect.x + indent + 8.0f, y + 4.0f,
                              rect.w - indent - 16.0f, 20.0f},
                             child.label, t.text);
                } else {
                    ui.LabelIn({rect.x + indent + 8.0f, y + 4.0f,
                              rect.w - indent - 16.0f, 20.0f},
                             child.label, t.textDim);
                }

                if (isHovered && ui.mousePressed(0)) {
                    m_selectedCategory = child.id;
                }
                y += itemH;
            }
        }
    }
}

void ProjectSettingsPanel::DrawContent(Slate::Context& ui, const Slate::Rect& rect) {
    Slate::Theme& t = ui.theme();

    // Content background
    ui.Panel(rect, t.surface0);

    // Clip to content area
    ui.PushClip(rect);

    f32 x = rect.x + 20.0f;
    f32 y = rect.y + 16.0f - m_scrollY;
    f32 w = rect.w - 40.0f;

    switch (m_selectedCategory) {
        case CatProjectGeneral: DrawProjectGeneral(ui, {x, y, w, rect.h}); break;
        case CatRenderingGeneral: DrawRenderingGeneral(ui, {x, y, w, rect.h}); break;
        case CatRenderingShadows: DrawRenderingShadows(ui, {x, y, w, rect.h}); break;
        case CatRenderingPostProcess: DrawRenderingPostProcess(ui, {x, y, w, rect.h}); break;
        case CatRenderingQuality: DrawRenderingQuality(ui, {x, y, w, rect.h}); break;
        case CatEditorGeneral: DrawEditorGeneral(ui, {x, y, w, rect.h}); break;
        default:
            ui.LabelIn({x, y, w, 24.0f}, "Select a category from the sidebar.",
                     t.textDim);
            break;
    }

    ui.PopClip();

    // Scrollbar (if content is taller than visible area)
    // For now, content fits within the window
}

// --- Category Content Renderers -----------------------------------------------

void ProjectSettingsPanel::DrawProjectGeneral(Slate::Context& ui, const Slate::Rect& rect) {
    Slate::Theme& t = ui.theme();
    f32 x = rect.x;
    f32 y = rect.y;
    f32 w = rect.w;

    // Section header
    ui.Heading({x, y, w, 28.0f}, "Project", t.accent);
    y += 36.0f;

    // Project Name
    ui.LabelIn({x, y, 140.0f, 22.0f}, "Project Name", t.textDim);
    ui.TextField(Slate::Context::ID("ps.projname"), {x + 140.0f, y, w - 140.0f, 24.0f},
                 m_settings.projectName);
    y += 36.0f;

    // Project Version
    ui.LabelIn({x, y, 140.0f, 22.0f}, "Version", t.textDim);
    ui.TextField(Slate::Context::ID("ps.projver"), {x + 140.0f, y, w - 140.0f, 24.0f},
                 m_settings.projectVersion);
    y += 36.0f;

    // Startup Scene
    ui.LabelIn({x, y, 140.0f, 22.0f}, "Startup Scene", t.textDim);
    ui.TextField(Slate::Context::ID("ps.startscene"), {x + 140.0f, y, w - 140.0f, 24.0f},
                 m_settings.startupScene);
    y += 48.0f;
}

void ProjectSettingsPanel::DrawRenderingGeneral(Slate::Context& ui, const Slate::Rect& rect) {
    Slate::Theme& t = ui.theme();
    auto& rs = m_settings.rendering;
    f32 x = rect.x;
    f32 y = rect.y;
    f32 w = rect.w;

    // Section header
    ui.Heading({x, y, w, 28.0f}, "Rendering - General", t.accent);
    y += 36.0f;

    // Anti-Aliasing
    ui.LabelIn({x, y, w, 20.0f}, "Anti-Aliasing", t.text);
    y += 26.0f;

    ui.Checkbox(Slate::Context::ID("ps.msaa"), {x + 16.0f, y, 18.0f, 18.0f},
                "MSAA", rs.msaaEnabled);
    y += 26.0f;

    if (rs.msaaEnabled) {
        ui.LabelIn({x + 32.0f, y, 120.0f, 22.0f}, "Samples", t.textDim);
        const char* samples[] = {"2x", "4x", "8x"};
        int sampleVals[] = {2, 4, 8};
        f32 bw = 60.0f;
        for (int i = 0; i < 3; ++i) {
            Rect r{x + 160.0f + bw * i, y, bw - 4.0f, 22.0f};
            bool sel = rs.msaaSamples == sampleVals[i];
            if (ui.Tab(Slate::Context::ID(samples[i]), r, samples[i], sel)) {
                rs.msaaSamples = sampleVals[i];
            }
        }
        y += 30.0f;
    }

    ui.Checkbox(Slate::Context::ID("ps.fxaa"), {x + 16.0f, y, 18.0f, 18.0f},
                "FXAA (Fast Approximate AA)", rs.fxaaEnabled);
    y += 36.0f;

    // Tone Mapping
    ui.LabelIn({x, y, w, 20.0f}, "Tone Mapping", t.text);
    y += 26.0f;

    ui.LabelIn({x + 16.0f, y, 120.0f, 22.0f}, "Mode", t.textDim);
    const char* modes[] = {"ACES", "Reinhard", "Linear"};
    f32 bw = 80.0f;
    for (int i = 0; i < 3; ++i) {
        Rect r{x + 160.0f + bw * i, y, bw - 4.0f, 22.0f};
        bool sel = rs.toneMappingMode == i;
        if (ui.Tab(Slate::Context::ID(modes[i]), r, modes[i], sel)) {
            rs.toneMappingMode = i;
        }
    }
    y += 30.0f;

    ui.LabelIn({x + 16.0f, y, 120.0f, 22.0f}, "Exposure", t.textDim);
    ui.SpinBox(Slate::Context::ID("ps.exposure"), {x + 160.0f, y, w - 160.0f, 22.0f},
               rs.exposure, Num(0.01, std::nullopt, std::nullopt, " EV"));
    y += 30.0f;

    ui.Checkbox(Slate::Context::ID("ps.autoexp"), {x + 16.0f, y, 18.0f, 18.0f},
                "Auto Exposure", rs.autoExposure);
    y += 36.0f;

    // Bloom
    ui.LabelIn({x, y, w, 20.0f}, "Bloom", t.text);
    y += 26.0f;

    ui.Checkbox(Slate::Context::ID("ps.bloom"), {x + 16.0f, y, 18.0f, 18.0f},
                "Enabled", rs.bloomEnabled);
    y += 26.0f;

    if (rs.bloomEnabled) {
        ui.LabelIn({x + 32.0f, y, 120.0f, 22.0f}, "Intensity", t.textDim);
        ui.SpinBox(Slate::Context::ID("ps.bloomint"), {x + 160.0f, y, w - 160.0f, 22.0f},
                   rs.bloomIntensity, Num(0.01, 0.0, std::nullopt));
        y += 26.0f;

        ui.LabelIn({x + 32.0f, y, 120.0f, 22.0f}, "Passes", t.textDim);
        ui.SpinBoxInt(Slate::Context::ID("ps.bloompasses"), {x + 160.0f, y, w - 160.0f, 22.0f},
                      rs.bloomPasses, Num(0.05, 3.0, 7.0));
        y += 30.0f;
    }
}


void ProjectSettingsPanel::DrawRenderingShadows(Slate::Context& ui, const Slate::Rect& rect) {
    Slate::Theme& t = ui.theme();
    auto& rs = m_settings.rendering;
    f32 x = rect.x;
    f32 y = rect.y;
    f32 w = rect.w;

    ui.Heading({x, y, w, 28.0f}, "Rendering - Shadows", t.accent);
    y += 36.0f;

    ui.Checkbox(Slate::Context::ID("ps.shadows"), {x, y, 18.0f, 18.0f},
                "Enable Shadows", rs.shadowsEnabled);
    y += 30.0f;

    if (rs.shadowsEnabled) {
        ui.LabelIn({x, y, 140.0f, 22.0f}, "Shadow Map Size", t.textDim);
        const char* sizes[] = {"512", "1024", "2048", "4096"};
        int sizeVals[] = {512, 1024, 2048, 4096};
        f32 bw = 70.0f;
        for (int i = 0; i < 4; ++i) {
            Rect r{x + 160.0f + bw * i, y, bw - 4.0f, 22.0f};
            bool sel = rs.shadowMapSize == sizeVals[i];
            if (ui.Tab(Slate::Context::ID(sizes[i]), r, sizes[i], sel)) {
                rs.shadowMapSize = sizeVals[i];
            }
        }
        y += 30.0f;

        ui.LabelIn({x, y, 140.0f, 22.0f}, "Cascade Count", t.textDim);
        ui.SpinBoxInt(Slate::Context::ID("ps.cascades"), {x + 160.0f, y, w - 160.0f, 22.0f},
                      rs.cascadeCount, Num(0.05, 1.0, 4.0));
        y += 28.0f;

        ui.LabelIn({x, y, 140.0f, 22.0f}, "Shadow Distance", t.textDim);
        ui.SpinBox(Slate::Context::ID("ps.shadist"), {x + 160.0f, y, w - 160.0f, 22.0f},
                   rs.shadowDistance, Num(0.5, 1.0, std::nullopt, " m"));
        y += 28.0f;

        ui.LabelIn({x, y, 140.0f, 22.0f}, "Shadow Softness", t.textDim);
        ui.SpinBox(Slate::Context::ID("ps.shadsoft"), {x + 160.0f, y, w - 160.0f, 22.0f},
                   rs.shadowSoftness, Num(0.01, 0.0, std::nullopt));
        y += 28.0f;
    }
}

void ProjectSettingsPanel::DrawRenderingPostProcess(Slate::Context& ui, const Slate::Rect& rect) {
    Slate::Theme& t = ui.theme();
    auto& rs = m_settings.rendering;
    f32 x = rect.x;
    f32 y = rect.y;
    f32 w = rect.w;

    ui.Heading({x, y, w, 28.0f}, "Rendering - Post Processing", t.accent);
    y += 36.0f;

    ui.Checkbox(Slate::Context::ID("ps.ssao"), {x, y, 18.0f, 18.0f},
                "Screen Space Ambient Occlusion", rs.ssaoEnabled);
    y += 26.0f;

    if (rs.ssaoEnabled) {
        ui.LabelIn({x + 16.0f, y, 120.0f, 22.0f}, "Intensity", t.textDim);
        ui.SpinBox(Slate::Context::ID("ps.ssao.int"), {x + 160.0f, y, w - 160.0f, 22.0f},
                   rs.ssaoIntensity, Num(0.01, 0.0, 4.0));
        y += 28.0f;
    }

    ui.Checkbox(Slate::Context::ID("ps.ssr"), {x, y, 18.0f, 18.0f},
                "Screen Space Reflections", rs.screenSpaceReflections);
    y += 30.0f;

    ui.Checkbox(Slate::Context::ID("ps.motionblur"), {x, y, 18.0f, 18.0f},
                "Motion Blur", rs.motionBlur);
    y += 26.0f;

    if (rs.motionBlur) {
        ui.LabelIn({x + 16.0f, y, 120.0f, 22.0f}, "Amount", t.textDim);
        ui.SpinBox(Slate::Context::ID("ps.mb.amount"), {x + 160.0f, y, w - 160.0f, 22.0f},
                   rs.motionBlurAmount, Amount01());
        y += 28.0f;
    }
}

void ProjectSettingsPanel::DrawRenderingQuality(Slate::Context& ui, const Slate::Rect& rect) {
    Slate::Theme& t = ui.theme();
    f32 x = rect.x;
    f32 y = rect.y;
    f32 w = rect.w;

    ui.Heading({x, y, w, 28.0f}, "Rendering - Quality", t.accent);
    y += 36.0f;

    ui.LabelIn({x, y, 140.0f, 22.0f}, "Quality Level", t.textDim);
    const char* levels[] = {"Low", "Medium", "High", "Epic", "Cinematic"};
    f32 bw = 80.0f;
    for (int i = 0; i < 5; ++i) {
        Rect r{x + 160.0f + bw * i, y, bw - 4.0f, 22.0f};
        if (ui.Tab(Slate::Context::ID(levels[i]), r, levels[i], false)) {
            // TODO: Apply quality preset
        }
    }
    y += 36.0f;

    ui.LabelIn({x, y, w, 20.0f}, "These settings are presets that configure the individual\n"
              "rendering settings on the other tabs.", t.textDim);
}

void ProjectSettingsPanel::DrawEditorGeneral(Slate::Context& ui, const Slate::Rect& rect) {
    Slate::Theme& t = ui.theme();
    f32 x = rect.x;
    f32 y = rect.y;
    f32 w = rect.w;

    ui.Heading({x, y, w, 28.0f}, "Editor - General", t.accent);
    y += 36.0f;

    ui.LabelIn({x, y, w, 20.0f}, "Editor settings will be added here.", t.textDim);
}

// --- Save/Load ----------------------------------------------------------------

bool ProjectSettingsPanel::SaveSettings(const std::string& path) {
    // TODO: Serialize to INI/JSON
    (void)path;
    return true;
}

bool ProjectSettingsPanel::LoadSettings(const std::string& path) {
    // TODO: Deserialize from INI/JSON
    (void)path;
    return true;
}

}  // namespace Luma::Editor::Panels
