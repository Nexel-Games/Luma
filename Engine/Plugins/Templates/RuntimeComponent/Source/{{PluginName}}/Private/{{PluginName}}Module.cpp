#include "{{PluginName}}Component.h"

#include "Luma/Plugins/Module.h"
#include "Luma/Scene/ComponentReflection.h"
#include "Luma/Scene/ComponentRegistry.h"

using namespace Luma;
using namespace Luma::Plugins;

namespace Luma {
template <>
const TypeInfo<{{PluginName}}Component>& GetTypeInfo<{{PluginName}}Component>() {
    static const TypeInfo<{{PluginName}}Component> info = [] {
        TypeBuilder<{{PluginName}}Component> b("{{PluginName}}");
        b.Property("value", &{{PluginName}}Component::value);
        return b.Build();
    }();
    return info;
}
}  // namespace Luma

namespace {

class {{PluginName}}Module final : public IModuleInterface {
public:
    void StartupModule(ModuleContext& ctx) override {
        ComponentDesc desc;
        desc.id = "{{PluginName}}";
        desc.displayName = "{{FriendlyName}}";
        desc.category = "{{Category}}";
        desc.owner = ctx.Plugin() ? ctx.Plugin()->name : "{{PluginName}}";
        std::string error;
        auto token = ctx.Components().Register<{{PluginName}}Component>(std::move(desc), &error);
        if (!token) {
            ctx.Fail(error.empty() ? "failed to register {{PluginName}}" : error);
            return;
        }
        ctx.Track(std::move(token));
    }
};

}  // namespace

LUMA_IMPLEMENT_MODULE({{PluginName}}Module, "{{PluginName}}")
