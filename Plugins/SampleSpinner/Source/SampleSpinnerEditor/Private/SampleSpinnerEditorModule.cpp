#include "Luma/Sample/SampleSpinnerComponent.h"

#include "Luma/Editor/MenuRegistry.h"
#include "Luma/Editor/PanelRegistry.h"
#include "Luma/Editor/SettingsPageRegistry.h"
#include "Luma/Plugins/Module.h"
#include "Luma/Scene/Components.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Slate/Context.h"

#include <algorithm>
#include <string>

using namespace Luma;
using namespace Luma::Plugins;
using Slate::Rect;

namespace {

struct SpinnerEditorSettings {
    f32 defaultDegreesPerSecond = 90.0f;
    bool showCountInPanel = true;
};

class SampleSpinnerEditorModule final : public IModuleInterface {
public:
    void StartupModule(ModuleContext& ctx) override {
        PluginEditorServices* editor = ctx.Editor();
        if (!editor || !editor->panels || !editor->menus || !editor->settingsPages) {
            ctx.Fail("SampleSpinnerEditor needs editor registries (menus, panels, settings pages)");
            return;
        }

        const std::string owner = ctx.Plugin() ? ctx.Plugin()->name : "SampleSpinner";

        Editor::PanelDesc panel;
        panel.id = "sample.spinnerStats";
        panel.title = "Spinner Stats";
        panel.tooltip = "How many Spinner components are in the open scene.";
        panel.owner = owner;
        panel.dockNextTo = "console";
        panel.dockDir = Slate::DockDir::Center;
        panel.openByDefault = true;
        panel.showInWindowMenu = true;
        panel.draw = [this](Slate::Context& ui, const Rect& area, Editor::Panels::PanelContext& pc) {
            DrawStats(ui, area, pc);
        };
        std::string error;
        auto panelToken = editor->panels->Register(std::move(panel), &error);
        if (!panelToken) {
            ctx.Fail(error.empty() ? "failed to register Spinner Stats" : error);
            return;
        }
        ctx.Track(std::move(panelToken));

        Editor::MenuEntryDesc entry;
        entry.id = "Window.SpinnerStats";
        entry.menu = "Window";
        entry.section = "Plugins";
        entry.label = "Spinner Stats";
        entry.tooltip = "Open the Spinner Stats panel.";
        entry.owner = owner;
        entry.order = 50;
        entry.execute = [open = editor->openPanel] {
            if (open) open("sample.spinnerStats");
        };
        auto menuToken = editor->menus->RegisterEntry(std::move(entry), &error);
        if (!menuToken) {
            ctx.Fail(error.empty() ? "failed to register Spinner Stats menu" : error);
            return;
        }
        ctx.Track(std::move(menuToken));

        Editor::SettingsPageDesc page;
        page.id = "Plugins.SampleSpinner";
        page.category = "Plugins";
        page.title = "Sample Spinner";
        page.tooltip = "Defaults used by the Sample Spinner plugin.";
        page.owner = owner;
        page.configFile = "SampleSpinner.json";
        page.load = [this](const SerialValue& section) {
            if (auto v = section.Find("defaultDegreesPerSecond")) {
                m_settings.defaultDegreesPerSecond =
                    static_cast<f32>(v->AsFloat(m_settings.defaultDegreesPerSecond));
            }
            if (auto v = section.Find("showCountInPanel")) {
                m_settings.showCountInPanel = v->AsBool(m_settings.showCountInPanel);
            }
        };
        page.save = [this] {
            auto out = SerialValue::MakeObject();
            out["defaultDegreesPerSecond"] = static_cast<f64>(m_settings.defaultDegreesPerSecond);
            out["showCountInPanel"] = m_settings.showCountInPanel;
            return out;
        };
        page.draw = [this](Slate::Context& ui, const Rect& area) { return DrawSettings(ui, area); };
        auto pageToken = editor->settingsPages->Register(std::move(page), &error);
        if (!pageToken) {
            ctx.Fail(error.empty() ? "failed to register Sample Spinner settings" : error);
            return;
        }
        ctx.Track(std::move(pageToken));
    }

private:
    void DrawStats(Slate::Context& ui, const Rect& area, Editor::Panels::PanelContext& pc) const {
        Slate::Theme& t = ui.theme();
        usize count = 0;
        if (pc.scene) {
            count = pc.scene->Registry().view<SampleSpinnerComponent>().size();
        }
        ui.Heading({area.x, area.y, area.w, 24.0f}, "Spinner Stats", t.accent);
        if (m_settings.showCountInPanel) {
            ui.LabelIn({area.x, area.y + 28.0f, area.w, 20.0f},
                       "Spinners in scene: " + std::to_string(count), t.text);
        } else {
            ui.LabelIn({area.x, area.y + 28.0f, area.w, 20.0f},
                       "Count hidden (see Project Settings → Sample Spinner).", t.textDim);
        }
        ui.LabelIn({area.x, area.y + 52.0f, area.w, 20.0f},
                   "Default speed: " + std::to_string(m_settings.defaultDegreesPerSecond) +
                       " deg/s",
                   t.textDim);
    }

    bool DrawSettings(Slate::Context& ui, const Rect& area) {
        const f32 labelWidth = std::min(220.0f, area.w * 0.5f);
        Rect field{area.x + labelWidth, area.y, area.w - labelWidth, 22.0f};
        ui.LabelIn({area.x, area.y, labelWidth, 22.0f}, "Default Degrees / Second",
                   ui.theme().textDim);
        Slate::SpinBoxOptions opts;
        opts.min = -720.0;
        opts.max = 720.0;
        opts.speed = 1.0;
        bool changed =
            ui.SpinBox(Slate::Context::ID("sample.spinner.speed"), field,
                       m_settings.defaultDegreesPerSecond, opts)
                .changed;
        field.y += 28.0f;
        bool show = m_settings.showCountInPanel;
        if (ui.Checkbox(Slate::Context::ID("sample.spinner.showCount"),
                        {area.x, field.y, area.w, 22.0f}, "Show spinner count in the panel",
                        show)) {
            m_settings.showCountInPanel = show;
            changed = true;
        }
        return changed;
    }

    SpinnerEditorSettings m_settings;
};

}  // namespace

LUMA_IMPLEMENT_MODULE(SampleSpinnerEditorModule, "SampleSpinnerEditor")
