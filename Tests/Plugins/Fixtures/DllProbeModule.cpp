#include "DllProbeComponent.h"

#include "Luma/Plugins/Module.h"
#include "Luma/Scene/ComponentReflection.h"
#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/Components.h"

namespace Luma {
template <>
const TypeInfo<DllProbeComponent>& GetTypeInfo<DllProbeComponent>() {
    static const TypeInfo<DllProbeComponent> info = [] {
        TypeBuilder<DllProbeComponent> b("DllProbe");
        b.Property("value", &DllProbeComponent::value);
        return b.Build();
    }();
    return info;
}
}  // namespace Luma

using namespace Luma;
using namespace Luma::Plugins;

namespace {

class DllProbeModule final : public IModuleInterface {
public:
    void StartupModule(ModuleContext& ctx) override {
        ComponentDesc desc;
        desc.id = "DllProbe";
        desc.displayName = "DLL Probe";
        desc.category = "Tests";
        desc.owner = "DllProbe";
        std::string error;
        auto token = ctx.Components().Register<DllProbeComponent>(std::move(desc), &error);
        if (!token) {
            ctx.Fail(error.empty() ? "failed to register DllProbe" : error);
            return;
        }
        ctx.Track(std::move(token));
    }
};

}  // namespace

LUMA_IMPLEMENT_MODULE(DllProbeModule, "DllProbe")
