#include "Luma/Editor/MenuRegistry.h"
#include "Luma/Plugins/Module.h"

using namespace Luma;
using namespace Luma::Plugins;

namespace {

class {{EditorModuleName}}Module final : public IModuleInterface {
public:
    void StartupModule(ModuleContext& ctx) override {
        PluginEditorServices* editor = ctx.Editor();
        if (!editor || !editor->menus) {
            ctx.Fail("{{EditorModuleName}} needs editor menus");
            return;
        }
        Editor::MenuEntryDesc entry;
        entry.id = "{{PluginName}}.Tools.Command";
        entry.menu = "Tools";
        entry.section = "Plugins";
        entry.label = "{{FriendlyName}}";
        entry.tooltip = "{{Description}}";
        entry.owner = ctx.Plugin() ? ctx.Plugin()->name : "{{PluginName}}";
        entry.order = 50;
        entry.execute = [] {};
        std::string error;
        auto token = editor->menus->RegisterEntry(std::move(entry), &error);
        if (!token) {
            ctx.Fail(error.empty() ? "failed to register Tools command" : error);
            return;
        }
        ctx.Track(std::move(token));
    }
};

}  // namespace

LUMA_IMPLEMENT_MODULE({{EditorModuleName}}Module, "{{EditorModuleName}}")
