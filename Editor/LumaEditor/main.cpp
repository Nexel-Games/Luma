#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "Luma/Core/EngineLoop.h"
#include "Luma/Core/Events.h"
#include "Luma/Core/Log.h"
#include "Luma/Core/LogBuffer.h"
#include "Luma/Core/Types.h"
#include "Luma/Core/Registration.h"
#include "Luma/Editor/EditorIcons.h"
#include "Luma/Editor/MenuRegistry.h"
#include "Luma/Editor/ProjectBrowser.h"
#include "Luma/Graphics/GraphicsSettings.h"
#include "Luma/Profiling/FrameLimiter.h"
#include "Luma/Profiling/Profiler.h"
#include "Luma/Platform/Process.h"
#include "Luma/Platform/Sound.h"
#include "Luma/Platform/Window.h"
#include "Luma/RHI/Renderer.h"
#include "Luma/RHI/VulkanRenderer.h"
#include "Luma/RHI/VulkanRHIDevice.h"
#include "Luma/Rendering/Vulkan/VulkanDeferredRenderer.h"
#include "Luma/Renderer/DeferredSceneView.h"
#include "Luma/Renderer/SceneViewFingerprint.h"
#include <charconv>
#include <optional>
#include "Luma/Slate/Context.h"
#include "Luma/Slate/Image.h"
#include "Luma/Slate/Icons.h"
#include "Luma/VFS/Path.h"
#include "Luma/VFS/VFS.h"
#include "Luma/Audio/AudioPreview.h"
#include "Luma/Asset/ThumbnailRenderer.h"

#include "EditorScreen.h"
#include "Luma/Jobs/JobSystem.h"
#include "Luma/Tasks/TaskManager.h"
#include "SplashScreen.h"
#include "SettingsWindow.h"

#include <vulkan/vulkan.h>

using namespace Luma;

namespace {

// Resolves an editor asset (e.g. "Fonts/Inter-Regular.ttf") through the VFS
// using the Content root. Returns the absolute filesystem path or an empty
// string if the asset doesn't exist.
std::string FindEditorAsset(const std::string& file) {
    auto& vfs = Luma::VFS::VFS::Global();
    if (!vfs.IsMounted(Luma::VFS::Root::Content)) return {};
    Luma::VFS::Path p(Luma::VFS::Root::Content, "Editor/" + file);
    if (!vfs.Exists(p)) return {};
    auto real = vfs.TryResolve(p);
    if (!real) return {};
    return real->string();
}

// Feeds a window event into the Slate context (and reports window close).
void FeedEvent(Slate::Context& ui, Event& e, bool& running, EditorScreen* editor = nullptr) {
    EventDispatcher d(e);
    d.Dispatch<WindowCloseEvent>([&](WindowCloseEvent&) {
        // The editor may hold unsaved assets: it decides (possibly after a
        // Save / Don't Save / Cancel prompt) through CloseConfirmed().
        if (editor) {
            editor->RequestClose();
        } else {
            running = false;
        }
        return true;
    });
    d.Dispatch<WindowDropEvent>([&](WindowDropEvent& drop) {
        if (editor) {
            editor->HandleDroppedFiles(drop.Paths());
        }
        return true;
    });
    d.Dispatch<MouseMovedEvent>([&](MouseMovedEvent& m) {
        ui.OnMouseMove(m.X(), m.Y());
        return false;
    });
    d.Dispatch<MouseButtonPressedEvent>([&](MouseButtonPressedEvent& m) {
        ui.OnMouseButton(m.Button(), true);
        return false;
    });
    d.Dispatch<MouseButtonReleasedEvent>([&](MouseButtonReleasedEvent& m) {
        ui.OnMouseButton(m.Button(), false);
        return false;
    });
    d.Dispatch<MouseScrolledEvent>([&](MouseScrolledEvent& m) {
        ui.OnScroll(m.OffsetY());
        return false;
    });
    d.Dispatch<KeyTypedEvent>([&](KeyTypedEvent& k) {
        ui.OnText(k.Codepoint());
        return false;
    });
    d.Dispatch<KeyPressedEvent>([&](KeyPressedEvent& k) {
        ui.OnKey(k.Keycode(), true);
        return false;
    });
    d.Dispatch<KeyReleasedEvent>([&](KeyReleasedEvent& k) {
        ui.OnKey(k.Keycode(), false);
        return false;
    });
    d.Dispatch<WindowLostFocusEvent>([&](WindowLostFocusEvent&) {
        ui.ReleaseAllKeys();
        return false;
    });
}

// Vulkan validation layers cost a lot of CPU per draw call, so they are on
// only in Debug builds, with --vulkan-validation, or LUMA_VULKAN_VALIDATION=1.
bool WantValidation(bool flag) {
    if (flag) return true;
    if (const char* env = std::getenv("LUMA_VULKAN_VALIDATION")) {
        return env[0] != '\0' && env[0] != '0';
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

int main(int argc, char** argv) {
    Log::Init(LogLevel::Trace);  // installs the colored stdout sink
    // In-memory history for the editor's Output Log panel.
    auto logHistory = std::make_shared<LogBuffer>(5000);
    Log::AddSink(logHistory);
    
    // Use absolute path for log file to avoid VFS remounting issues
    std::error_code logEc;
    std::filesystem::path logDir = std::filesystem::absolute("Saved/Logs", logEc);
    std::filesystem::create_directories(logDir, logEc);
    std::filesystem::path logFile = logDir / "Editor.log";
    Log::AddSink(Log::MakeFileSink(logFile.string()));
    
    // Test logging immediately
    LUMA_LOG_INFO("Editor", "Logging system initialized");
    LUMA_LOG_INFO("Editor", "Log file: {}", logFile.string());
    
    // Flush logs to ensure immediate write
    Luma::Log::Flush();

    // Parse mode: --project <path.luma> opens the editor; otherwise the browser.
    std::filesystem::path projectFile;
    std::string screenshotPath;
    std::string selectName;  // --select <EntityName>: preselect for captures
    std::vector<std::string> menuCommands;  // --menu <EntryId>: run menu entries (captures)
    // --preview-capture <material virtual path> <png>: material preview capture
    std::string previewMaterial;
    std::string previewPng;
    // --benchmark <frames>: measure that many frames (after --benchmark-warmup,
    // default 120), log the averaged stats, write --benchmark-out <json>, exit.
    int benchmarkFrames = 0;
    int benchmarkWarmup = 120;
    std::string benchmarkOut;
    std::string benchmarkLabel = "editor";
    bool validationFlag = false;
    bool editorMode = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--project" && i + 1 < argc) {
            projectFile = argv[++i];
            editorMode = true;
        } else if (arg == "--project-manager") {
            editorMode = false;
        } else if (arg == "--screenshot" && i + 1 < argc) {
            screenshotPath = argv[++i];
        } else if (arg == "--select" && i + 1 < argc) {
            selectName = argv[++i];
        } else if (arg == "--menu" && i + 1 < argc) {
            menuCommands.emplace_back(argv[++i]);
        } else if (arg == "--benchmark" || arg == "--benchmark-warmup") {
            int& value = arg == "--benchmark" ? benchmarkFrames : benchmarkWarmup;
            const std::string_view input = i + 1 < argc ? argv[++i] : "";
            const auto end = input.data() + input.size();
            const auto parsed = std::from_chars(input.data(), end, value);
            if (parsed.ec != std::errc{} || parsed.ptr != end || value < (arg == "--benchmark" ? 1 : 0)) {
                LUMA_LOG_ERROR("Benchmark", "{} requires a valid frame count", arg);
                return 2;
            }
        } else if (arg == "--benchmark-out" && i + 1 < argc) {
            benchmarkOut = argv[++i];
        } else if (arg == "--benchmark-label" && i + 1 < argc) {
            benchmarkLabel = argv[++i];
        } else if (arg == "--vulkan-validation") {
            validationFlag = true;
        } else if (arg == "--preview-capture" && i + 2 < argc) {
            previewMaterial = argv[++i];
            previewPng = argv[++i];
        }
    }

    if (benchmarkFrames > 0 && (!editorMode || !std::filesystem::is_regular_file(projectFile))) {
        LUMA_LOG_ERROR("Benchmark", "--benchmark requires --project <existing file.luma>");
        return 2;
    }

    // Touch the VFS and (in editor mode) remount Project/Saved/Intermediate
    // under the active project so project-relative content and logs resolve
    // through the same paths.
    {
        auto& vfs = Luma::VFS::VFS::Global();
        if (editorMode && !projectFile.empty()) {
            auto projectRoot = Luma::VFS::ProjectRootFromFile(projectFile);
            if (!projectRoot.empty()) {
                vfs.Mount(Luma::VFS::Root::Project, projectRoot);
                // Scratch data (logs, screenshots, caches) lives in the
                // project's Intermediate/ folder at the project root — both
                // the legacy "Saved" and "Intermediate" roots resolve there.
                vfs.Mount(Luma::VFS::Root::Saved,
                          projectRoot / "Intermediate");
                vfs.Mount(Luma::VFS::Root::Intermediate,
                          projectRoot / "Intermediate");
                LUMA_LOG_INFO("Editor", "mounted project at {}",
                              projectRoot.string());
            }
        }
    }

    WindowProps props;
    props.title = editorMode ? "Luma Editor" : "Luma - Project Browser";
    props.width = editorMode ? 1360u : 900u;
    props.height = editorMode ? 820u : 640u;
    std::unique_ptr<Window> window = Window::Create(props);

    // Graphics settings the renderer needs at creation (present mode, texture
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
    std::unique_ptr<Renderer> renderer = CreateVulkanRenderer(*window, rc);

    ThumbnailManager::Get().Initialize();

    // Fonts: Red Hat Display (variable font) is the primary UI family. The variable font
    // supports multiple weights through a single file. We'll use the same variable font
    // for all weights and let the renderer handle the weight variations.
    //   uiRegular -> body / default text
    //   uiMedium  -> captions / hint text / weaker emphasis
    //   uiSemiBold-> section headings, tab titles, button labels
    //   uiBold    -> titles, splash, large display
    //   mono      -> console output, logs, code
    // Red Hat Display ships in Content/Editor/Fonts; if it can't be found
    // (or a file is missing) fall back to the system UI font.
    auto firstExisting = [](std::initializer_list<std::string> candidates) {
        for (const std::string& c : candidates) {
            std::error_code ec;
            if (!c.empty() && std::filesystem::exists(c, ec)) return c;
        }
        return std::string{};
    };
    std::string systemFonts = "C:/Windows/Fonts/";
    if (const char* windir = std::getenv("WINDIR")) {
        systemFonts = std::filesystem::path(windir).append("Fonts").generic_string() + "/";
    }
    const std::string redHatFont =
        FindEditorAsset("Fonts/RedHatDisplay/RedHatDisplay-VariableFont_wght.ttf");
    const std::string bodyFont = firstExisting({redHatFont, systemFonts + "segoeui.ttf"});
    const std::string mediumFont = bodyFont;
    const std::string uiFont = bodyFont;
    const std::string titleFont = bodyFont;
    const std::string monoFont = firstExisting({systemFonts + "consola.ttf", bodyFont});
    if (bodyFont.empty()) {
        LUMA_LOG_ERROR("Editor", "No UI font found (looked for Content/Editor/Fonts/RedHatDisplay "
                                 "and {}segoeui.ttf)", systemFonts);
        Log::Flush();
        return 1;
    }
    if (redHatFont.empty()) {
        LUMA_LOG_WARN("Editor", "Red Hat Display not found in Content/Editor/Fonts; using {}", bodyFont);
    }

    Slate::Context ui;  // owns the loaded fonts + theme tokens
    // Build the typography scale on the theme. Sizes are tuned for a 96 DPI
    // baseline; will scale crisply with the renderer's DPI factor.
    Slate::Typography& type = ui.theme().type;
    type.uiRegular = bodyFont;
    type.uiMedium = mediumFont;
    type.uiSemiBold = uiFont;
    type.uiBold = titleFont;
    type.mono = monoFont;
    // Further increased font sizes for maximum quality and readability
    type.captionSize = 15.0f;   // increased from 14.0f
    type.bodySize = 18.0f;      // increased from 16.0f
    type.bodyStrongSize = 18.0f; // increased from 16.0f
    type.headingSize = 18.0f;   // increased from 16.0f
    type.titleSize = 28.0f;     // increased from 26.0f
    // Card titles (bold) and their secondary lines (semibold), e.g. the Plugins
    // browser: the real Red Hat Display weights, sized above the 18px body.
    type.headlineFont = firstExisting({FindEditorAsset("Fonts/RedHatDisplay/static/RedHatDisplay-Bold.ttf"), titleFont});
    type.subtitleFont =
        firstExisting({FindEditorAsset("Fonts/RedHatDisplay/static/RedHatDisplay-SemiBold.ttf"), uiFont});
    type.headlineSize = 24.0f;
    type.subtitleSize = 20.0f;
    type.displaySize = 38.0f;   // increased from 36.0f
    ui.SetClipboardHandlers([&window] { return window->GetClipboardText(); },
                            [&window](const std::string& text) { window->SetClipboardText(text); });
    if (!ui.Init(*renderer, type, window->ContentScale())) {
        // Without a font every label is empty and layout breaks: stop here
        // with a clear message rather than running a broken UI.
        LUMA_LOG_ERROR("Editor", "failed to load the UI fonts ({}, {})", bodyFont, monoFont);
        Log::Flush();
        return 1;
    }
    if (!Slate::LoadIconAtlas(ui, *renderer, FindEditorAsset("Icons/SlateIcons.png"))) {
        LUMA_USER_ERROR("Editor", "Missing or invalid editor icon pack: Icons/SlateIcons.png");
    }
    // Window icon from the engine icon asset.
    {
        std::string iconPath = FindEditorAsset("luma_icon.png");
        if (!iconPath.empty()) {
            std::vector<u8> pixels;
            const Slate::Image icon = Slate::LoadImage(*renderer, iconPath, &pixels);
            if (!pixels.empty()) window->SetIcon(icon.width, icon.height, pixels.data());
            ui.SetBrandIcon(icon);
        }
    }
    // Logo for the banner / splash.
    Slate::Image logo = {};
    {
        std::string logoPath = FindEditorAsset("luma_logo.png");
        if (!logoPath.empty()) logo = Slate::LoadImage(*renderer, logoPath);
    }

    bool running = true;
    // Engine services: declared before the editor so they outlive it.
    Jobs::JobSystem jobs;
    Tasks::TaskManager tasks;
    // Named editor images (released after the editor that draws them).
    RegistrationSet editorImages;
    std::unique_ptr<EditorScreen> editor;
    window->SetEventCallback(
        [&](Event& e) { FeedEvent(ui, e, running, editor.get()); });

    // Template thumbnails.
    ProjectBrowser browser;
    browser.SetLogo(logo);
    struct TemplateAsset {
        GameTemplate value;
        const char* file;
    };
    const TemplateAsset kTemplateAssets[] = {
        {GameTemplate::Empty, "Templates/Empty.png"},
        {GameTemplate::FirstPerson, "Templates/FirstPerson.png"},
        {GameTemplate::ThirdPerson, "Templates/ThirdPerson.png"},
        {GameTemplate::TopDown, "Templates/TopDown.png"},
    };
    for (const TemplateAsset& asset : kTemplateAssets) {
        std::string path = FindEditorAsset(asset.file);
        if (!path.empty()) {
            browser.SetTemplateThumbnail(asset.value,
                                         Slate::LoadImage(*renderer, path));
        }
    }

    // Editor toolbar icons.
    auto loadIcon = [&](const char* file) -> TextureHandle {
        std::string path = FindEditorAsset(file);
        if (path.empty()) return 0;
        const TextureHandle texture = Slate::LoadImage(*renderer, path).texture;
        // Luma icon pack artwork is multi-colour: panels' tints must not darken it.
        if (Slate::IsLumaIconPath(file)) Slate::MarkFullColorTexture(texture);
        return texture;
    };
    TextureHandle iconPlay = loadIcon("Luma_Icons/PNG/32x32/01_Main_Editor_Toolbar/001_play.png");
    TextureHandle iconPause = loadIcon("Luma_Icons/PNG/32x32/01_Main_Editor_Toolbar/002_pause.png");
    TextureHandle iconStop = loadIcon("Luma_Icons/PNG/32x32/01_Main_Editor_Toolbar/003_stop.png");
    // Content Browser chrome icons (sort arrows + search glass + folder +
    // reload + import + open-folder). Folders use the Luma icon pack; the
    // other Content Browser icons keep their classic art for now.
    TextureHandle cbSortUp = loadIcon("Luma_Icons_V2/PNG/32x32/016_sort_ascending.png");
    TextureHandle cbSortDown = loadIcon("Luma_Icons_V2/PNG/32x32/017_sort_descending.png");
    TextureHandle cbSearchGlass = loadIcon("Icons/search_glass.png");
    TextureHandle cbFolder = loadIcon("Luma_Icons/PNG/128x128/04_Content_Browser_Assets/061_folder.png");
    TextureHandle cbReload = loadIcon("Icons/reload.png");
    TextureHandle cbImport = loadIcon("Icons/import.png");
    TextureHandle cbOpenFolder = loadIcon("Luma_Icons_V2/PNG/128x128/024_open_folder.png");
    TextureHandle cbExpandArrow = loadIcon("Luma_Icons_V2/PNG/32x32/001_chevron_right.png");
    TextureHandle cbRetractArrow = loadIcon("Luma_Icons_V2/PNG/32x32/002_chevron_down.png");

    // World Outliner icons (Create-menu + actor-row textures).
    TextureHandle olCreateButton = loadIcon("Luma_Icons/PNG/32x32/01_Main_Editor_Toolbar/017_add.png");
    TextureHandle olSearchGlass = loadIcon("Luma_Icons/PNG/32x32/01_Main_Editor_Toolbar/016_search.png");
    TextureHandle olCatGeometry = loadIcon("Luma_Icons_V2/PNG/32x32/031_geometry_shapes.png");
    TextureHandle olCatLight = loadIcon("Luma_Icons_V2/PNG/32x32/030_generic_light.png");
    TextureHandle olCatEnvironment = loadIcon("Luma_Icons_V2/PNG/32x32/032_environment_sky.png");
    TextureHandle olActorCube = loadIcon("Luma_Icons/PNG/32x32/03_Scene_Objects_Actors/042_cube.png");
    TextureHandle olActorPlane = loadIcon("Luma_Icons/PNG/32x32/03_Scene_Objects_Actors/046_plane.png");
    TextureHandle olActorSphere = loadIcon("Luma_Icons/PNG/32x32/03_Scene_Objects_Actors/043_sphere.png");
    TextureHandle olActorCylinder = loadIcon("Luma_Icons/PNG/32x32/03_Scene_Objects_Actors/045_cylinder.png");
    TextureHandle olActorDirLight =
        loadIcon("Luma_Icons/PNG/32x32/03_Scene_Objects_Actors/050_directional_light.png");
    TextureHandle olActorPointLight =
        loadIcon("Luma_Icons/PNG/32x32/03_Scene_Objects_Actors/051_point_light.png");
    TextureHandle olActorSpotLight = loadIcon("Luma_Icons/PNG/32x32/03_Scene_Objects_Actors/052_spot_light.png");
    TextureHandle olActorMesh = loadIcon("Luma_Icons/PNG/32x32/03_Scene_Objects_Actors/047_static_mesh.png");

    // Unreal icon pack (Content/Editor/Icons/ue).
    EditorScreen::IconSet ueIcons;
    ueIcons.tabOutliner = loadIcon("Luma_Icons_V2/PNG/32x32/018_world_outliner_tab.png");
    ueIcons.tabDetails = loadIcon("Luma_Icons_V2/PNG/32x32/019_details_inspector_tab.png");
    ueIcons.tabViewport = loadIcon("Luma_Icons_V2/PNG/32x32/028_viewport_tab.png");
    ueIcons.tabContent = loadIcon("Luma_Icons_V2/PNG/32x32/029_content_browser_tab.png");
    ueIcons.tabOutputLog = loadIcon("Luma_Icons/PNG/32x32/10_Debugging_Engine_Systems/142_output_log.png");
    // The Luma plugin icon (Content/Editor/Luma_Icons/Plugin): the Plugins tab and
    // the card icon of plugins without their own Resources/Icon128.png.
    ueIcons.tabPlugins = loadIcon("Luma_Icons/Plugin/luma_plugin_32x32.png");
    ueIcons.pluginCard = loadIcon("Luma_Icons/Plugin/luma_plugin_64x64.png");
    // The Code Editor (Luma_Icons/CodeEditor): its tab and the Tools menu entry.
    ueIcons.tabCode = loadIcon("Luma_Icons/CodeEditor/luma_code_32x32.png");
    // The Luma logo (window headers, e.g. Plugins): full colour, never tinted.
    ueIcons.brandLogo = loadIcon("luma_icon.png");
    if (ueIcons.brandLogo) Slate::MarkFullColorTexture(ueIcons.brandLogo);
    ueIcons.save = loadIcon("Luma_Icons/PNG/32x32/01_Main_Editor_Toolbar/004_save.png");
    ueIcons.error = loadIcon("Luma_Icons/PNG/32x32/10_Debugging_Engine_Systems/144_error.png");
    ueIcons.warning = loadIcon("Luma_Icons/PNG/32x32/10_Debugging_Engine_Systems/143_warning.png");
    ueIcons.viewport.translate = loadIcon("Luma_Icons/PNG/32x32/02_Viewport_Transform/022_move.png");
    ueIcons.viewport.rotate = loadIcon("Luma_Icons/PNG/32x32/02_Viewport_Transform/023_rotate.png");
    ueIcons.viewport.scale = loadIcon("Luma_Icons/PNG/32x32/02_Viewport_Transform/024_scale.png");
    ueIcons.viewport.worldSpace = loadIcon("Luma_Icons/PNG/32x32/02_Viewport_Transform/027_world_space.png");
    ueIcons.viewport.localSpace = loadIcon("Luma_Icons/PNG/32x32/02_Viewport_Transform/026_local_space.png");
    ueIcons.viewport.gridSnap = loadIcon("Luma_Icons/PNG/32x32/02_Viewport_Transform/028_snap_to_grid.png");
    ueIcons.viewport.cameraSpeed = loadIcon("Luma_Icons/PNG/32x32/02_Viewport_Transform/040_camera_speed.png");
    ueIcons.viewport.perspective = loadIcon("Luma_Icons/PNG/32x32/02_Viewport_Transform/031_perspective_view.png");
    ueIcons.viewport.lit = loadIcon("Luma_Icons/PNG/32x32/05_Rendering_Materials/082_lit_view.png");
    ueIcons.viewport.unlit = loadIcon("Luma_Icons/PNG/32x32/05_Rendering_Materials/083_unlit_view.png");
    ueIcons.viewport.show = loadIcon("Luma_Icons_V2/PNG/32x32/010_eye.png");

    SplashScreen splash;

    // Initialize Vulkan deferred renderer (editor mode only)
    void* deferredRendererPtr = renderer->GetVulkanDeferredRenderer();
    if (deferredRendererPtr) {
        LUMA_LOG_INFO("Editor", "Vulkan deferred renderer available");
    } else {
        LUMA_LOG_WARN("Editor", "Vulkan deferred renderer not available");
    }
    Log::Flush();
    Audio::AudioPreview audioPreview;
    if (editorMode) {
        std::string audioError;
        audioPreview.Init(&audioError);
        editor = std::make_unique<EditorScreen>(projectFile);
        editor->SetAudioPreview(&audioPreview);
        editor->SetLogHistory(logHistory);
        editor->SetRenderer(renderer.get());
        editor->SetBackgroundServices(&jobs, &tasks);
        editor->SetActivePresentModeQuery([r = renderer.get()] {
            return std::string(Graphics::ToString(r->ActivePresentMode()));
        });
        if (!previewMaterial.empty() &&
            !editor->CapturePreview(previewMaterial, previewPng, 256)) {
            LUMA_LOG_ERROR("Editor", "--preview-capture failed for '{}'", previewMaterial);
        }
        for (const std::string& command : menuCommands) {
            std::string error;
            if (!Editor::Menus().Execute(command, &error)) {
                LUMA_LOG_WARN("Editor", "--menu: {}", error);
            }
        }
        if (!selectName.empty() && !editor->SelectEntityByName(selectName)) {
            LUMA_LOG_WARN("Editor", "--select: no entity named '{}'", selectName);
        }
        editor->SetToolbarIcons(iconPlay, iconPause, iconStop);
        editor->SetEditorIcons(ueIcons);
        editor->SetContentBrowserIcons(cbSortUp, cbSortDown, cbSearchGlass,
                                       cbFolder, cbReload, cbImport,
                                       cbOpenFolder, cbExpandArrow,
                                       cbRetractArrow);
        // World Outliner icons: Create-menu header button + search glass +
        // category rows + primitive rows + actor-row icons beside entity
        // names. Zero handles fall back to pack glyphs.
        editor->SetCreateButtonIcon(olCreateButton);
        editor->SetSearchGlassIcon(olSearchGlass);
        // Actor / category images by name (registries refer to icons by name;
        // a missing texture falls back to the glyph).
        struct NamedImage {
            const char* name;
            TextureHandle texture;
            Slate::Icon fallback;
        };
        const NamedImage images[] = {
            {"CategoryGeometry", olCatGeometry, Slate::Icon::Cube},
            {"CategoryLight", olCatLight, Slate::Icon::Light},
            {"CategoryEnvironment", olCatEnvironment, Slate::Icon::Sphere},
            {"ActorCube", olActorCube, Slate::Icon::Cube},
            {"ActorPlane", olActorPlane, Slate::Icon::Plane},
            {"ActorSphere", olActorSphere, Slate::Icon::Sphere},
            {"ActorCylinder", olActorCylinder, Slate::Icon::Cylinder},
            {"ActorDirLight", olActorDirLight, Slate::Icon::Light},
            {"ActorPointLight", olActorPointLight, Slate::Icon::Light},
            {"ActorSpotLight", olActorSpotLight, Slate::Icon::Light},
            {"ActorMesh", olActorMesh, Slate::Icon::Cube},
            {"Plugins", ueIcons.tabPlugins, Slate::Icon::Gear},  // Edit > Plugins
            {"CodeEditor", ueIcons.tabCode, Slate::Icon::Script},  // Tools > Code Editor
        };
        for (const NamedImage& image : images) {
            editorImages.Add(Editor::EditorImages().Register(image.name, image.texture,
                                                             image.fallback, "Engine"));
        }
    }

    // Splash phase (editor mode only).
    constexpr f32 kSplashDuration = 1.6f;
    f32 splashTime = 0.0f;
    // For a screenshot, skip the splash and capture after the UI settles.
    if (!screenshotPath.empty() || benchmarkFrames > 0) splashTime = kSplashDuration;
    int frameCount = 0;
    const int kCaptureFrame = 30;

    LUMA_LOG_INFO("Editor", "starting in {} mode",
                  editorMode ? "editor" : "project-browser");

    SettingsWindow projectSettings(EditorScreen::SettingsKind::Project, FeedEvent);
    SettingsWindow preferences(EditorScreen::SettingsKind::Preferences, FeedEvent);
    const auto settingsIcon = FindEditorAsset("luma_icon.png");
    const auto settingsAtlas = FindEditorAsset("Icons/SlateIcons.png");
    FrameClock clock;
    Profiling::FrameLimiter limiter;
    Profiling::Profiler& profiler = Profiling::Global();
    auto lastFlush = Profiling::Clock::now();
    std::optional<u64> lastView;
    Profiling::Clock::time_point settleUntil{};
    bool cameraPreviewActive = false;  // a camera preview snapshot is live
    i64 benchmarkFrame = 0;
    int exitCode = 0;
    while (running && !window->ShouldClose()) {
        // Frame pacing first, so the wait counts in the frame time but not in
        // the CPU time. The browser relies on VSync instead.
        if (editor) {
            const auto& display = editor->GraphicsSettings().display;
            const bool focused = (window->IsFocused() && !window->IsMinimized()) ||
                                 projectSettings.Focused() || preferences.Focused();
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
            if (editor->CloseConfirmed()) {
                if (editor->WantsRelaunch() && !editor->ProjectFile().empty()) {
                    LaunchDetached(ExecutablePath(),
                                   {"--project", editor->ProjectFile().string()});
                }
                break;
            }
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
            if (benchmarkFrame == static_cast<i64>(benchmarkWarmup) + benchmarkFrames) {
                profiler.StopCapture();
                const Profiling::FrameStats total = profiler.TotalAverage();
                LUMA_LOG_INFO("Benchmark", "[{}] {} frames (after {} warm-up)\n{}", benchmarkLabel,
                              total.frames, benchmarkWarmup, Profiling::FormatReport(total));
                if (!benchmarkOut.empty() && !Profiling::WriteReportJson(total, benchmarkOut, benchmarkLabel)) {
                    exitCode = 1;
                    LUMA_LOG_ERROR("Benchmark", "could not write {}", benchmarkOut);
                }
                running = false;
                break;
            }
            ++benchmarkFrame;
        }

        // Render the 3D scene into the editor viewport (using last frame's rect),
        // before opening the swapchain frame.
        if (editorMode && editor && splashTime >= kSplashDuration) {
            {
                LUMA_PROFILE_SCOPE("Simulation");
                editor->TickSimulation(dt.Seconds());
            }
            Slate::Rect vp = editor->ViewportRect();
            if (vp.w > 4.0f && vp.h > 4.0f) {
                // Use ONLY Vulkan deferred renderer for viewport
                if (deferredRendererPtr) {
                    auto* vulkanDeferredRenderer = static_cast<Rendering::VulkanDeferredRenderer*>(deferredRendererPtr);
                    
                    // Render the 3D view at framebuffer density times the
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
                    vulkanDeferredRenderer->SetQuality(Graphics::ToRenderQuality(rendering));
                    bool needsRender = editor->GraphicsSettings().display.realtimeViewport;
                    if (needsRender) lastView.reset();
                    else {
                        const auto signature = Renderer2::SceneViewFingerprint(deferredScene, vulkanDeferredRenderer->ContentRevision());
                        const auto now = Profiling::Clock::now();
                        if (lastView != signature) {
                            lastView = signature;
                            settleUntil = now + std::chrono::seconds(1);
                            needsRender = true;
                        }
                        needsRender |= now < settleUntil;
                    }
                    // Selected-camera preview: render the camera's view at the
                    // same resolution (a different size would rebuild all
                    // targets) INTO a snapshot, BEFORE the main render so the
                    // live buffer ends on the main view. Rides needsRender, so
                    // it re-renders on any scene/selection change and holds its
                    // snapshot while nothing changes.
                    Renderer2::DeferredSceneView previewScene;
                    std::string cameraName;
                    const bool hasPreview =
                        editor->BuildCameraPreviewView(previewScene, viewW, viewH, &cameraName);
                    if (hasPreview && needsRender) {
                        vulkanDeferredRenderer->SetSnapshotTarget(EditorScreen::kCameraPreviewKey);
                        vulkanDeferredRenderer->RenderScene(previewScene);
                        vulkanDeferredRenderer->SetSnapshotTarget(0);
                    }

                    if (needsRender) vulkanDeferredRenderer->RenderScene(deferredScene);

                    // The deferred renderer owns the UI texture handle for its
                    // light-accumulation buffer (re-pointed internally on resize).
                    TextureHandle sceneTex = vulkanDeferredRenderer->GetViewportTextureHandle();
                    if (sceneTex != 0) {
                        editor->SetViewportTexture(sceneTex);
                    }

                    // Hand the camera preview snapshot to the viewport inset, or
                    // hide + free it when no camera is selected.
                    if (hasPreview) {
                        editor->SetCameraPreview(
                            vulkanDeferredRenderer->SnapshotTexture(EditorScreen::kCameraPreviewKey),
                            cameraName);
                        cameraPreviewActive = true;
                    } else {
                        editor->SetCameraPreview(0, "");
                        if (cameraPreviewActive) {
                            vulkanDeferredRenderer->ReleaseSnapshot(EditorScreen::kCameraPreviewKey);
                            cameraPreviewActive = false;
                        }
                    }

                } else {
                    LUMA_LOG_ERROR("Editor", "Vulkan deferred renderer not available - viewport will be black");
                }
            }
        }

        if (editor) {
            projectSettings.Tick(*editor, type, rc.enableValidation, settingsIcon, settingsAtlas, dt.Seconds());
            preferences.Tick(*editor, type, rc.enableValidation, settingsIcon, settingsAtlas, dt.Seconds());
        }
        if (!renderer->BeginFrame()) continue;
        Profiling::CpuScope uiScope("UI (Slate)");
        // While the editor is still loading, every click is refused (with the
        // system alert) instead of reaching half-initialised UI.
        ui.SetInputLocked(editorMode && splashTime < kSplashDuration);
        ui.BeginFrame(static_cast<f32>(window->Width()),
                      static_cast<f32>(window->Height()), dt);

        f32 w = static_cast<f32>(window->Width());
        f32 h = static_cast<f32>(window->Height());

        if (editorMode) {
            if (splashTime < kSplashDuration) {
                splashTime += dt;
                f32 progress = splashTime / kSplashDuration;
                const char* msg = "Starting editor...";
                if (progress < 0.3f) msg = "Initializing Vulkan...";
                else if (progress < 0.6f) msg = "Loading project...";
                else if (progress < 0.85f) msg = "Compiling shaders...";
                splash.Draw(ui, w, h, progress, msg, logo);
            } else {
                editor->Draw(ui, w, h);
            }
        } else {
            BrowserResult result = browser.Draw(ui, w, h);
            if (result.launch) {
                // Godot-style: relaunch this exe in editor mode, then exit.
                LUMA_LOG_INFO("Editor", "opening project: {}",
                              result.projectFile.string());
                LaunchDetached(ExecutablePath(),
                               {"--project", result.projectFile.string()});
                running = false;
            }
        }

        {
            LUMA_PROFILE_SCOPE("UI Record");
            renderer->DrawUI(ui.EndFrame());
        }
        // A click a modal dialog (or loading) refused: the system alert.
        if (ui.TakeBlockedClicks() > 0) PlayAlertSound();
        window->SetCursor(ui.RequestedCursor());
        // Spin-box scrubbing: hide + lock the pointer while a drag wants it;
        // when the drag ends, unlock and put the cursor back where it began.
        if (auto restore = ui.TakePointerRestore()) {
            window->SetPointerLocked(false);
            window->SetCursorPosition(restore->x, restore->y);
            ui.WarpMouse(restore->x, restore->y);
        } else {
            window->SetPointerLocked(ui.PointerLockRequested());
        }

        if (!screenshotPath.empty() && frameCount == kCaptureFrame) {
            renderer->CaptureFrame(screenshotPath);
        }
        renderer->EndFrame();

        // Flush logs periodically to ensure they're written to disk
        if (Profiling::Clock::now() - lastFlush >= std::chrono::seconds(1)) {
            lastFlush = Profiling::Clock::now();
            LUMA_PROFILE_SCOPE("Log Flush");
            Luma::Log::Flush();
        }

        if (!screenshotPath.empty() && frameCount >= kCaptureFrame) {
            running = false;  // captured; exit
        }
        ++frameCount;
    }

    renderer->WaitIdle();
    audioPreview.Shutdown();
    editor.reset();
    renderer.reset();
    window.reset();
    
    // Flush logs before shutdown
    Log::Flush();
    
    Log::Shutdown();
    return exitCode;
}
