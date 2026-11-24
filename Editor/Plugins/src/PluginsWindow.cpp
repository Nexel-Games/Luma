#include "Luma/EditorPlugins/PluginsWindow.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <optional>
#include <set>

#include "Luma/Core/Version.h"
#include "Luma/Platform/Process.h"
#include "Luma/Plugins/PluginDescriptor.h"
#include "Luma/Slate/Theme.h"

namespace Luma::EditorPlugins {
namespace {

using Plugins::PluginManager;
using Plugins::PluginOrigin;
using Plugins::PluginState;
using Plugins::PluginStatus;
using Slate::Color;
using Slate::Rect;

std::string Join(const std::vector<std::string>& parts, const char* sep) {
    std::string out;
    for (usize i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

bool TreeSelected(std::string_view key, std::string_view selected) {
    return key == selected;
}

}  // namespace

std::string LowerAscii(std::string_view text) {
    std::string out(text);
    for (char& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

bool PluginMatchesQuery(const PluginStatus& status, std::string_view query) {
    if (query.empty()) return true;
    const std::string q = LowerAscii(query);
    auto has = [&](std::string_view field) { return LowerAscii(field).find(q) != std::string::npos; };
    if (has(status.manifest.id) || has(status.manifest.displayName) ||
        has(status.descriptor.friendlyName) || has(status.descriptor.description) ||
        has(status.descriptor.createdBy) || has(status.manifest.author) ||
        has(status.manifest.description) || has(status.descriptor.category)) {
        return true;
    }
    for (const auto& mod : status.descriptor.modules) {
        if (has(mod.name)) return true;
    }
    return false;
}

std::string PluginStatusLine(const PluginStatus& status) {
    auto reason = [&] {
        return status.diagnostics.empty() ? std::string{} : status.diagnostics.front();
    };
    if (status.state == PluginState::NotInstalled) return "Not installed";
    if (status.state == PluginState::Failed) {
        const std::string r = reason();
        return r.empty() ? "Failed" : ("Failed — " + r);
    }
    if (status.state == PluginState::Unavailable) {
        const std::string r = reason();
        return r.empty() ? "Unavailable" : ("Unavailable — " + r);
    }
    if (!status.enabledInProject) return "Disabled";
    if (status.state == PluginState::Started) return "Active";
    if (status.restartRequired) return "Restart required";
    return "Active";
}

std::string PluginCategory(const PluginStatus& status) {
    const std::string& cat = status.descriptor.category.empty() ? status.manifest.category
                                                                : status.descriptor.category;
    if (cat.empty()) return "Other";
    return cat;
}

bool PluginsWindow::TakeRestartNow() {
    const bool go = m_restartNow;
    m_restartNow = false;
    return go;
}

void PluginsWindow::SetFolders(std::filesystem::path templateRoot,
                               std::filesystem::path enginePlugins,
                               std::filesystem::path projectPlugins) {
    m_templateRoot = std::move(templateRoot);
    m_enginePlugins = std::move(enginePlugins);
    m_projectPlugins = std::move(projectPlugins);
}

void PluginsWindow::Rediscover(PluginManager& manager, Project* project) {
    if (!manager.SearchRoots().empty()) {
        const auto roots = manager.SearchRoots();
        manager.Discover(roots);
        manager.ApplyProject(project);
    }
}

void PluginsWindow::Draw(Slate::Context& ui, const Rect& area, PluginManager& manager,
                         Project* project, const Scene* openScene) {
    Slate::Theme& t = ui.theme();
    const bool writable = project && project->IsWritable();
    f32 y = area.y;

    if (!writable) {
        const char* reason = project ? "The project file is read-only. Make it writable to "
                                       "change which plugins are enabled."
                                     : "Open a project to change plugins.";
        ui.LabelIn({area.x + 8.0f, y, area.w - 16.0f, 32.0f}, reason, t.warning);
        y += 36.0f;
    }

    const Rect search{area.x + 8.0f, y, area.w - 96.0f, 26.0f};
    const Rect addBtn{search.Right() + 8.0f, y, 72.0f, 26.0f};
    ui.SearchBox(Slate::Context::ID("plugins.browser.search"), search, m_search, "Search plugins");
    if (ui.Button(Slate::Context::ID("plugins.browser.add"), addBtn, "Add")) {
        m_wizardOpen = true;
        m_wizardName.clear();
        m_wizardFriendly.clear();
        m_wizardDescription.clear();
        if (m_wizardCategory.empty()) m_wizardCategory = "Other";
        m_wizardLocation = m_projectPlugins.empty() ? 1 : 0;
    }
    ui.Tooltip(addBtn, "New Plugin wizard");
    y += 34.0f;

    if (m_pending) {
        const auto& plan = *m_pending;
        std::string title = plan.enable ? "Enable plugin" : "Disable plugin";
        const Rect content = ui.BeginModal(Slate::Context::ID("plugins.browser.confirm"), title,
                                           {520.0f, 220.0f});
        f32 my = content.y;
        std::string body = plan.enable ? "Enable " + plan.targetId : "Disable " + plan.targetId;
        if (plan.enable && plan.experimental) {
            body += ". This plugin is Experimental.";
        } else if (plan.enable && plan.beta) {
            body += ". This plugin is Beta.";
        }
        if (plan.enable && !plan.alsoEnable.empty()) {
            body += " Required dependencies that will also be enabled: " +
                    Join(plan.alsoEnable, ", ") + ".";
        }
        if (!plan.enable && !plan.dependents.empty()) {
            body += ". Enabled plugins that require it: " + Join(plan.dependents, ", ") + ".";
        }
        ui.LabelIn({content.x, my, content.w, 72.0f}, body, t.text);
        my += 76.0f;
        if (!plan.enable && !plan.affectedComponents.empty()) {
            ui.LabelIn({content.x, my, content.w, 40.0f},
                       "Open scene uses: " + Join(plan.affectedComponents, "; "), t.warning);
        }
        const char* okLabel = plan.enable ? "Enable All" : "Disable";
        auto result = ui.ModalButtonRow(Slate::Context::ID("plugins.browser.confirm"), content,
                                        okLabel, "Cancel");
        if (result.cancel) m_pending.reset();
        if (result.ok && project) {
            std::string error;
            if (manager.Commit(*project, plan, &error)) {
                m_message.clear();
            } else {
                m_message = error;
                m_messageError = true;
            }
            m_pending.reset();
        }
        ui.EndModal();
    }

    if (m_wizardOpen) DrawWizard(ui, manager, project);
    if (m_editOpen) DrawDescriptorEditor(ui, manager, project);

    if (!m_message.empty()) {
        ui.LabelIn({area.x + 8.0f, y, area.w - 16.0f, 22.0f}, m_message,
                   m_messageError ? t.error : t.textDim);
        y += 26.0f;
    }

    const f32 bannerH = manager.RestartRequired() ? 36.0f : 0.0f;
    const Rect body{area.x, y, area.w, area.Bottom() - y - bannerH};
    const f32 treeW = std::min(220.0f, std::max(140.0f, body.w * 0.28f));
    const Rect tree{body.x, body.y, treeW, body.h};
    const Rect tiles{tree.Right() + 8.0f, body.y, body.w - treeW - 8.0f, body.h};
    ui.Panel(tree, t.recessed);

    std::vector<PluginStatus> all = manager.Statuses();
    std::vector<const PluginStatus*> visible;
    for (const PluginStatus& s : all) {
        if (PluginMatchesQuery(s, m_search)) visible.push_back(&s);
    }

    auto countRoot = [&](PluginOrigin origin) {
        int n = 0;
        for (const PluginStatus* s : visible) {
            if (s->origin == origin) ++n;
        }
        return n;
    };
    std::map<std::string, int> builtInCats;
    std::map<std::string, int> projectCats;
    for (const PluginStatus* s : visible) {
        if (s->origin == PluginOrigin::BuiltIn) builtInCats[PluginCategory(*s)]++;
        else projectCats[PluginCategory(*s)]++;
    }

    auto drawTreeRow = [&](f32& rowY, const std::string& key, const std::string& label, f32 indent) {
        const Rect row{tree.x + 4.0f + indent, rowY, tree.w - 8.0f - indent, 20.0f};
        if (TreeSelected(key, m_treeKey)) ui.PanelRounded(row, t.selectionBg, t.radius.sm);
        else if (row.Contains(ui.mouse())) ui.PanelRounded(row, t.surface2, t.radius.sm);
        ui.LabelIn(row, label, TreeSelected(key, m_treeKey) ? t.selectionText : t.text);
        if (row.Contains(ui.mouse()) && ui.mousePressed(0)) m_treeKey = key;
        rowY += 22.0f;
    };

    f32 treeY = tree.y + 6.0f;
    drawTreeRow(treeY, "builtin",
                "Built-In (" + std::to_string(countRoot(PluginOrigin::BuiltIn)) + ")", 0.0f);
    for (const auto& [cat, n] : builtInCats) {
        drawTreeRow(treeY, "builtin/" + cat, cat + " (" + std::to_string(n) + ")", 14.0f);
    }
    drawTreeRow(treeY, "project",
                "Project (" + std::to_string(countRoot(PluginOrigin::Project)) + ")", 0.0f);
    for (const auto& [cat, n] : projectCats) {
        drawTreeRow(treeY, "project/" + cat, cat + " (" + std::to_string(n) + ")", 14.0f);
    }

    auto show = [&](const PluginStatus& s) {
        if (m_treeKey == "builtin") return s.origin == PluginOrigin::BuiltIn;
        if (m_treeKey == "project") return s.origin == PluginOrigin::Project;
        if (m_treeKey.rfind("builtin/", 0) == 0) {
            return s.origin == PluginOrigin::BuiltIn &&
                   PluginCategory(s) == m_treeKey.substr(8);
        }
        if (m_treeKey.rfind("project/", 0) == 0) {
            return s.origin == PluginOrigin::Project &&
                   PluginCategory(s) == m_treeKey.substr(8);
        }
        return true;
    };

    f32 tileY = tiles.y + 4.0f;
    for (const PluginStatus* s : visible) {
        if (!show(*s)) continue;
        const Rect tile{tiles.x, tileY, tiles.w - 8.0f, 92.0f};
        ui.PanelRounded(tile, t.surface2, t.radius.md);
        const std::string title = s->descriptor.friendlyName.empty() ? s->manifest.displayName
                                                                     : s->descriptor.friendlyName;
        const std::string version = s->descriptor.versionName.ToString().empty()
                                        ? s->manifest.version.ToString()
                                        : s->descriptor.versionName.ToString();
        ui.LabelIn({tile.x + 36.0f, tile.y + 6.0f, tile.w - 140.0f, 18.0f}, title, t.text);
        ui.LabelIn({tile.x + 36.0f, tile.y + 24.0f, 80.0f, 16.0f}, version, t.textDim);
        f32 badgeX = tile.x + 120.0f;
        if (s->descriptor.isBetaVersion) {
            ui.LabelIn({badgeX, tile.y + 24.0f, 40.0f, 16.0f}, "Beta", t.warning);
            badgeX += 44.0f;
        }
        if (s->descriptor.isExperimentalVersion) {
            ui.LabelIn({badgeX, tile.y + 24.0f, 90.0f, 16.0f}, "Experimental", t.warning);
        }
        const std::string desc = s->descriptor.description.empty() ? s->manifest.description
                                                                   : s->descriptor.description;
        ui.LabelIn({tile.x + 36.0f, tile.y + 42.0f, tile.w - 48.0f, 16.0f}, desc, t.textDim);
        const std::string author = s->descriptor.createdBy.empty() ? s->manifest.author
                                                                   : s->descriptor.createdBy;
        const Rect authorR{tile.x + 36.0f, tile.y + 60.0f, 180.0f, 16.0f};
        const std::string by = author.empty() ? std::string{} : ("Created by " + author);
        if (!by.empty()) ui.LabelIn(authorR, by, t.textDim);
        const std::string url = s->descriptor.createdByUrl.empty() ? s->manifest.url
                                                                   : s->descriptor.createdByUrl;
        if (!url.empty() && authorR.Contains(ui.mouse()) && ui.mousePressed(0)) {
            if (openUrl) openUrl(url);
            else OpenExternal(url);
        }
        ui.LabelIn({tile.x + 36.0f, tile.y + 74.0f, tile.w - 48.0f, 14.0f}, PluginStatusLine(*s),
                   (s->state == PluginState::Failed || s->state == PluginState::Unavailable ||
                    s->state == PluginState::NotInstalled)
                       ? t.error
                       : (s->restartRequired ? t.warning : t.textDim));

        bool enabled = s->enabledInProject;
        const Rect check{tile.x + 8.0f, tile.y + 10.0f, 22.0f, 22.0f};
        if (writable && s->state != PluginState::NotInstalled) {
            if (ui.Checkbox(Slate::Context::ID("plugins.browser.enable." + s->manifest.id), check,
                            "", enabled) &&
                enabled != s->enabledInProject) {
                m_pending = enabled ? manager.PlanEnable(s->manifest.id)
                                    : manager.PlanDisable(s->manifest.id, openScene);
                if (!m_pending->blockedBy.empty()) {
                    m_message = m_pending->blockedBy.front();
                    m_messageError = true;
                    m_pending.reset();
                }
            }
        }
        if (s->origin == PluginOrigin::Project) {
            const Rect edit{tile.Right() - 72.0f, tile.y + 8.0f, 64.0f, 22.0f};
            if (ui.Button(Slate::Context::ID("plugins.browser.edit." + s->manifest.id), edit,
                          "Edit...")) {
                m_edit = s->descriptor;
                m_editVersionName = m_edit.versionName.ToString();
                m_editIconPath.clear();
                m_editOpen = true;
            }
        }
        tileY += 100.0f;
    }

    if (bannerH > 0.0f) {
        const Rect banner{area.x, area.Bottom() - bannerH, area.w, bannerH};
        ui.Panel(banner, t.surface1);
        ui.LabelIn({banner.x + 8.0f, banner.y, banner.w - 130.0f, banner.h},
                   "Restart required to apply plugin changes", t.warning);
        const Rect now{banner.Right() - 118.0f, banner.y + 6.0f, 110.0f, 24.0f};
        if (ui.Button(Slate::Context::ID("plugins.browser.restart"), now, "Restart Now")) {
            m_restartNow = true;
        }
    }
}

void PluginsWindow::DrawWizard(Slate::Context& ui, PluginManager& manager, Project* project) {
    Slate::Theme& t = ui.theme();
    const Rect content =
        ui.BeginModal(Slate::Context::ID("plugins.wizard"), "New Plugin", {640.0f, 460.0f});
    f32 y = content.y;
    ui.LabelIn({content.x, y, content.w, 18.0f}, "Template", t.textDim);
    y += 20.0f;
    const auto templates = Plugins::AllPluginTemplates();
    std::vector<std::string> names;
    names.reserve(templates.size());
    for (auto tmpl : templates) names.emplace_back(Plugins::TemplateDisplayName(tmpl));
    m_wizardTemplate = ui.Combo(Slate::Context::ID("plugins.wizard.template"),
                                {content.x, y, content.w, 24.0f}, names, m_wizardTemplate);
    y += 28.0f;
    ui.LabelIn({content.x, y, content.w, 32.0f},
               Plugins::TemplateDescription(templates[static_cast<usize>(m_wizardTemplate)]),
               t.textDim);
    y += 36.0f;

    auto labeled = [&](const char* label, u64 id, std::string& value, const char* placeholder) {
        ui.LabelIn({content.x, y, 110.0f, 22.0f}, label, t.textDim);
        ui.TextField(id, {content.x + 114.0f, y, content.w - 114.0f, 22.0f}, value, placeholder);
        y += 26.0f;
    };
    labeled("Name", Slate::Context::ID("plugins.wizard.name"), m_wizardName, "MyPlugin");
    labeled("Friendly", Slate::Context::ID("plugins.wizard.friendly"), m_wizardFriendly,
            "My Plugin");
    labeled("Author", Slate::Context::ID("plugins.wizard.author"), m_wizardAuthor, "Your name");
    labeled("Category", Slate::Context::ID("plugins.wizard.category"), m_wizardCategory, "Other");
    labeled("Description", Slate::Context::ID("plugins.wizard.desc"), m_wizardDescription, "");

    ui.LabelIn({content.x, y, 110.0f, 22.0f}, "Location", t.textDim);
    std::vector<std::string> locations;
    if (!m_projectPlugins.empty()) locations.emplace_back("Project Plugins");
    locations.emplace_back("Engine Plugins");
    if (m_projectPlugins.empty()) m_wizardLocation = 0;
    m_wizardLocation = ui.Combo(Slate::Context::ID("plugins.wizard.location"),
                                {content.x + 114.0f, y, content.w - 114.0f, 22.0f}, locations,
                                m_wizardLocation);
    y += 28.0f;
    ui.LabelIn({content.x, y, content.w, 36.0f},
               "This editor does not compile C++. After Create, rebuild, then restart to load modules.",
               t.textDim);

    auto result = ui.ModalButtonRow(Slate::Context::ID("plugins.wizard"), content, "Create",
                                    "Cancel");
    if (result.cancel) m_wizardOpen = false;
    if (result.ok) {
        Plugins::PluginWizardRequest req;
        req.tmpl = templates[static_cast<usize>(m_wizardTemplate)];
        req.name = m_wizardName;
        req.friendlyName = m_wizardFriendly;
        req.author = m_wizardAuthor;
        req.description = m_wizardDescription;
        req.category = m_wizardCategory;
        req.templateRoot = m_templateRoot;
        const bool useProject = !m_projectPlugins.empty() && m_wizardLocation == 0;
        req.location = useProject ? m_projectPlugins : m_enginePlugins;
        Plugins::PluginWizardResult generated;
        std::string error;
        if (!Plugins::GeneratePlugin(req, &generated, &error)) {
            m_message = error;
            m_messageError = true;
        } else {
            Rediscover(manager, project);
            m_message = "Created " + req.name + " in " + generated.pluginDir.string() +
                        (generated.hasModules
                             ? ". Rebuild the editor to compile C++, then restart to load it."
                             : ". Enable it and restart to mount its content.");
            m_messageError = false;
            m_wizardOpen = false;
        }
    }
    ui.EndModal();
}

void PluginsWindow::DrawDescriptorEditor(Slate::Context& ui, PluginManager& manager,
                                         Project* project) {
    Slate::Theme& t = ui.theme();
    const Rect content = ui.BeginModal(Slate::Context::ID("plugins.descriptor"),
                                       "Edit plugin descriptor", {560.0f, 440.0f});
    f32 y = content.y;
    auto labeled = [&](const char* label, u64 id, std::string& value) {
        ui.LabelIn({content.x, y, 130.0f, 22.0f}, label, t.textDim);
        ui.TextField(id, {content.x + 134.0f, y, content.w - 134.0f, 22.0f}, value);
        y += 26.0f;
    };
    labeled("Friendly name", Slate::Context::ID("plugins.edit.friendly"), m_edit.friendlyName);
    labeled("Description", Slate::Context::ID("plugins.edit.desc"), m_edit.description);
    labeled("Category", Slate::Context::ID("plugins.edit.category"), m_edit.category);
    labeled("Created by", Slate::Context::ID("plugins.edit.author"), m_edit.createdBy);
    labeled("Created by URL", Slate::Context::ID("plugins.edit.authorUrl"), m_edit.createdByUrl);
    labeled("Docs URL", Slate::Context::ID("plugins.edit.docs"), m_edit.docsUrl);
    labeled("Support URL", Slate::Context::ID("plugins.edit.support"), m_edit.supportUrl);
    labeled("Version name", Slate::Context::ID("plugins.edit.version"), m_editVersionName);
    labeled("Engine version", Slate::Context::ID("plugins.edit.engine"), m_edit.engineVersionRange);
    labeled("Icon PNG", Slate::Context::ID("plugins.edit.icon"), m_editIconPath);

    ui.Checkbox(Slate::Context::ID("plugins.edit.beta"), {content.x, y, content.w, 20.0f},
                "Beta", m_edit.isBetaVersion);
    y += 22.0f;
    ui.Checkbox(Slate::Context::ID("plugins.edit.exp"), {content.x, y, content.w, 20.0f},
                "Experimental", m_edit.isExperimentalVersion);
    y += 22.0f;
    ui.Checkbox(Slate::Context::ID("plugins.edit.content"), {content.x, y, content.w, 20.0f},
                "Can contain content", m_edit.canContainContent);

    auto result = ui.ModalButtonRow(Slate::Context::ID("plugins.edit"), content, "Save", "Cancel");
    if (result.cancel) m_editOpen = false;
    if (result.ok) {
        std::string error;
        if (auto parsed = Luma::SemVer::Parse(m_editVersionName)) {
            m_edit.versionName = *parsed;
        } else {
            m_message = "Version name \"" + m_editVersionName + "\" is not a valid semantic version.";
            m_messageError = true;
            ui.EndModal();
            return;
        }
        if (m_edit.descriptorPath.empty()) {
            m_message = "This plugin has no descriptor path to save.";
            m_messageError = true;
        } else if (!Plugins::WriteDescriptorFile(m_edit, m_edit.descriptorPath, &error)) {
            m_message = error;
            m_messageError = true;
        } else {
            if (!m_editIconPath.empty()) {
                const auto dest = m_edit.descriptorPath.parent_path() / "Resources" / "Icon128.png";
                std::error_code ec;
                std::filesystem::create_directories(dest.parent_path(), ec);
                std::filesystem::copy_file(m_editIconPath, dest,
                                           std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) {
                    m_message = "Saved the descriptor but could not copy the icon: " + ec.message();
                    m_messageError = true;
                    Rediscover(manager, project);
                    m_editOpen = false;
                    ui.EndModal();
                    return;
                }
            }
            Rediscover(manager, project);
            m_message = "Saved " + m_edit.descriptorPath.filename().string() +
                        ". Unknown descriptor fields were kept.";
            m_messageError = false;
            m_editOpen = false;
        }
    }
    ui.EndModal();
}

void RegisterPluginsWindow(RegistrationSet& registrations, Editor::MenuRegistry& menus,
                           Editor::PanelRegistry& panels, PluginsWindow& window,
                           Plugins::PluginManager& manager, std::function<Project*()> project,
                           std::function<const Scene*()> openScene,
                           std::function<void()> openPanel) {
    Editor::MenuEntryDesc entry;
    entry.id = "Edit.Plugins";
    entry.menu = "Edit";
    entry.section = "Configuration";
    entry.label = "Plugins";
    entry.tooltip = "Enable and disable plugins for this project.";
    entry.owner = "Engine";
    entry.order = 190;
    entry.execute = [openPanel] {
        if (openPanel) openPanel();
    };
    registrations.Add(menus.RegisterEntry(std::move(entry)));

    Editor::PanelDesc panel;
    panel.id = kPluginsPanelId;
    panel.title = "Plugins";
    panel.tooltip = "Installed plugins for this project.";
    panel.owner = "Engine";
    panel.dockNextTo = "console";
    panel.dockDir = Slate::DockDir::Center;
    panel.openByDefault = false;
    panel.showInWindowMenu = true;
    panel.draw = [&window, &manager, project, openScene](Slate::Context& ui, const Rect& area,
                                                         Editor::Panels::PanelContext&) {
        window.Draw(ui, area, manager, project ? project() : nullptr,
                    openScene ? openScene() : nullptr);
    };
    registrations.Add(panels.Register(std::move(panel)));
}

}  // namespace Luma::EditorPlugins
