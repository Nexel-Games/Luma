#include "Luma/Plugins/Module.h"

#include <algorithm>

#include "Luma/Core/Log.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Luma::Plugins {
namespace {

std::string Narrow(const std::filesystem::path& p) { return p.string(); }

#if defined(_WIN32)
std::string WinError(DWORD code) {
    char buf[512];
    const DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, code, 0, buf, sizeof(buf), nullptr);
    if (n == 0) return "Win32 error " + std::to_string(code);
    std::string text(buf, n);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.pop_back();
    }
    return text;
}
#endif

}  // namespace

const char* DllModuleBackend::HostConfigName() {
#if defined(LUMA_CONFIG_DEBUG)
    return "Debug";
#elif defined(LUMA_CONFIG_DEVELOPMENT)
    return "Development";
#elif defined(LUMA_CONFIG_RELEASE)
    return "Release";
#elif defined(LUMA_CONFIG_SHIPPING)
    return "Shipping";
#else
    return "Development";
#endif
}

void DllModuleBackend::SetSearchRoots(std::span<const std::filesystem::path> roots) {
    m_roots.assign(roots.begin(), roots.end());
}

std::optional<std::filesystem::path> DllModuleBackend::FindDll(std::string_view name) const {
    const std::string file = "LumaEditor-" + std::string(name) + ".dll";
    const std::string config = HostConfigName();
    for (const std::filesystem::path& root : m_roots) {
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (!entry.is_directory(ec)) continue;
            const std::filesystem::path bin = entry.path() / "Binaries" / "Win64";
            const std::filesystem::path candidates[] = {
                bin / config / file,
                bin / file,
            };
            for (const std::filesystem::path& path : candidates) {
                if (std::filesystem::is_regular_file(path, ec)) return path;
            }
        }
    }
    return std::nullopt;
}

bool DllModuleBackend::IsAvailable(std::string_view name) const {
    if (m_handles.find(std::string(name)) != m_handles.end()) return true;
    return FindDll(name).has_value();
}

std::unique_ptr<IModuleInterface> DllModuleBackend::Create(std::string_view name,
                                                           std::string* error) {
    auto fail = [&](std::string message) -> std::unique_ptr<IModuleInterface> {
        LUMA_USER_ERROR("Plugins", "{}", message);
        if (error) *error = std::move(message);
        return nullptr;
    };

#if !defined(_WIN32)
    return fail("DllModuleBackend is only implemented on Windows.");
#else
    const std::string key(name);
    if (m_handles.count(key)) {
        return fail(key + " is already loaded from a DLL. Shut it down first.");
    }
    const auto path = FindDll(name);
    if (!path) {
        return fail("module \"" + key +
                    "\" has no LumaEditor-" + key +
                    ".dll under a discovered plugin Binaries/Win64 folder. Rebuild the plugin.");
    }

    const std::wstring wpath = path->wstring();
    HMODULE handle = LoadLibraryExW(
        wpath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!handle) {
        return fail("could not load " + Narrow(*path) + " (" + WinError(GetLastError()) +
                    "). Put dependent DLLs next to the module or rebuild it.");
    }

    using VersionFn = u32 (*)();
    using CreateFn = IModuleInterface* (*)();
    auto version = reinterpret_cast<VersionFn>(GetProcAddress(handle, "LumaModuleApiVersion"));
    auto create = reinterpret_cast<CreateFn>(GetProcAddress(handle, "LumaCreateModule"));
    if (!version) {
        FreeLibrary(handle);
        return fail(key + " is missing export LumaModuleApiVersion. Rebuild the plugin for this engine.");
    }
    if (!create) {
        FreeLibrary(handle);
        return fail(key + " is missing export LumaCreateModule. Rebuild the plugin for this engine.");
    }
    const u32 api = version();
    if (api != kModuleApiVersion) {
        FreeLibrary(handle);
        return fail(key + " module API " + std::to_string(api) + " does not match engine " +
                    std::to_string(kModuleApiVersion) +
                    ". Rebuild the plugin for this engine version.");
    }

    IModuleInterface* instance = nullptr;
    try {
        instance = create();
    } catch (...) {
        FreeLibrary(handle);
        return fail(key + " threw in LumaCreateModule.");
    }
    if (!instance) {
        FreeLibrary(handle);
        return fail(key + " LumaCreateModule returned null.");
    }

    m_handles[key] = handle;
    LUMA_LOG_INFO("Plugins", "loaded DLL module {} from {}", key, Narrow(*path));
    return std::unique_ptr<IModuleInterface>(instance);
#endif
}

void DllModuleBackend::Unload(std::string_view name) {
#if defined(_WIN32)
    const std::string key(name);
    auto it = m_handles.find(key);
    if (it == m_handles.end()) return;
    HMODULE handle = static_cast<HMODULE>(it->second);
    m_handles.erase(it);
    if (handle) {
        using DestroyFn = void (*)(IModuleInterface*);
        auto destroy = reinterpret_cast<DestroyFn>(GetProcAddress(handle, "LumaDestroyModule"));
        (void)destroy;  // ModuleManager already destroyed the instance.
        FreeLibrary(handle);
        LUMA_LOG_INFO("Plugins", "unloaded DLL module {}", key);
    }
#else
    (void)name;
#endif
}

DllModuleBackend& DllModuleBackend::Global() {
    static DllModuleBackend backend;
    return backend;
}

}  // namespace Luma::Plugins
