#include "Luma/Editor/Panels/ProjectSettings.h"

#include <algorithm>

#include "Luma/Core/Log.h"

#include "Luma/Slate/Icons.h"

namespace Luma::Editor::Panels {

using Slate::Rect;

ProjectSettingsPanel::ProjectSettingsPanel(const SettingsPageRegistry& pages) : m_pages(pages) {}

void ProjectSettingsPanel::EnsureSelection() {
    if (!m_selected.empty() && m_pages.Find(m_selected)) return;
    m_selected.clear();
    for (const std::string& category : m_pages.Categories()) {
        const auto pages = m_pages.PagesIn(category);
        if (!pages.empty()) {
            m_selected = pages.front()->id;
            return;
        }
    }
}

void ProjectSettingsPanel::Open() {
    m_isOpen = true;
    m_status.clear();
    m_statusError = false;
    if (m_store) m_store->LoadNewPages();
    EnsureSelection();
}

void ProjectSettingsPanel::SaveNow() {
    if (!m_store) return;
    std::vector<std::string> errors;
    if (m_store->SaveAll(&errors)) {
        m_status = "Saved to " + m_store->ConfigDir().string();
        m_statusError = false;
    } else {
        m_status = errors.empty() ? std::string("Some settings could not be saved") : errors.front();
        m_statusError = true;
    }
}

void ProjectSettingsPanel::Close() {
    if (m_store && m_store->AnyDirty()) SaveNow();
    if (!m_statusError) m_isOpen = false;
}

void ProjectSettingsPanel::DrawSidebar(Slate::Context& ui, const Rect& rect) {
    Slate::Theme& t = ui.theme();
    // Editor Preferences style: a large group title per category, then the
    // pages as selectable items with an accent bar on the selected one.
    ui.Panel(rect, t.surface1);
    f32 y = rect.y + 15.0f;
    for (const std::string& category : m_pages.Categories()) {
        ui.LabelIn({rect.x + 12.0f, y, rect.w - 20.0f, 36.0f}, category, t.text, Slate::Align::Left, true);
        y += 44.0f;
        for (const SettingsPageDesc* page : m_pages.PagesIn(category)) {
            const Rect item{rect.x + 15.0f, y, rect.w - 27.0f, 30.0f};
            const bool selected = page->id == m_selected;
            std::string label = page->title;
            if (m_store && m_store->IsDirty(page->id)) label += " *";
            const u64 itemId = Slate::Context::ID("ps.page." + page->id);
            if (ui.Selectable(itemId, item, label, selected)) {
                m_selected = page->id;
                m_scroll = 0.0f;
            }
            if (selected) ui.Panel({item.x, item.y + 5.0f, 2.0f, item.h - 10.0f}, t.accent);
            if (!page->tooltip.empty()) ui.Tooltip(itemId ^ 0x7177ull, item, page->title + "\n" + page->tooltip);
            y += 32.0f;
        }
        y += 12.0f;
    }
}

void ProjectSettingsPanel::Draw(Slate::Context& ui, f32 width, f32 height) {
    if (!m_isOpen) return;
    EnsureSelection();
    Slate::Theme& t = ui.theme();
    const u64 id = Slate::Context::ID("ps.window");
    ui.Panel({0, 0, width, height}, t.surface0);
    ui.Panel({0, 0, width, 38}, t.tabWell);
    ui.LogoMark({20, 19}, 12);
    ui.PanelRounded({40, 4, 200, 34}, t.surface2, t.radius.sm);
    Slate::DrawIcon(ui, {52, 12, 18, 18}, Slate::Icon::Gear, t.textDim);
    ui.Heading({76, 4, 160, 34}, "Project Settings", t.text);
    ui.Panel({0, 38, width, 1}, t.separator);
    const Rect content{0, 39, width, std::max(0.0f, height - 82)};
    const f32 sidebarW = std::min(width * 0.33f, 232.0f);
    const Rect sidebar{content.x, content.y, sidebarW, content.h};
    DrawSidebar(ui, sidebar);
    ui.Panel({sidebar.Right(), content.y, 1.0f, content.h}, t.separator);
    const Rect pageRect{sidebar.Right() + 22.0f, content.y, content.w - sidebarW - 44.0f,
                        content.h};

    if (const SettingsPageDesc* page = m_pages.Find(m_selected)) {
        // Title + subtitle, as in Editor Preferences.
        ui.LabelIn({pageRect.x, pageRect.y + 15.0f, pageRect.w, 40.0f}, page->category + " - " + page->title, t.text,
                   Slate::Align::Left, true);
        ui.LabelIn({pageRect.x, pageRect.y + 55.0f, pageRect.w, 24.0f},
                   "Project settings, saved to Config/" + page->configFile, t.textDim);
        const Rect body{pageRect.x, pageRect.y + 89.0f, pageRect.w, std::max(0.0f, pageRect.h - 101.0f)};
        m_scroll = ui.VerticalScroll(id ^ 0x51, body, page->contentHeight, m_scroll);
        ui.PushClip(body);
        const bool changed = page->draw(ui, {body.x, body.y - m_scroll, std::max(1.0f, body.w - 12), body.h});
        ui.PopClip();
        if (changed && m_store) m_store->MarkDirty(page->id);
    } else {
        ui.LabelIn({pageRect.x, pageRect.y, pageRect.w, 22.0f},
                   m_store ? "No settings pages are registered."
                           : "Open a project to edit its settings.",
                   t.textDim);
    }

    ui.Panel({0, content.Bottom(), width, 1}, t.separator);
    const f32 by = content.Bottom() + t.space.md;
    const Rect closeRect{content.Right() - 90.0f, by, 90.0f, 26.0f};
    const Rect saveRect{closeRect.x - 98.0f, by, 90.0f, 26.0f};
    if (!m_status.empty()) {
        ui.LabelIn({content.x, by, saveRect.x - content.x - 8.0f, 26.0f}, m_status,
                   m_statusError ? t.error : t.textDim);
    }
    const bool dirty = m_store && m_store->AnyDirty();
    if (ui.Button(id ^ 0x5A7Eull, saveRect, dirty ? "Save *" : "Save") && dirty) SaveNow();
    if (ui.Button(id ^ 0xC105Eull, closeRect, "Close") ||
        (ui.keyEscape() && !ui.AnyMenuOpen() && !ui.textFieldFocused())) {
        Close();
    }
}

// --- Maps & Modes -----------------------------------------------------------------

std::filesystem::path EditorStartupScenePath(const Project& project,
                                             const MapsAndModesSettings& settings) {
    if (!settings.editorStartupScene.empty()) {
        const std::filesystem::path candidate = project.RootDir() / settings.editorStartupScene;
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec)) return candidate;
        LUMA_USER_WARN("ProjectSettings",
                       "Editor startup map '{}' does not exist; opening the project's startup "
                       "scene instead",
                       settings.editorStartupScene);
    }
    return project.StartupScenePath();
}

RegistrationToken RegisterMapsAndModesPage(SettingsPageRegistry& pages,
                                           MapsAndModesSettings& settings,
                                           std::function<std::vector<std::string>()> listScenes) {
    SettingsPageDesc page;
    page.id = std::string(kMapsAndModesPageId);
    page.category = "Project";
    page.title = "Maps & Modes";
    page.tooltip = "The scene the editor opens with this project.";
    page.owner = "Engine";
    page.configFile = "Editor.json";
    MapsAndModesSettings* s = &settings;
    page.load = [s](const SerialValue& section) {
        *s = MapsAndModesSettings{};
        if (const SerialValue* v = section.IsObject() ? section.Find("editorStartupScene")
                                                      : nullptr) {
            s->editorStartupScene = v->AsString();
        }
    };
    page.save = [s] {
        SerialValue section = SerialValue::MakeObject();
        section["editorStartupScene"] = s->editorStartupScene;
        return section;
    };
    page.draw = [s, listScenes = std::move(listScenes)](Slate::Context& ui, const Rect& r) {
        Slate::Theme& t = ui.theme();
        ui.LabelIn({r.x, r.y, 180.0f, 22.0f}, "Editor Startup Map", t.textDim);
        std::vector<std::string> items = {"(Project startup scene)"};
        const std::vector<std::string> scenes = listScenes ? listScenes() : std::vector<std::string>{};
        items.insert(items.end(), scenes.begin(), scenes.end());
        int current = 0;
        for (usize i = 0; i < scenes.size(); ++i) {
            if (scenes[i] == s->editorStartupScene) current = static_cast<int>(i) + 1;
        }
        if (current == 0 && !s->editorStartupScene.empty()) {
            items.push_back(s->editorStartupScene + " (missing)");
            current = static_cast<int>(items.size()) - 1;
        }
        const int picked = ui.Combo(Slate::Context::ID("ps.maps.editorstartup"),
                                    {r.x + 190.0f, r.y, std::max(r.w - 190.0f, 120.0f), 22.0f},
                                    items, current);
        ui.LabelIn({r.x, r.y + 30.0f, r.w, 20.0f},
                   "The scene opened when this project is loaded in the editor.", t.textDim);
        if (picked == current) return false;
        if (picked == 0) {
            s->editorStartupScene.clear();
        } else if (picked <= static_cast<int>(scenes.size())) {
            s->editorStartupScene = scenes[static_cast<usize>(picked) - 1];
        } else {
            return false;  // the "(missing)" entry itself
        }
        return true;
    };
    return pages.Register(std::move(page));
}

}  // namespace Luma::Editor::Panels
