#include "Luma/Editor/MenuRegistry.h"
#include "Luma/Editor/PanelRegistry.h"
#include "Luma/Plugins/Module.h"
#include "Luma/Slate/Context.h"

using namespace Luma;
using namespace Luma::Plugins;
using Slate::Rect;

namespace {

class {{EditorModuleName}}Module final : public IModuleInterface {
public:
    void StartupModule(ModuleContext& ctx) override {
        PluginEditorServices* editor = ctx.Editor();
        if (!editor || !editor->panels || !editor->menus) {
            ctx.Fail("{{EditorModuleName}} needs editor panels and menus");
            return;
        }
        const std::string owner = ctx.Plugin() ? ctx.Plugin()->name : "{{PluginName}}";
        const std::string panelId = "{{PluginName}}.window";

        Editor::PanelDesc panel;
        panel.id = panelId;
        panel.title = "{{FriendlyName}}";
        panel.tooltip = "{{Description}}";
        panel.owner = owner;
        panel.dockNextTo = "console";
        panel.dockDir = Slate::DockDir::Center;
        panel.openByDefault = false;
        panel.showInWindowMenu = true;
        panel.draw = [](Slate::Context& ui, const Rect& area, Editor::Panels::PanelContext&) {
            ui.LabelIn(area, "{{FriendlyName}}", ui.theme().text);
        };
        std::string error;
        auto panelToken = editor->panels->Register(std::move(panel), &error);
        if (!panelToken) {
            ctx.Fail(error.empty() ? "failed to register panel" : error);
            return;
        }
        ctx.Track(std::move(panelToken));

        Editor::MenuEntryDesc entry;
        entry.id = "Window.{{PluginName}}";
        entry.menu = "Window";
        entry.section = "Plugins";
        entry.label = "{{FriendlyName}}";
        entry.tooltip = "Open {{FriendlyName}}.";
        entry.owner = owner;
        entry.order = 50;
        entry.execute = [open = editor->openPanel, panelId] {
            if (open) open(panelId);
        };
        auto menuToken = editor->menus->RegisterEntry(std::move(entry), &error);
        if (!menuToken) {
            ctx.Fail(error.empty() ? "failed to register Window menu" : error);
            return;
        }
        ctx.Track(std::move(menuToken));
    }
};

}  // namespace

LUMA_IMPLEMENT_MODULE({{EditorModuleName}}Module, "{{EditorModuleName}}")
