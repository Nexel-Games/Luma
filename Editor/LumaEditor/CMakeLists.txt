# LumaEditor.exe - one binary, two modes (project browser + editor), connected
# by relaunching itself with --project (Godot's model).

add_executable(LumaEditor
    main.cpp
    SettingsWindow.cpp
    SplashScreen.cpp
    EditorScreen.cpp
    MaterialPreview.cpp)

# Panels live under Editor/panels/ and expose their headers via
# Editor/panels/<Name>/include plus the shared Editor/panels/PanelContext.h.
# LumaEditor needs the same include roots so it can include the public API.
target_include_directories(LumaEditor
    PRIVATE
        ${CMAKE_SOURCE_DIR}/Editor/panels
        ${CMAKE_SOURCE_DIR}/Engine/Rendering/Vulkan/include
        ${CMAKE_SOURCE_DIR}/Engine/Rendering/Vulkan/src
        ${CMAKE_SOURCE_DIR}/ThirdParty/stb)

target_link_libraries(LumaEditor
    PRIVATE Luma::Core Luma::Math Luma::Platform Luma::RenderVulkan Luma::Slate
            Luma::Project Luma::Plugins Luma::EditorPlugins Luma::CodeEditor Luma::ProjectBrowser Luma::Grid Luma::Scene
            Luma::Gizmo Luma::VFS Luma::Asset Luma::Renderer Luma::Mesh
            Luma::PanelWorldOutliner Luma::PanelViewport Luma::PanelInspector
            Luma::PanelConsole Luma::PanelContentBrowser Luma::PanelImportDialog
            Luma::Audio
            Luma::PanelFileSystemTree Luma::PanelBackgroundTasks Luma::Jobs Luma::Tasks
            Luma::PanelProjectSettings Luma::PanelPreferences
            Luma::Material Luma::AssetImport Luma::MaterialBridge Luma::PanelMaterialEditor
            Luma::MaterialEditors Luma::GraphEditor Luma::AssetWindows
            Luma::Profiling Luma::GraphicsSettings
            Vulkan::Vulkan)  # main.cpp drives VulkanDeferredRenderer directly

set_target_properties(LumaEditor PROPERTIES
    OUTPUT_NAME "LumaEditor"
    FOLDER "Editor")

# Stage the editor content tree (logos, fonts, splash assets) next to the
# executable so the binary is self-contained when launched outside the repo
# (CI, packaging, IDE F5, etc.). Keep the source of truth under Content/Editor.
add_custom_command(TARGET LumaEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/Content/Editor
        $<TARGET_FILE_DIR:LumaEditor>/Content/Editor
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/Content/Engine
        $<TARGET_FILE_DIR:LumaEditor>/Content/Engine
    COMMENT "Staging Content/Editor and Content/Engine next to LumaEditor.exe")
