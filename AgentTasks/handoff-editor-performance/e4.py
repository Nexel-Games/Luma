from edit import Ed
m = Ed('Editor/LumaEditor/main.cpp')
m.rep('''#include "Luma/Editor/ProjectBrowser.h"
''', '''#include "Luma/Editor/ProjectBrowser.h"
#include "Luma/Graphics/GraphicsSettings.h"
#include "Luma/Profiling/FrameLimiter.h"
#include "Luma/Profiling/Profiler.h"
''')
m.rep('''}  // namespace

int main(int argc, char** argv) {''', '''// Vulkan validation layers cost a lot of CPU per draw call, so they are on
// only in Debug builds, with --vulkan-validation, or LUMA_VULKAN_VALIDATION=1.
bool WantValidation(bool flag) {
    if (flag) return true;
    if (const char* env = std::getenv("LUMA_VULKAN_VALIDATION")) {
        return env[0] != '\\0' && env[0] != '0';
    }
#if defined(LUMA_CONFIG_DEBUG)
    return true;
#else
    return false;
#endif
}

// Applies the render scale to a viewport size in UI points: DPI-aware (the
// UI is laid out in points), clamped to 4K so a huge scale can't exhaust VRAM.
u32 ScaledViewportExtent(f32 points, f32 dpi, f32 renderScale, u32 maxPixels) {
    const f32 pixels = points * std::max(dpi, 1.0f) * renderScale + 0.5f;
    const u32 n = pixels > 1.0f ? static_cast<u32>(pixels) : 1u;
    return n < maxPixels ? n : maxPixels;
}

}  // namespace

int main(int argc, char** argv) {''')
m.rep('''    std::string previewPng;
    bool editorMode = false;''', '''    std::string previewPng;
    // --benchmark <frames>: measure that many frames (after --benchmark-warmup,
    // default 120), log the averaged stats, write --benchmark-out <json>, exit.
    int benchmarkFrames = 0;
    int benchmarkWarmup = 120;
    std::string benchmarkOut;
    std::string benchmarkLabel = "editor";
    bool validationFlag = false;
    bool editorMode = false;''')
m.rep('''        } else if (arg == "--preview-capture" && i + 2 < argc) {''', '''        } else if (arg == "--benchmark" && i + 1 < argc) {
            benchmarkFrames = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--benchmark-warmup" && i + 1 < argc) {
            benchmarkWarmup = std::max(0, std::atoi(argv[++i]));
        } else if (arg == "--benchmark-out" && i + 1 < argc) {
            benchmarkOut = argv[++i];
        } else if (arg == "--benchmark-label" && i + 1 < argc) {
            benchmarkLabel = argv[++i];
        } else if (arg == "--vulkan-validation") {
            validationFlag = true;
        } else if (arg == "--preview-capture" && i + 2 < argc) {''')
m.rep('''    RendererConfig rc;
    rc.appName = "Luma Editor";
    rc.vsync = true;
    std::unique_ptr<Renderer> renderer = CreateVulkanRenderer(*window, rc);''', '''    // Graphics settings the renderer needs at creation (present mode, texture
    // filtering). The editor's Project Settings pages own the live copy.
    Graphics::GraphicsSettings startupGraphics;
    if (editorMode && !projectFile.empty()) {
        const auto projectRoot = Luma::VFS::ProjectRootFromFile(projectFile);
        std::string error;
        if (!projectRoot.empty() && !Graphics::LoadFromConfigDir(projectRoot / "Config", startupGraphics, &error)) {
            LUMA_USER_WARN("Graphics", "{}; using default graphics settings", error);
        }
    }
    RendererConfig rc;
    rc.appName = "Luma Editor";
    rc.enableValidation = WantValidation(validationFlag);
    // The project browser stays on VSync (it is idle UI); the editor follows
    // the project's Display settings.
    rc.presentMode = editorMode ? Graphics::EffectivePresentMode(startupGraphics.display) : PresentMode::Fifo;
    rc.maxAnisotropy = startupGraphics.rendering.maxAnisotropy;
    rc.textureMipBias = startupGraphics.rendering.textureMipBias;
    std::unique_ptr<Renderer> renderer = CreateVulkanRenderer(*window, rc);''')
m.rep('''        editor->SetBackgroundServices(&jobs, &tasks);''', '''        editor->SetBackgroundServices(&jobs, &tasks);
        editor->SetActivePresentModeQuery([r = renderer.get()] {
            return std::string(Graphics::ToString(r->ActivePresentMode()));
        });''')
m.rep('''    if (!screenshotPath.empty()) splashTime = kSplashDuration;''', '''    if (!screenshotPath.empty() || benchmarkFrames > 0) splashTime = kSplashDuration;''')
m.rep('''    FrameClock clock;
    while (running && !window->ShouldClose()) {
        window->PollEvents();
        if (editor) {
            if (editor->CloseConfirmed()) break;
            window->SetShouldClose(false);  // closing waits for the prompt
        }
        Timestep dt = clock.Tick();
        // Main-thread job completions (GPU uploads, pipeline creation...) and
        // task bookkeeping, before this frame's UI reads them.
        jobs.RunMainThreadJobs(2.0);
        tasks.Update(static_cast<f64>(dt));
''', '''    FrameClock clock;
    Profiling::FrameLimiter limiter;
    Profiling::Profiler& profiler = Profiling::Global();
    int benchmarkFrame = 0;
    while (running && !window->ShouldClose()) {
        // Frame pacing first, so the wait counts in the frame time but not in
        // the CPU time. The browser relies on VSync instead.
        if (editor) {
            const auto& display = editor->GraphicsSettings().display;
            const bool focused = window->IsFocused() && !window->IsMinimized();
            limiter.Wait(benchmarkFrames > 0 ? 0.0
                                             : Graphics::EffectiveFrameRateLimit(display, focused));
        }
        profiler.BeginFrame();
        struct FrameEnd {
            ~FrameEnd() { Profiling::Global().EndFrame(); }
        } frameEnd;
        {
            LUMA_PROFILE_SCOPE("Poll Events");
            window->PollEvents();
        }
        if (editor) {
            if (editor->CloseConfirmed()) break;
            window->SetShouldClose(false);  // closing waits for the prompt
            // Live settings: a present-mode change rebuilds the swapchain
            // before the next frame (no restart).
            renderer->SetPresentMode(Graphics::EffectivePresentMode(editor->GraphicsSettings().display));
        }
        Timestep dt = clock.Tick();
        // Main-thread job completions (GPU uploads, pipeline creation...) and
        // task bookkeeping, before this frame's UI reads them.
        {
            LUMA_PROFILE_SCOPE("Main-Thread Jobs");
            jobs.RunMainThreadJobs(2.0);
            tasks.Update(static_cast<f64>(dt));
        }

        // Benchmark: warm up, then average a fixed number of frames.
        if (benchmarkFrames > 0 && editor && splashTime >= kSplashDuration) {
            if (benchmarkFrame == benchmarkWarmup) profiler.StartCapture();
            if (benchmarkFrame == benchmarkWarmup + benchmarkFrames) {
                profiler.StopCapture();
                const Profiling::FrameStats total = profiler.TotalAverage();
                LUMA_LOG_INFO("Benchmark", "[{}] {} frames (after {} warm-up)\\n{}", benchmarkLabel,
                              total.frames, benchmarkWarmup, Profiling::FormatReport(total));
                if (!benchmarkOut.empty() && !Profiling::WriteReportJson(total, benchmarkOut, benchmarkLabel)) {
                    LUMA_LOG_ERROR("Benchmark", "could not write {}", benchmarkOut);
                }
                running = false;
                break;
            }
            ++benchmarkFrame;
        }
''')
m.rep('''        if (editorMode && editor && splashTime >= kSplashDuration) {
            editor->TickSimulation(dt.Seconds());
            Slate::Rect vp = editor->ViewportRect();''', '''        if (editorMode && editor && splashTime >= kSplashDuration) {
            {
                LUMA_PROFILE_SCOPE("Simulation");
                editor->TickSimulation(dt.Seconds());
            }
            Slate::Rect vp = editor->ViewportRect();''')
m.rep('''                    // Render the 3D view at framebuffer density (and 1.5× on
                    // 96-DPI) so the panel samples a sharper image instead of
                    // stretching a low-res target. Aspect still matches vp.
                    const f32 dpi = window->ContentScale();
                    const f32 quality = dpi > 1.05f ? dpi : 1.5f;
                    const u32 rw = static_cast<u32>(vp.w * quality + 0.5f);
                    const u32 rh = static_cast<u32>(vp.h * quality + 0.5f);
                    const u32 viewW = rw > 0 ? (rw < 3840u ? rw : 3840u) : 1u;
                    const u32 viewH = rh > 0 ? (rh < 2160u ? rh : 2160u) : 1u;

                    Renderer2::DeferredSceneView deferredScene =
                        editor->BuildDeferredSceneView(viewW, viewH, dt.Seconds());

                    vulkanDeferredRenderer->SetViewportDimensions(
                        static_cast<i32>(viewW), static_cast<i32>(viewH));
                    vulkanDeferredRenderer->PrepareScene();
                    vulkanDeferredRenderer->RenderScene(deferredScene);''', '''                    // Render the 3D view at framebuffer density times the
                    // project's render scale (Rendering Quality page). Aspect
                    // still matches vp.
                    const auto& rendering = editor->GraphicsSettings().rendering;
                    const f32 dpi = window->ContentScale();
                    const u32 viewW = ScaledViewportExtent(vp.w, dpi, rendering.renderScale, 3840u);
                    const u32 viewH = ScaledViewportExtent(vp.h, dpi, rendering.renderScale, 2160u);

                    Renderer2::DeferredSceneView deferredScene;
                    {
                        LUMA_PROFILE_SCOPE("Scene View Build");
                        deferredScene = editor->BuildDeferredSceneView(viewW, viewH, dt.Seconds());
                    }

                    vulkanDeferredRenderer->SetViewportDimensions(
                        static_cast<i32>(viewW), static_cast<i32>(viewH));
                    vulkanDeferredRenderer->PrepareScene();
                    vulkanDeferredRenderer->RenderScene(deferredScene);''')
m.rep('''        if (!renderer->BeginFrame()) continue;
        ui.BeginFrame(''', '''        if (!renderer->BeginFrame()) continue;
        Profiling::CpuScope uiScope("UI (Slate)");
        ui.BeginFrame(''')
m.rep('''        renderer->DrawUI(ui.EndFrame());''', '''        {
            LUMA_PROFILE_SCOPE("UI Record");
            renderer->DrawUI(ui.EndFrame());
        }''')
m.rep('''        renderer->EndFrame();

        // Flush logs periodically to ensure they're written to disk
        Luma::Log::Flush();
''', '''        renderer->EndFrame();

        // Flush logs periodically to ensure they're written to disk
        {
            LUMA_PROFILE_SCOPE("Log Flush");
            Luma::Log::Flush();
        }
''')
m.save()
print('ok')
