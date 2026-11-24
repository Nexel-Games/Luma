#include "Luma/Plugins/Module.h"

using namespace Luma::Plugins;

namespace {
class Dummy final : public IModuleInterface {
public:
    void StartupModule(ModuleContext&) override {}
};
}  // namespace

extern "C" __declspec(dllexport) std::uint32_t LumaModuleApiVersion() { return 999; }
extern "C" __declspec(dllexport) IModuleInterface* LumaCreateModule() { return new Dummy(); }
extern "C" __declspec(dllexport) void LumaDestroyModule(IModuleInterface* m) { delete m; }
