#include "Luma/Editor/Panels/ProjectSettings.h"
#include "Luma/Scene/SceneSystems.h"
#include <algorithm>

namespace Luma::Editor::Panels {

RegistrationToken RegisterSimulationPage(SettingsPageRegistry& pages, SimulationSettings& settings) {
    SettingsPageDesc page;
    page.id = "Project.Simulation";
    page.title = "Simulation";
    page.category = "Project";
    page.owner = "Engine";
    page.tooltip = "Fixed updates for Play in Editor.";
    page.configFile = "Simulation.json";
    page.load = [&settings](const SerialValue& section) {
        SimulationSettings loaded;
        if (auto v = section.Find("fixedTimeStep")) loaded.fixedTimeStep = v->AsFloat(loaded.fixedTimeStep);
        if (auto v = section.Find("maxSubsteps"))
            loaded.maxSubsteps = static_cast<i32>(std::clamp<i64>(v->AsInt(loaded.maxSubsteps), 1, 128));
        settings = loaded.Valid() ? loaded : SimulationSettings{};
    };
    page.save = [&settings] {
        auto out = SerialValue::MakeObject();
        out["fixedTimeStep"] = settings.fixedTimeStep;
        out["maxSubsteps"] = settings.maxSubsteps;
        return out;
    };
    page.draw = [&settings](Slate::Context& ui, const Slate::Rect& area) {
        const f32 labelWidth = std::min(170.0f, area.w * 0.5f);
        Slate::Rect field{area.x + labelWidth, area.y, area.w - labelWidth, 22};
        ui.LabelIn({area.x, area.y, labelWidth, 22}, "Fixed Time Step (s)", ui.theme().textDim);
        Slate::SpinBoxOptions opts;
        opts.min = 0.0001;
        opts.max = 1.0;
        opts.speed = 0.0001;
        opts.maxFractionalDigits = 6;
        f32 step = static_cast<f32>(settings.fixedTimeStep);
        const bool changed = ui.SpinBox(Slate::Context::ID("simulation.fixed"), field, step, opts).changed;
        if (changed) settings.fixedTimeStep = std::clamp(static_cast<f64>(step), 0.0001, 1.0);
        field.y += 28;
        ui.LabelIn({area.x, field.y, labelWidth, 22}, "Max Substeps", ui.theme().textDim);
        opts.min = 1;
        opts.max = 128;
        opts.speed = 0.1;
        const bool capChanged = ui.SpinBoxInt(Slate::Context::ID("simulation.cap"), field, settings.maxSubsteps, opts).changed;
        return changed || capChanged;
    };
    return pages.Register(std::move(page));
}

}  // namespace Luma::Editor::Panels
