#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "Luma/Core/Log.h"
#include "Luma/Core/Registration.h"
#include "Luma/Core/Types.h"
#include "Luma/Plugins/Api.h"
#include "Luma/Plugins/Plugin.h"
#include "Luma/Plugins/PluginDescriptor.h"
#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/EntityTemplates.h"
#include "Luma/Scene/SceneSystems.h"

// One compiled unit of plugin (or engine) code. Modules start and stop through
// ModuleContext. Registrations go through Track() and are released on shutdown
// or rollback. Main-thread only.

namespace Luma {
class AssetTypeRegistry;
}

namespace Luma::Import {
class AssetFactoryRegistry;
class TranslatorRegistry;
}

namespace Luma::Plugins {

inline constexpr u32 kModuleApiVersion = 1;

class ModuleContext {
public:
    ModuleContext(std::string name, const PluginDescriptor* plugin,
                  const PluginHostServices& services);

    std::string_view ModuleName() const { return m_name; }
    const PluginDescriptor* Plugin() const { return m_plugin; }
    std::filesystem::path PluginBaseDir() const;

    ComponentRegistry& Components();
    SceneSystemRegistry& SceneSystems();
    EntityTemplateRegistry& EntityTemplates();
    AssetTypeRegistry& AssetTypes();
    Import::AssetFactoryRegistry& AssetFactories();
    Import::TranslatorRegistry& Translators();
    PluginEditorServices* Editor();

    void Track(RegistrationToken token);
    usize TrackedCount() const { return m_tokens.Size(); }
    void ReleaseAll();

    void Fail(std::string message);
    bool Failed() const { return m_failed; }
    const std::string& FailMessage() const { return m_failMessage; }

    void Log(LogLevel level, std::string_view message) const;

private:
    std::string m_name;
    const PluginDescriptor* m_plugin = nullptr;
    PluginHostServices m_services;
    RegistrationSet m_tokens;
    bool m_failed = false;
    std::string m_failMessage;
};

class IModuleInterface {
public:
    virtual ~IModuleInterface() = default;
    virtual void StartupModule(ModuleContext& ctx) = 0;
    virtual void ShutdownModule(ModuleContext&) {}
    virtual bool SupportsDynamicReloading() const { return false; }
};

class IModuleBackend {
public:
    virtual ~IModuleBackend() = default;
    virtual bool IsAvailable(std::string_view name) const = 0;
    virtual std::unique_ptr<IModuleInterface> Create(std::string_view name,
                                                     std::string* error) = 0;
    virtual void Unload(std::string_view /*name*/) {}
};

// Static (monolithic) registrar. LUMA_IMPLEMENT_MODULE writes here.
class StaticModuleBackend final : public IModuleBackend {
public:
    using Factory = std::function<std::unique_ptr<IModuleInterface>()>;

    bool Register(std::string name, Factory factory, std::string* error = nullptr);
    bool IsAvailable(std::string_view name) const override;
    std::unique_ptr<IModuleInterface> Create(std::string_view name,
                                             std::string* error) override;

    static StaticModuleBackend& Global();

private:
    std::map<std::string, Factory, std::less<>> m_factories;
};

// Loads `Plugins/<Name>/Binaries/Win64/<Config>/LumaEditor-<Module>.dll`.
class DllModuleBackend final : public IModuleBackend {
public:
    void SetSearchRoots(std::span<const std::filesystem::path> roots);
    const std::vector<std::filesystem::path>& SearchRoots() const { return m_roots; }

    bool IsAvailable(std::string_view name) const override;
    std::unique_ptr<IModuleInterface> Create(std::string_view name,
                                             std::string* error) override;
    void Unload(std::string_view name) override;

    static DllModuleBackend& Global();
    static const char* HostConfigName();

private:
    std::optional<std::filesystem::path> FindDll(std::string_view name) const;

    std::vector<std::filesystem::path> m_roots;
    std::map<std::string, void*> m_handles;
};

// Exactly once per module, in that module's Private sources.
// Static libraries must be whole-archived into the host or the registrar is dropped.
// Modular plugin DLLs export LumaModuleApiVersion / LumaCreateModule / LumaDestroyModule.
#if defined(LUMA_MODULE_DLL)
#define LUMA_IMPLEMENT_MODULE(ClassName, NameLiteral)                                 \
    LUMA_MODULE_EXPORT std::uint32_t LumaModuleApiVersion() {                         \
        return static_cast<std::uint32_t>(                                            \
            ::Luma::Plugins::kModuleApiVersion);                                      \
    }                                                                                 \
    LUMA_MODULE_EXPORT ::Luma::Plugins::IModuleInterface* LumaCreateModule() {        \
        try {                                                                         \
            return new ClassName();                                                   \
        } catch (...) {                                                               \
            return nullptr;                                                           \
        }                                                                             \
    }                                                                                 \
    LUMA_MODULE_EXPORT void LumaDestroyModule(::Luma::Plugins::IModuleInterface* m) { \
        delete m;                                                                     \
    }
#else
#define LUMA_IMPLEMENT_MODULE(ClassName, NameLiteral)                            \
    namespace {                                                                  \
    [[maybe_unused]] const bool luma_module_reg_##ClassName =                    \
        ::Luma::Plugins::StaticModuleBackend::Global().Register(                 \
            NameLiteral, []() -> std::unique_ptr<::Luma::Plugins::IModuleInterface> { \
                return std::make_unique<ClassName>();                            \
            });                                                                  \
    }
#endif

}  // namespace Luma::Plugins
