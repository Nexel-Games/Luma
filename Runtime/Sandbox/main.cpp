#include <chrono>
#include <filesystem>
#include <memory>
#include "Luma/Graphics/GraphicsSettings.h"
#include "Luma/Profiling/FrameLimiter.h"
#include <string>
#include <thread>
#include <vector>

#include "Luma/Core/Application.h"
#include "Luma/Core/Config.h"
#include "Luma/Core/EngineLoop.h"
#include "Luma/Core/Events.h"
#include "Luma/Core/Log.h"
#include "Luma/Core/Types.h"
#include "Luma/Core/Version.h"
#include "Luma/Platform/Window.h"
#include "Luma/RHI/Renderer.h"
#include "Luma/RHI/VulkanRenderer.h"
#include "Luma/Asset/AssetPath.h"
#include "Luma/Plugins/ModuleManager.h"
#include "Luma/Plugins/PluginManager.h"
#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/SceneSystems.h"
#include "Luma/VFS/Path.h"
#include "Luma/VFS/VFS.h"

#include "SandboxLayer.h"

using namespace Luma;

namespace {

// Loads Engine.ini through the VFS. The Engine root is auto-detected at
// startup (env var, exe parent, or by walking up from CWD looking for the
// repo's CMakeLists.txt). Returns a default Config if the file is missing.
Config LoadEngineConfig() {
    Config cfg;
    auto& vfs = VFS::VFS::Global();
    Luma::VFS::Path iniPath(Luma::VFS::Root::Config, "Engine.ini");
    if (auto text = vfs.ReadText(iniPath)) {
        if (cfg.LoadFromString(*text)) {
            LUMA_LOG_INFO("Boot", "loaded config: {}", iniPath.ToString());
            return cfg;
        }
        LUMA_LOG_WARN("Boot", "failed to parse {}; using built-in defaults",
                      iniPath.ToString());
    } else {
        LUMA_LOG_WARN("Boot", "{} not found; using built-in defaults",
                      iniPath.ToString());
    }
    return cfg;
}

}  // namespace

int main(int argc, char** argv) {
    Log::Init(LogLevel::Trace);

    // Touch the VFS first so it mounts Engine/ before anything else needs it.
    auto& vfs = Luma::VFS::VFS::Global();
    LUMA_LOG_INFO("Boot", "engine root: {}",
                  vfs.IsMounted(Luma::VFS::Root::Engine)
                      ? vfs.RootPath(Luma::VFS::Root::Engine).string()
                      : "<unmounted>");

    // Make sure the log directory exists under the Saved root.
    if (vfs.IsMounted(Luma::VFS::Root::Saved)) {
        vfs.CreateDirectories(Luma::VFS::Path(Luma::VFS::Root::Saved, "Logs"));
    }
    Log::AddSink(Log::MakeFileSink("Saved/Logs/Luma.log"));

    LUMA_LOG_INFO("Boot", "starting {}", EngineVersionString());

    Graphics::GraphicsSettings graphics;
    if (argc > 1) {
        if (argc != 3 || std::string_view(argv[1]) != "--project" ||
            !std::filesystem::is_regular_file(argv[2])) {
            LUMA_LOG_ERROR("Boot", "Expected --project <existing project file>");
            return 2;
        }
        const auto root = VFS::ProjectRootFromFile(std::filesystem::absolute(argv[2]));
        vfs.Mount(VFS::Root::Project, root);
        std::string error;
        if (!Graphics::LoadFromConfigDir(root / "Config", graphics, &error))
            LUMA_LOG_WARN("Graphics", "{}", error);
    }

    Plugins::PluginManager plugins(Plugins::CompiledPluginTable::Global());
    Plugins::ModuleManager modules;
    AssetPathResolver assetPaths;
    {
        std::vector<std::filesystem::path> roots;
        if (vfs.IsMounted(VFS::Root::Engine)) {
            roots.push_back(vfs.RootPath(VFS::Root::Engine).parent_path() / "Plugins");
        }
        plugins.Discover(roots);
        std::vector<std::string> enabled;
        for (const auto& status : plugins.Statuses()) {
            if (status.manifest.enabledByDefault) enabled.push_back(status.manifest.id);
        }
        plugins.RestrictEnabled(enabled);
        if (vfs.IsMounted(VFS::Root::Content)) {
            if (auto real = vfs.TryResolve(VFS::Path(VFS::Root::Content, "Engine"))) {
                assetPaths.SetEngineRoot(*real);
            }
        }
        Plugins::PluginHostServices host;
        host.components = &Components();
        host.sceneSystems = &SceneSystems();
        plugins.SetHostServices(host);
        plugins.MountContentAndConfig(&assetPaths, nullptr);
        plugins.StartAll(Plugins::PluginHostKind::Runtime);
        modules.SetHostServices(host);
        modules.SetTarget(Plugins::PluginTarget::Game);
        modules.SetActivePlugins(plugins.LoadableDescriptors());
        modules.LoadAllPhases();
        plugins.ApplyModuleStatuses(modules);
    }

    Config cfg = LoadEngineConfig();

    ApplicationSpec spec;
    spec.name = cfg.GetString("Application.name", "Luma Engine");
    spec.width = static_cast<u32>(cfg.GetInt("Window.width", 1280));
    spec.height = static_cast<u32>(cfg.GetInt("Window.height", 720));

    Application app(spec);

    WindowProps props;
    props.title = cfg.GetString("Window.title", spec.name);
    props.width = spec.width;
    props.height = spec.height;

    std::unique_ptr<Window> window = Window::Create(props);

    RendererConfig rendererConfig;
    rendererConfig.appName = spec.name;
    if (argc == 1) graphics.display.vsync = cfg.GetBool("Window.vsync", true);
    rendererConfig.presentMode = Graphics::EffectivePresentMode(graphics.display);
    rendererConfig.maxAnisotropy = graphics.rendering.maxAnisotropy;
    rendererConfig.textureMipBias = graphics.rendering.textureMipBias;
    std::unique_ptr<Renderer> renderer =
        CreateVulkanRenderer(*window, rendererConfig);
    renderer->SetClearColor(ClearColor{0.10f, 0.12f, 0.16f, 1.0f});

    // Route events to the app, and feed resizes to the renderer.
    window->SetEventCallback([&app, &renderer](Event& e) {
        app.OnEvent(e);
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowResizeEvent>([&renderer](WindowResizeEvent& r) {
            renderer->OnResize(r.Width(), r.Height());
            return false;
        });
    });

    app.PushLayer(std::make_unique<SandboxLayer>());

    LUMA_LOG_INFO("Boot", "entering main loop");
    FrameClock clock;
    Profiling::FrameLimiter limiter;
    f32 fpsAccum = 0.0f;
    int frames = 0;
    while (app.IsRunning() && !window->ShouldClose()) {
        window->PollEvents();

        Timestep dt = clock.Tick();
        app.RunOneFrame(dt);

        if (renderer->BeginFrame()) {
            // The runtime renders no UI itself yet; the editor/browser drive
            // Luma Slate through renderer->DrawUI(). World rendering lands in M3+.
            renderer->EndFrame();
        }

        fpsAccum += dt.Seconds();
        ++frames;
        if (fpsAccum >= 1.0f) {
            LUMA_LOG_TRACE("Perf", "{} fps ({:.2f} ms/frame)", frames,
                           (fpsAccum / static_cast<f32>(frames)) * 1000.0f);
            fpsAccum = 0.0f;
            frames = 0;
        }

        limiter.Wait(Graphics::EffectiveFrameRateLimit(graphics.display,
                          window->IsFocused() && !window->IsMinimized()));
    }

    LUMA_LOG_INFO("Boot", "main loop exited; shutting down");
    modules.ShutdownAll();
    plugins.ShutdownAll();
    renderer->WaitIdle();
    renderer.reset();
    window.reset();
    Log::Shutdown();
    return 0;
}
