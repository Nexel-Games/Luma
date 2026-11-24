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
#include "Luma/Slate/Icons.h"
#include "Luma/Slate/Menu.h"
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
        return r.empty() ? "Failed" : ("Failed - " + r);
    }
    if (status.state == PluginState::Unavailable) {
        const std::string r = reason();
        return r.empty() ? "Unavailable" : ("Unavailable - " + r);
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

namespace {

std::string Title(const PluginStatus& s) {
    return s.descriptor.friendlyName.empty() ? (s.manifest.displayName.empty() ? s.manifest.id : s.manifest.displayName)
                                             : s.descriptor.friendlyName;
}

std::string VersionText(const PluginStatus& s) {
    const std::string v = s.descriptor.versionName.ToString();
    return v.empty() ? s.manifest.version.ToString() : v;
}

std::string Description(const PluginStatus& s) {
    return s.descriptor.description.empty() ? s.manifest.description : s.descriptor.description;
}

std::string Author(const PluginStatus& s) {
    return s.descriptor.createdBy.empty() ? s.manifest.author : s.descriptor.createdBy;
}

std::string AuthorUrl(const PluginStatus& s) {
    return s.descriptor.createdByUrl.empty() ? s.manifest.url : s.descriptor.createdByUrl;
}

bool Problem(const PluginStatus& s) {
    return s.state == PluginState::Failed || s.state == PluginState::Unavailable ||
           s.state == PluginState::NotInstalled;
}

std::filesystem::path PluginFolder(const PluginStatus& s) { return s.descriptor.descriptorPath.parent_path(); }

const Color kWhite = Color::RGB(255, 255, 255);

// Text in a specific font, vertically centred in `r` (no padding).
void Text(Slate::Context& ui, Slate::Font& font, const Rect& r, std::string_view text, Color color,
          Slate::Align align = Slate::Align::Left) {
    const f32 w = font.Measure(text).x;
    f32 x = r.x;
    if (align == Slate::Align::Center) x = r.x + (r.w - w) * 0.5f;
    else if (align == Slate::Align::Right) x = r.Right() - w;
    auto& draw = ui.drawList();
    draw.PushClip(r);
    draw.AddText(font, {x, r.y + (r.h - font.LineHeight()) * 0.5f}, text, color);
    draw.PopClip();
}

// A small rounded label (Beta / Experimental / Project).
f32 Badge(Slate::Context& ui, f32 x, f32 y, std::string_view text, Color fill, Color ink) {
    const f32 w = ui.font().Measure(text).x + 14.0f;
    ui.PanelRounded({x, y, w, 18.0f}, fill, 9.0f);
    ui.LabelIn({x, y, w, 18.0f}, text, ink, Slate::Align::Center);
    return w;
}

// A flat text link with a leading glyph: true when clicked.
bool Link(Slate::Context& ui, u64 id, f32 x, f32 y, Slate::Icon icon, std::string_view text, f32& width) {
    const Slate::Theme& t = ui.theme();
    width = ui.font().Measure(text).x + 38.0f;
    (void)t;
    return ui.IconTextButton(id, {x, y, width, 26.0f}, icon, text, kWhite, false, 19.0f);
}

}  // namespace

void PluginsWindow::OpenWizard() {
    m_wizardOpen = true;
    m_wizardName.clear();
    m_wizardFriendly.clear();
    m_wizardDescription.clear();
    if (m_wizardCategory.empty()) m_wizardCategory = "Other";
    m_wizardLocation = m_projectPlugins.empty() ? 1 : 0;
}

void PluginsWindow::OpenDescriptorEditor(const PluginStatus& status) {
    m_edit = status.descriptor;
    m_editVersionName = m_edit.versionName.ToString();
    m_editIconPath.clear();
    m_editOpen = true;
}

void PluginsWindow::RequestToggle(PluginManager& manager, const PluginStatus& status, bool enable,
                                  const Scene* openScene) {
    m_pending = enable ? manager.PlanEnable(status.manifest.id) : manager.PlanDisable(status.manifest.id, openScene);
    if (!m_pending->blockedBy.empty()) {
        m_message = m_pending->blockedBy.front();
        m_messageError = true;
        m_pending.reset();
    }
}

TextureHandle PluginsWindow::IconFor(const PluginStatus& status) {
    const std::filesystem::path folder = PluginFolder(status);
    if (folder.empty() || !loadImage) return defaultPluginIcon;
    const std::string key = folder.generic_string();
    auto it = m_icons.find(key);
    if (it == m_icons.end()) {
        std::error_code ec;
        const std::filesystem::path icon = folder / "Resources" / "Icon128.png";
        it = m_icons.emplace(key, std::filesystem::is_regular_file(icon, ec) ? loadImage(icon) : TextureHandle{0})
                 .first;
    }
    return it->second ? it->second : defaultPluginIcon;
}

const PluginStatus* PluginsWindow::Selected(const std::vector<PluginStatus>& all) const {
    for (const PluginStatus& s : all) {
        if (s.manifest.id == m_selected) return &s;
    }
    return nullptr;
}

void PluginsWindow::DrawConfirm(Slate::Context& ui, PluginManager& manager, Project* project) {
    Slate::Theme& t = ui.theme();
    const auto& plan = *m_pending;
    const std::string title = plan.enable ? "Enable Plugin" : "Disable Plugin";
    TextureHandle icon = defaultPluginIcon;
    std::string name = plan.targetId;
    for (const PluginStatus& s : manager.Statuses()) {
        if (s.manifest.id != plan.targetId) continue;
        icon = IconFor(s);
        name = Title(s);
        break;
    }
    const u64 id = Slate::Context::ID("plugins.browser.confirm");
    const Rect content = ui.BeginModal(id, title, {540.0f, 220.0f}, icon);
    f32 my = content.y;
    Text(ui, ui.subtitleFont(), {content.x, my, content.w, 28.0f},
         (plan.enable ? "Enable " : "Disable ") + name + "?", kWhite);
    my += 34.0f;
    std::string body;
    if (plan.enable && plan.experimental) body += "This plugin is Experimental. ";
    else if (plan.enable && plan.beta) body += "This plugin is Beta. ";
    if (plan.enable && !plan.alsoEnable.empty()) {
        body += "Required dependencies that will also be enabled: " + Join(plan.alsoEnable, ", ") + ". ";
    }
    if (!plan.enable && !plan.dependents.empty()) {
        body += "Enabled plugins that require it: " + Join(plan.dependents, ", ") + ". ";
    }
    body += "The change applies after the editor restarts.";
    ui.LabelIn({content.x - t.space.lg, my, content.w + t.space.lg, 60.0f}, body, kWhite);
    my += 64.0f;
    if (!plan.enable && !plan.affectedComponents.empty()) {
        const Rect warn{content.x, my, content.w, 34.0f};
        ui.drawList().AddRectFilledRounded(warn, t.warning.WithAlpha(34), t.radius.md);
        ui.drawList().AddRectFilled({warn.x, warn.y, 3.0f, warn.h}, t.warning);
        Text(ui, ui.font(), warn.Inset(12.0f, 0.0f), "Open scene uses: " + Join(plan.affectedComponents, "; "),
             t.warning);
    }
    const char* okLabel = plan.enable ? (plan.alsoEnable.empty() ? "Enable" : "Enable All") : "Disable";
    auto result = ui.ModalButtonRow(id, content, okLabel, "Cancel");
    if (result.cancel || !result.open) m_pending.reset();
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

void PluginsWindow::DrawAbout(Slate::Context& ui, PluginManager& manager) {
    Slate::Theme& t = ui.theme();
    const u64 id = Slate::Context::ID("plugins.browser.about");
    const Rect content = ui.BeginModal(id, "About Plugins", {500.0f, 230.0f});
    if (logo) ui.Image(logo, {content.x, content.y, 56.0f, 56.0f});
    const f32 tx = content.x + 70.0f;
    ui.Heading({tx, content.y + 2.0f, content.w - 70.0f, 24.0f}, "Luma Plugins", kWhite);
    ui.LabelIn({tx, content.y + 26.0f, content.w - 70.0f, 20.0f},
               std::string("Luma Engine ") + LUMA_VERSION_STRING, t.textDim);
    int enabled = 0;
    const auto all = manager.Statuses();
    for (const PluginStatus& s : all) enabled += s.enabledInProject ? 1 : 0;
    ui.LabelIn({content.x, content.y + 70.0f, content.w, 20.0f},
               std::to_string(all.size()) + " plugin(s) found, " + std::to_string(enabled) + " enabled in this project.",
               t.text);
    ui.LabelIn({content.x, content.y + 92.0f, content.w, 40.0f},
               "Plugins live in the engine's Plugins folder (built-in) and the project's Plugins folder. "
               "Enabling or disabling one takes effect after a restart.",
               t.textDim);
    const auto r = ui.ModalButtonRow(id, content, "Close", "");
    if (r.ok || r.cancel || !r.open) m_aboutOpen = false;
    ui.EndModal();
}

void PluginsWindow::DrawMenuBar(Slate::Context& ui, const Rect& bar, PluginManager& manager, Project* project,
                                const Scene* openScene, bool writable) {
    Slate::Theme& t = ui.theme();
    ui.Panel(bar, t.surface1);
    ui.Panel({bar.x, bar.Bottom() - 1.0f, bar.w, 1.0f}, t.separator);
    const std::vector<PluginStatus> all = manager.Statuses();
    const PluginStatus* sel = Selected(all);
    using Slate::MenuBuilder;
    using Slate::MenuEntryOptions;
    auto opt = [](std::string tip, bool enabled = true, bool checked = false) {
        MenuEntryOptions o;
        o.tooltip = std::move(tip);
        o.enabled = enabled;
        o.checked = checked;
        return o;
    };
    auto openFolder = [](const std::filesystem::path& folder) {
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        OpenExternal(folder.string());
    };
    std::string selId = sel ? sel->manifest.id : std::string{};
    const bool selProject = sel && sel->origin == PluginOrigin::Project;
    const bool selEnabled = sel && sel->enabledInProject;
    const bool canToggle = sel && writable && sel->state != PluginState::NotInstalled;
    const std::filesystem::path selFolder = sel ? PluginFolder(*sel) : std::filesystem::path{};
    const std::string docs = sel ? sel->descriptor.docsUrl : std::string{};
    const std::string support = sel ? sel->descriptor.supportUrl : std::string{};
    auto browse = [this](const std::string& url) {
        if (openUrl) openUrl(url);
        else OpenExternal(url);
    };

    struct Item {
        const char* label;
        std::function<std::vector<Slate::MenuEntry>()> build;
    };
    const Item items[] = {
        {"File",
         [&] {
             MenuBuilder b;
             b.BeginSection("Plugins");
             b.AddEntry("New Plugin...", Slate::Icon::Plus, [this] { OpenWizard(); },
                        opt("Create a plugin from a template.", writable || !m_enginePlugins.empty()));
             b.AddEntry("Refresh Plugin List", Slate::Icon::Refresh,
                        [this, &manager, project] { Rediscover(manager, project); },
                        opt("Read the plugin folders again (new or removed plugins)."));
             b.EndSection();
             b.BeginSection("Folders");
             b.AddEntry("Open Project Plugins Folder", Slate::Icon::Browse,
                        [openFolder, f = m_projectPlugins] { openFolder(f); },
                        opt("Show the project's Plugins folder.", !m_projectPlugins.empty()));
             b.AddEntry("Open Engine Plugins Folder", Slate::Icon::Browse,
                        [openFolder, f = m_enginePlugins] { openFolder(f); },
                        opt("Show the engine's Plugins folder.", !m_enginePlugins.empty()));
             b.EndSection();
             b.BeginSection();
             b.AddEntry("Close", Slate::Icon::Close, [this] { if (onClose) onClose(); },
                        opt("Close the Plugins window.", static_cast<bool>(onClose)));
             b.EndSection();
             return b.Build();
         }},
        {"Edit",
         [&] {
             MenuBuilder b;
             b.BeginSection("Selected Plugin");
             b.AddEntry(selEnabled ? "Disable Plugin" : "Enable Plugin", Slate::Icon::Check,
                        [this, &manager, selId, selEnabled, openScene] {
                            for (const PluginStatus& s : manager.Statuses()) {
                                if (s.manifest.id == selId) RequestToggle(manager, s, !selEnabled, openScene);
                            }
                        },
                        opt(sel ? "Turn the selected plugin on or off for this project." : "Select a plugin first.",
                            canToggle));
             b.AddEntry("Edit Plugin Descriptor...", Slate::Icon::File,
                        [this, &manager, selId] {
                            for (const PluginStatus& s : manager.Statuses()) {
                                if (s.manifest.id == selId) OpenDescriptorEditor(s);
                            }
                        },
                        opt("Edit the selected project plugin's name, description, version and links.",
                            selProject && writable));
             b.EndSection();
             b.BeginSection("Search");
             b.AddEntry("Clear Search", Slate::Icon::Close, [this] { m_search.clear(); },
                        opt("Show every plugin again.", !m_search.empty()));
             b.EndSection();
             return b.Build();
         }},
        {"Window",
         [&] {
             MenuBuilder b;
             b.BeginSection("Plugins Window");
             b.AddEntry("Dock in Window Row", Slate::Icon::Grip, [this] { if (onDock) onDock(); },
                        opt("Dock this window as a tab in the row above the toolbar.", static_cast<bool>(onDock)));
             b.AddEntry("Float", Slate::Icon::Duplicate, [this] { if (onFloat) onFloat(); },
                        opt("Undock this window over the level editor.", static_cast<bool>(onFloat)));
             b.AddEntry("Minimize", Slate::Icon::Minus, [this] { if (onMinimize) onMinimize(); },
                        opt("Keep the window as a tab in the window row.", static_cast<bool>(onMinimize)));
             b.EndSection();
             return b.Build();
         }},
        {"Tools",
         [&] {
             MenuBuilder b;
             b.BeginSection("Plugin");
             b.AddEntry("Show in Explorer", Slate::Icon::Browse, [selFolder] { OpenExternal(selFolder.string()); },
                        opt("Open the selected plugin's folder.", !selFolder.empty()));
             b.EndSection();
             b.BeginSection("Editor");
             b.AddEntry("Restart Editor Now", Slate::Icon::Refresh, [this] { m_restartNow = true; },
                        opt("Restart to apply the plugin changes.", manager.RestartRequired()));
             b.EndSection();
             return b.Build();
         }},
        {"Help",
         [&] {
             MenuBuilder b;
             b.BeginSection("Selected Plugin");
             b.AddEntry("Plugin Documentation", Slate::Icon::File, [browse, docs] { browse(docs); },
                        opt("Open the selected plugin's documentation.", !docs.empty()));
             b.AddEntry("Plugin Support", Slate::Icon::File, [browse, support] { browse(support); },
                        opt("Open the selected plugin's support page.", !support.empty()));
             b.EndSection();
             b.BeginSection();
             b.AddEntry("About Plugins", Slate::Icon::Dot, [this] { m_aboutOpen = true; }, opt(""));
             b.EndSection();
             return b.Build();
         }},
    };

    f32 x = bar.x + 6.0f;
    for (const Item& item : items) {
        const f32 w = ui.font().Measure(item.label).x + 18.0f;
        const Rect r{x, bar.y + 2.0f, w, bar.h - 4.0f};
        const u64 id = Slate::Context::ID(std::string("plugins.menu.") + item.label);
        bool open = ui.MenuButton(id, r, item.label);
        // While one of this bar's menus is open, hovering another title switches to it.
        bool anyOpen = false;
        for (const Item& other : items) {
            anyOpen = anyOpen || ui.IsMenuOpen(Slate::Context::ID(std::string("plugins.menu.") + other.label));
        }
        if (anyOpen && !ui.IsMenuOpen(id) && r.Contains(ui.mouse())) open = true;
        if (open && !ui.IsMenuOpen(id)) {
            Slate::MenuOptions o;
            o.minWidth = 220.0f;
            ui.OpenMenuBelow(id, r, item.build(), o);
        }
        x += w + 2.0f;
    }
}

void PluginsWindow::DrawHeader(Slate::Context& ui, const Rect& header, const std::vector<PluginStatus>& all) {
    Slate::Theme& t = ui.theme();
    ui.GradientRect(header, t.surface2, t.surface1);
    ui.Panel({header.x, header.Bottom() - 1.0f, header.w, 1.0f}, t.separator);
    // Brand block: the Luma logo beside the window's title (not in a corner badge).
    const f32 logoS = 44.0f;
    const Rect logoR{header.x + 16.0f, header.y + (header.h - logoS) * 0.5f, logoS, logoS};
    if (logo) {
        ui.Image(logo, logoR);
    } else {
        ui.LogoMark({logoR.x + logoS * 0.5f, logoR.y + logoS * 0.5f}, logoS * 0.5f);
    }
    (void)all;

    // Actions: Add, search, view settings.
    const f32 h = 30.0f;
    const f32 cy = header.y + (header.h - h) * 0.5f;
    const Rect settings{header.Right() - 124.0f, cy, 112.0f, h};
    const u64 settingsId = Slate::Context::ID("plugins.browser.settings");
    if (ui.IconTextButton(settingsId, settings, Slate::Icon::Gear, "Settings", kWhite, ui.IsMenuOpen(settingsId),
                          22.0f)) {
        Slate::MenuBuilder b;
        b.BeginSection("Show");
        auto toggle = [](bool& flag, const char* tip) {
            Slate::MenuEntryOptions o;
            o.tooltip = tip;
            o.checked = flag;
            return o;
        };
        b.AddEntry("Enabled Plugins", Slate::Icon::None, [this] { m_showEnabled = !m_showEnabled; },
                   toggle(m_showEnabled, "List plugins that are enabled in this project."));
        b.AddEntry("Disabled Plugins", Slate::Icon::None, [this] { m_showDisabled = !m_showDisabled; },
                   toggle(m_showDisabled, "List plugins that are not enabled."));
        b.AddEntry("Beta and Experimental", Slate::Icon::None, [this] { m_showPreview = !m_showPreview; },
                   toggle(m_showPreview, "List plugins marked Beta or Experimental."));
        b.EndSection();
        Slate::MenuOptions o;
        o.minWidth = 220.0f;
        ui.OpenMenuBelow(settingsId, settings, b.Build(), o);
    }
    ui.Tooltip(settingsId ^ 0x71ull, settings, "View Settings\nChoose which plugins the list shows.");
    const Rect add{logoR.Right() + 16.0f, cy, 82.0f, h};
    const u64 addId = Slate::Context::ID("plugins.browser.add");
    if (ui.IconTextButton(addId, add, Slate::Icon::Plus, "Add", t.success, false, 18.0f)) OpenWizard();
    ui.Tooltip(addId ^ 0x71ull, add, "New Plugin\nCreate a plugin from a template.");
    const Rect search{add.Right() + 10.0f, cy, std::max(120.0f, settings.x - add.Right() - 22.0f), h};
    ui.SearchBox(Slate::Context::ID("plugins.browser.search"), search, m_search, "Search plugins");
}

void PluginsWindow::Draw(Slate::Context& ui, const Rect& area, PluginManager& manager, Project* project,
                         const Scene* openScene) {
    Slate::Theme& t = ui.theme();
    const bool writable = project && project->IsWritable();
    ui.Panel(area, t.surface0);
    const std::vector<PluginStatus> all = manager.Statuses();
    if (!m_selected.empty() && !Selected(all)) m_selected.clear();

    const Rect menuBar{area.x, area.y, area.w, 28.0f};
    const Rect header{area.x, menuBar.Bottom(), area.w, 64.0f};
    DrawHeader(ui, header, all);
    f32 y = header.Bottom();

    // Notices: read-only project, the last error.
    auto notice = [&](std::string_view text, Color color) {
        const Rect r{area.x, y, area.w, 28.0f};
        ui.Panel(r, t.surface1);
        ui.Panel({r.x, r.y, 3.0f, r.h}, color);
        ui.LabelIn({r.x + 14.0f, r.y, r.w - 28.0f, r.h}, text, color);
        y += 28.0f;
    };
    if (!writable) {
        notice(project ? "The project file is read-only. Make it writable to change which plugins are enabled."
                       : "Open a project to change plugins.",
               t.warning);
    }
    if (!m_message.empty()) notice(m_message, m_messageError ? t.error : t.textDim);

    const f32 bannerH = manager.RestartRequired() ? 40.0f : 0.0f;
    const Rect body{area.x, y, area.w, area.Bottom() - y - bannerH};

    // Filter: search + view settings.
    std::vector<const PluginStatus*> visible;
    for (const PluginStatus& s : all) {
        if (!PluginMatchesQuery(s, m_search)) continue;
        if (s.enabledInProject ? !m_showEnabled : !m_showDisabled) continue;
        if (!m_showPreview && (s.descriptor.isBetaVersion || s.descriptor.isExperimentalVersion)) continue;
        visible.push_back(&s);
    }

    // --- Sidebar: All Plugins, then each origin with its categories -----------------
    const f32 treeW = std::min(260.0f, std::max(170.0f, body.w * 0.22f));
    const Rect tree{body.x, body.y, treeW, body.h};
    ui.Panel(tree, t.recessed);
    ui.Panel({tree.Right() - 1.0f, tree.y, 1.0f, tree.h}, t.separator);
    std::map<std::string, int> cats[2];
    int origins[2] = {0, 0};
    for (const PluginStatus* s : visible) {
        const int o = s->origin == PluginOrigin::Project ? 0 : 1;
        origins[o]++;
        cats[o][PluginCategory(*s)]++;
    }
    ui.PushClip(tree);
    f32 ty = tree.y + 10.0f - m_treeScroll;
    auto treeRow = [&](const std::string& key, const std::string& label, int count, f32 indent, bool group) {
        const Rect row{tree.x + 8.0f, ty, tree.w - 16.0f, group ? 30.0f : 26.0f};
        const bool selected = key == m_treeKey;
        const bool hover = row.Contains(ui.mouse()) && tree.Contains(ui.mouse());
        if (selected) {
            ui.PanelRounded(row, t.selectionBg, t.radius.sm);
            ui.PanelRounded({row.x, row.y + 5.0f, 3.0f, row.h - 10.0f}, t.accent, 1.5f);
        } else if (hover) {
            ui.PanelRounded(row, t.surface2, t.radius.sm);
        }
        const Color ink = kWhite;
        if (group) {
            ui.Heading({row.x + 12.0f + indent, row.y, row.w - 60.0f, row.h}, label, ink);
        } else {
            ui.LabelIn({row.x + 12.0f + indent, row.y, row.w - 60.0f, row.h}, label, ink);
        }
        ui.LabelIn({row.Right() - 48.0f, row.y, 40.0f, row.h}, std::to_string(count), kWhite, Slate::Align::Right);
        if (hover && ui.mousePressed(0)) {
            m_treeKey = key;
            m_listScroll = 0.0f;
        }
        ty += row.h + 2.0f;
    };
    treeRow("all", "All Plugins", static_cast<int>(visible.size()), 0.0f, true);
    ty += 8.0f;
    const char* groupKey[2] = {"project", "builtin"};
    const char* groupName[2] = {"PROJECT", "BUILT-IN"};
    for (int g = 0; g < 2; ++g) {
        ui.Panel({tree.x + 14.0f, ty, tree.w - 28.0f, 1.0f}, t.separator);
        ty += 6.0f;
        treeRow(groupKey[g], groupName[g], origins[g], 0.0f, true);
        for (const auto& [cat, n] : cats[g]) treeRow(std::string(groupKey[g]) + "/" + cat, cat, n, 14.0f, false);
        ty += 8.0f;
    }
    const f32 treeContent = ty + m_treeScroll - tree.y;
    ui.PopClip();
    m_treeScroll = ui.VerticalScroll(Slate::Context::ID("plugins.browser.tree.scroll"), tree, treeContent, m_treeScroll);

    auto show = [&](const PluginStatus& s) {
        if (m_treeKey == "all") return true;
        const std::string origin = s.origin == PluginOrigin::Project ? "project" : "builtin";
        if (m_treeKey == origin) return true;
        return m_treeKey == origin + "/" + PluginCategory(s);
    };
    std::string listTitle = "All Plugins";
    if (m_treeKey == "project") listTitle = "Project";
    else if (m_treeKey == "builtin") listTitle = "Built-In";
    else if (const auto slash = m_treeKey.find('/'); slash != std::string::npos) {
        listTitle = (m_treeKey.rfind("project", 0) == 0 ? "Project  /  " : "Built-In  /  ") +
                    m_treeKey.substr(slash + 1);
    }

    // --- Plugin cards ----------------------------------------------------------------------
    const Rect list{tree.Right(), body.y, body.w - treeW, body.h};
    Text(ui, ui.subtitleFont(), {list.x + 20.0f, list.y + 8.0f, list.w - 40.0f, 24.0f}, listTitle, kWhite);
    constexpr f32 kFooterH = 30.0f;
    const Rect cards{list.x, list.y + 36.0f, list.w, list.h - 36.0f - kFooterH};
    ui.PushClip(cards);
    // Branding (behind the cards, fixed while the list scrolls).
    {
        const Slate::Font& font = brandFont ? *brandFont : ui.titleFont();
        const char* mark = "LUMA PLUGINS";
        const Slate::Vec2 size = font.Measure(mark);
        ui.drawList().AddText(font, {cards.Right() - size.x - 28.0f, cards.Bottom() - size.y - 14.0f}, mark,
                              Color::RGBA(255, 255, 255, 26));
    }
    f32 cy = cards.y + 2.0f - m_listScroll;
    constexpr f32 kCardH = 104.0f;
    int shown = 0;
    for (const PluginStatus* s : visible) {
        if (!show(*s)) continue;
        ++shown;
        const Rect card{cards.x + 16.0f, cy, cards.w - 36.0f, kCardH};
        cy += kCardH + 8.0f;
        if (card.Bottom() < cards.y || card.y > cards.Bottom()) continue;  // off screen
        const bool selected = s->manifest.id == m_selected;
        const bool hover = card.Contains(ui.mouse()) && cards.Contains(ui.mouse());
        const Color fill = selected ? t.cardSelected : (hover ? t.cardHover : t.cardBg);
        ui.PanelRoundedBordered(card, fill, selected ? t.accent : t.outline, t.radius.md, selected ? 1.5f : 1.0f);

        // Enable checkbox.
        const Rect check{card.x + 16.0f, card.y + (card.h - 20.0f) * 0.5f, 20.0f, 20.0f};
        bool enabled = s->enabledInProject;
        const bool canToggle = writable && s->state != PluginState::NotInstalled;
        if (canToggle) {
            if (ui.Checkbox(Slate::Context::ID("plugins.browser.enable." + s->manifest.id), check, "", enabled) &&
                enabled != s->enabledInProject) {
                RequestToggle(manager, *s, enabled, openScene);
            }
        } else {
            ui.PanelRoundedBordered(check, t.fieldBg, t.fieldBorder, 3.0f);
            if (enabled) Slate::DrawIcon(ui, check.Inset(3.0f, 3.0f), Slate::Icon::Check, t.textDisabled);
        }

        // Icon tile.
        const Rect iconTile{check.Right() + 16.0f, card.y + 14.0f, 76.0f, 76.0f};
        ui.PanelRounded(iconTile, t.surface0, t.radius.md);
        if (const TextureHandle icon = IconFor(*s)) ui.Image(icon, iconTile.Inset(10.0f, 10.0f));

        // Name, badges, description.
        const f32 tx = iconTile.Right() + 16.0f;
        const f32 rightW = 200.0f;
        const std::string name = Title(*s);
        Slate::Font& nameFont = ui.headlineFont();
        const f32 textW = card.w - (tx - card.x) - rightW - 24.0f;
        Text(ui, nameFont, {tx, card.y + 8.0f, textW, 32.0f}, name, kWhite);
        f32 bx = tx + std::min(nameFont.Measure(name).x, textW) + 12.0f;
        const f32 badgeY = card.y + 15.0f;
        if (s->descriptor.isBetaVersion) bx += Badge(ui, bx, badgeY, "Beta", t.warning, t.surface0) + 6.0f;
        if (s->descriptor.isExperimentalVersion) {
            bx += Badge(ui, bx, badgeY, "Experimental", t.error, kWhite) + 6.0f;
        }
        if (s->origin == PluginOrigin::Project) Badge(ui, bx, badgeY, "Project", t.accentMuted, kWhite);
        Text(ui, ui.font(), {tx, card.y + 42.0f, textW, 22.0f}, Description(*s), kWhite);

        // Links: Edit (project plugins), Show in Explorer, Documentation.
        f32 lx = tx - 8.0f;
        const f32 ly = card.y + 68.0f;  // 26px link row: ends 6px above the card's bottom
        f32 lw = 0.0f;
        if (s->origin == PluginOrigin::Project && writable) {
            if (Link(ui, Slate::Context::ID("plugins.browser.edit." + s->manifest.id), lx, ly, Slate::Icon::Gear, "Edit",
                     lw)) {
                OpenDescriptorEditor(*s);
            }
            lx += lw + 4.0f;
        }
        const std::filesystem::path folder = PluginFolder(*s);
        if (!folder.empty()) {
            if (Link(ui, Slate::Context::ID("plugins.browser.folder." + s->manifest.id), lx, ly,
                     Slate::Icon::Browse, "Show in Explorer", lw)) {
                OpenExternal(folder.string());
            }
            lx += lw + 4.0f;
        }
        if (!s->descriptor.docsUrl.empty()) {
            if (Link(ui, Slate::Context::ID("plugins.browser.docs." + s->manifest.id), lx, ly, Slate::Icon::File,
                     "Documentation", lw)) {
                if (openUrl) openUrl(s->descriptor.docsUrl);
                else OpenExternal(s->descriptor.docsUrl);
            }
        }

        // Right column: version, author, state.
        const Rect right{card.Right() - rightW - 16.0f, card.y, rightW, card.h};
        Text(ui, ui.subtitleFont(), {right.x, card.y + 10.0f, right.w, 28.0f}, "Version " + VersionText(*s), kWhite,
             Slate::Align::Right);
        const std::string author = Author(*s);
        if (!author.empty()) {
            const Rect authorR{right.x, card.y + 40.0f, right.w, 26.0f};
            const std::string url = AuthorUrl(*s);
            const bool linkHover = !url.empty() && authorR.Contains(ui.mouse());
            Text(ui, ui.subtitleFont(), authorR, author, linkHover ? t.accent : kWhite, Slate::Align::Right);
            if (linkHover && ui.mousePressed(0)) {
                if (openUrl) openUrl(url);
                else OpenExternal(url);
            }
        }
        const Color stateColor =
            Problem(*s) ? t.error : (s->restartRequired ? t.warning : (s->enabledInProject ? t.success : kWhite));
        Text(ui, ui.font(), {right.x, card.y + 70.0f, right.w, 20.0f}, PluginStatusLine(*s),
             stateColor, Slate::Align::Right);

        // Select by clicking the card's background.
        if (hover && ui.mousePressed(0) && !check.Contains(ui.mouse())) m_selected = s->manifest.id;
    }
    if (shown == 0) {
        ui.LabelIn({cards.x, cards.y + 40.0f, cards.w, 24.0f},
                   m_search.empty() ? "No plugins here." : "No plugins match \"" + m_search + "\".", t.textDim,
                   Slate::Align::Center);
    }
    const f32 listContent = cy + m_listScroll - cards.y;
    ui.PopClip();
    m_listScroll = ui.VerticalScroll(Slate::Context::ID("plugins.browser.list.scroll"), cards, listContent, m_listScroll);

    // Footer: how many plugins there are, how many are on, how many are listed.
    {
        const Rect footer{list.x, cards.Bottom(), list.w, kFooterH};
        ui.Panel(footer, t.surface1);
        ui.Panel({footer.x, footer.y, footer.w, 1.0f}, t.separator);
        int enabledCount = 0;
        for (const PluginStatus& s : all) enabledCount += s.enabledInProject ? 1 : 0;
        const std::string counts = std::to_string(all.size()) + " installed  |  " +std::to_string(enabledCount) +
                                   " enabled  |  " +std::to_string(shown) + " shown";
        Text(ui, ui.font(), {footer.x + 20.0f, footer.y, footer.w - 40.0f, footer.h}, counts, kWhite);
    }

    // --- Restart bar ---------------------------------------------------------------------
    if (bannerH > 0.0f) {
        const Rect banner{area.x, area.Bottom() - bannerH, area.w, bannerH};
        ui.Panel(banner, t.surface2);
        ui.Panel({banner.x, banner.y, banner.w, 2.0f}, t.warning);
        ui.LabelIn({banner.x + 16.0f, banner.y, banner.w - 170.0f, banner.h},
                   "Restart the editor to apply your plugin changes.", t.warning);
        const Rect now{banner.Right() - 140.0f, banner.y + 7.0f, 124.0f, 26.0f};
        if (ui.IconTextButton(Slate::Context::ID("plugins.browser.restart"), now, Slate::Icon::Refresh, "Restart Now",
                              t.warning, false, 14.0f)) {
            m_restartNow = true;
        }
    }

    // Menu bar last: its menus open above everything drawn so far.
    DrawMenuBar(ui, menuBar, manager, project, openScene, writable);
    if (m_pending) DrawConfirm(ui, manager, project);
    if (m_wizardOpen) DrawWizard(ui, manager, project);
    if (m_editOpen) DrawDescriptorEditor(ui, manager, project);
    if (m_aboutOpen) DrawAbout(ui, manager);
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

    auto result = ui.ModalButtonRow(Slate::Context::ID("plugins.descriptor"), content, "Save", "Cancel");
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
                           std::function<void()> openWindow) {
    Editor::MenuEntryDesc entry;
    entry.id = "Edit.Plugins";
    entry.menu = "Edit";
    entry.section = "Configuration";
    entry.label = "Plugins";
    entry.icon = "Plugins";  // the plugin icon, registered by the editor
    entry.tooltip = "Enable and disable plugins for this project.";
    entry.owner = "Engine";
    entry.order = 190;
    entry.execute = [openWindow] {
        if (openWindow) openWindow();
    };
    registrations.Add(menus.RegisterEntry(std::move(entry)));
}

}  // namespace Luma::EditorPlugins
