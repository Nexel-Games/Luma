#include "Luma/Sample/SampleSpinnerComponent.h"

#include "Luma/Plugins/Module.h"
#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/Components.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Scene/SceneSystems.h"

#include <memory>
#include <string>

using namespace Luma;
using namespace Luma::Plugins;

namespace {

class SampleSpinnerSystem final : public ISceneSystem {
public:
    std::string_view Id() const override { return "SampleSpinner.Spinner"; }

    void Update(Scene& scene, f64 seconds) override {
        auto view = scene.Registry().view<SampleSpinnerComponent, TransformComponent>();
        for (auto [entity, spinner, transform] : view.each()) {
            const f32 deg = spinner.degreesPerSecond * static_cast<f32>(seconds);
            transform.rotationEuler.x += spinner.axis.x * deg;
            transform.rotationEuler.y += spinner.axis.y * deg;
            transform.rotationEuler.z += spinner.axis.z * deg;
        }
    }
};

class SampleSpinnerModule final : public IModuleInterface {
public:
    void StartupModule(ModuleContext& ctx) override {
        ComponentDesc desc;
        desc.id = "SampleSpinner";
        desc.displayName = "Spinner";
        desc.category = "Simulation";
        desc.tooltip = "Rotates this actor during Play.";
        desc.owner = ctx.Plugin() ? ctx.Plugin()->name : "SampleSpinner";
        std::string error;
        auto token = ctx.Components().Register<SampleSpinnerComponent>(std::move(desc), &error);
        if (!token) {
            ctx.Fail(error.empty() ? "failed to register SampleSpinner" : error);
            return;
        }
        ctx.Track(std::move(token));

        std::string systemError;
        auto systemToken =
            ctx.SceneSystems().Register(std::make_unique<SampleSpinnerSystem>(), 100, &systemError);
        if (!systemToken) {
            ctx.Fail(systemError.empty() ? "failed to register the spinner scene system"
                                         : systemError);
            return;
        }
        ctx.Track(std::move(systemToken));
    }
};

}  // namespace

LUMA_IMPLEMENT_MODULE(SampleSpinnerModule, "SampleSpinner")
