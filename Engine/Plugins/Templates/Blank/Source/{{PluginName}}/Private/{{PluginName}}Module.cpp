#include "Luma/Plugins/Module.h"

using namespace Luma::Plugins;

namespace {

class {{PluginName}}Module final : public IModuleInterface {
public:
    void StartupModule(ModuleContext&) override {}
};

}  // namespace

LUMA_IMPLEMENT_MODULE({{PluginName}}Module, "{{PluginName}}")
