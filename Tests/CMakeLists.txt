# Luma automated tests (Catch2 v3, run via ctest).

luma_require_catch2()

add_subdirectory(Editor)

add_executable(LumaTests
    Core/AssertTests.cpp
    Core/LogTests.cpp
    Core/LogBufferTests.cpp
    Core/ConfigTests.cpp
    Core/EventTests.cpp
    Core/LayerStackTests.cpp
    Core/ApplicationTests.cpp
    Core/MemoryTests.cpp
    Core/VersionTests.cpp
    Core/RegistrationTests.cpp
    Jobs/JobSystemTests.cpp
    Material/GraphTests.cpp
    Material/ExpressionRegistryTests.cpp
    Material/MaterialAssetsV4Tests.cpp
    Material/CompilerCoreTests.cpp
    Material/ExpressionCompileTests.cpp
    Material/PreshaderTests.cpp
    Material/FunctionCompileTests.cpp
    Material/CompileCoverageTests.cpp
    Rendering/ShaderCompilerTests.cpp
    Rendering/VulkanMaterialLayoutTests.cpp
    Rendering/MaterialRenderTests.cpp
    Material/MaterialMigrationV4Tests.cpp
    Tasks/TaskManagerTests.cpp
    Project/ProjectTests.cpp
    Project/ProjectFileTests.cpp
    Plugins/ManifestTests.cpp
    Plugins/DiscoveryTests.cpp
    Plugins/LifecycleTests.cpp
    Plugins/PersistenceTests.cpp
    Plugins/DescriptorTests.cpp
    Plugins/ResolutionTests.cpp
    Plugins/ModuleTests.cpp
    Plugins/SamplePluginTests.cpp
    Plugins/ContentMountTests.cpp
    Plugins/DllModuleTests.cpp
    Plugins/WizardTests.cpp
    Plugins/PackagingTests.cpp
    Serialization/SerialValueTests.cpp
    Serialization/JsonTests.cpp
    Serialization/ReflectionTests.cpp
    Scene/SceneSerializerTests.cpp
    Scene/PrefabTests.cpp
    Scene/MeshRendererSerializationTests.cpp
    Scene/AssetReferencesTests.cpp
    Scene/CameraComponentTests.cpp
    Scene/SceneTests.cpp
    Scene/ComponentRegistryTests.cpp
    Scene/UnresolvedComponentTests.cpp
    Scene/EntityTemplateTests.cpp
    Scene/HierarchyTests.cpp
    Scene/SceneSystemsTests.cpp
    Math/MathTests.cpp
    Slate/ThemeTests.cpp
    Slate/DrawListTests.cpp
    Slate/IconTests.cpp
    Slate/LumaIconPackTests.cpp
    Slate/WidgetTests.cpp
    Slate/ColorPickerTests.cpp
    Slate/MenuTests.cpp
    Slate/DragDropTests.cpp
    Slate/ModalTests.cpp
    Slate/TextEditStateTests.cpp
    Slate/NumericTextTests.cpp
    Slate/TextFieldTests.cpp
    Slate/SpinBoxTests.cpp
    Asset/AssetIdTests.cpp
    Asset/AssetScannerTests.cpp
    Asset/AssetRegistryTests.cpp
    Asset/MaterialThumbnailTests.cpp
    Asset/AssetPathTests.cpp
    Asset/TextureColorSpaceTests.cpp
    Asset/Import/ImportSceneTests.cpp
    Asset/Import/ModelTranslatorTests.cpp
    Asset/Import/ImageTranslatorTests.cpp
    Asset/Import/AudioTranslatorTests.cpp
    Asset/Import/PipelineTests.cpp
    Asset/Import/FactoryTests.cpp
    Asset/Import/ImportSessionTests.cpp
    Asset/Import/SourceWatchTests.cpp
    Asset/ThumbnailImportTests.cpp
    Audio/AudioDecodeTests.cpp
    Scene/MaterialMigrationTests.cpp
    Material/MaterialModelTests.cpp
    Material/MaterialSerializerTests.cpp
    Material/MaterialLibraryTests.cpp
    Material/MaterialFromTexturesTests.cpp
    Editor/ContentBrowserTests.cpp
    Editor/ThumbnailBudgetTests.cpp
    Editor/ImportDialogTests.cpp
    Editor/ConsoleTests.cpp
    Editor/MaterialSceneBridgeTests.cpp
    Editor/MaterialEditorTests.cpp
    Editor/InspectorMaterialTests.cpp
    Editor/PrefabAssetEditorTests.cpp
    Editor/InspectorRegistryTests.cpp
    Editor/OutlinerRegistryTests.cpp
    Editor/EditorFrameworkTests.cpp
    Editor/ProjectSettingsTests.cpp
    Editor/PluginsWindowTests.cpp
    Editor/StaticMeshSlotTests.cpp
    Editor/MeshPlacementTests.cpp
    Editor/TransformGizmoTests.cpp
    Editor/BackgroundTasksTests.cpp
    Editor/InstanceParametersTests.cpp
    Editor/MaterialGraphModelTests.cpp
    Editor/MaterialGraphEditorTests.cpp
    Editor/AssetWindowsTests.cpp
    GraphEditor/GraphEditorCoreTests.cpp
    GraphEditor/GraphControllerTests.cpp
    GraphEditor/GraphEditorWidgetTests.cpp
    GraphEditor/GraphPaletteTests.cpp
    GraphEditor/DrawListAntiAliasTests.cpp
    VFS/PathTests.cpp
    VFS/VFSTests.cpp
    VFS/FileWatcherTests.cpp
    Rendering/RHIAdapterTests.cpp
    Rendering/MeshGeometryTests.cpp
    Rendering/MeshSimplifyTests.cpp
    Rendering/PerformanceSettingsTests.cpp
    Rendering/MaterialGpuDescTests.cpp
    Rendering/DebugDrawTests.cpp
    Rendering/BakeCameraMesh.cpp
    Rendering/VulkanTextureCacheTests.cpp
    Rendering/VulkanUITextureTests.cpp
    Rendering/VulkanMaterialSystemTests.cpp
    Fixtures/GenerateMaterialShowcase.cpp)

target_link_libraries(LumaTests PRIVATE Luma::Core Luma::Jobs Luma::Tasks Luma::Project Luma::Plugins Luma::EditorPlugins Luma::Serialization Luma::Scene Luma::Slate Luma::Asset Luma::Audio Luma::VFS Luma::Material Luma::MaterialCompiler Luma::ShaderCompiler Luma::AssetImport Luma::PanelContentBrowserForTest Luma::PanelConsole Luma::Gizmo Luma::Mesh Luma::MaterialBridge Luma::PanelMaterialEditor Luma::PanelInspector Luma::PanelWorldOutliner Luma::PanelProjectSettings Luma::PanelBackgroundTasks Luma::PanelImportDialog Luma::PanelViewport Luma::RenderVulkan Luma::MaterialEditors Luma::GraphEditor Luma::AssetWindows Luma::GraphicsSettings Luma::Profiling Catch2::Catch2WithMain)

set_target_properties(LumaTests PROPERTIES FOLDER "Tests")

add_executable(LumaPreferencesTests Editor/PreferencesTests.cpp)
target_compile_definitions(LumaPreferencesTests PRIVATE LUMA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(LumaPreferencesTests PRIVATE Luma::PanelPreferences Catch2::Catch2WithMain)
set_target_properties(LumaPreferencesTests PROPERTIES FOLDER "Tests")
add_test(NAME LumaPreferencesTests COMMAND LumaPreferencesTests)

# One ctest entry that runs the whole Catch2 binary (reports per-assertion on
# failure). Fine-grained catch_discover_tests can be adopted later.
add_test(NAME LumaTests COMMAND LumaTests)

set(LUMA_DLL_FIXTURE_ROOT "${CMAKE_BINARY_DIR}/DllPluginFixtures")
add_library(DllProbeDll SHARED Plugins/Fixtures/DllProbeModule.cpp)
target_include_directories(DllProbeDll PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Fixtures)
target_link_libraries(DllProbeDll PRIVATE Luma::Plugins Luma::Scene)
target_compile_definitions(DllProbeDll PRIVATE LUMA_MODULE_DLL=1)
set_target_properties(DllProbeDll PROPERTIES
    FOLDER "Tests/Fixtures"
    OUTPUT_NAME "LumaEditor-DllProbe"
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    RUNTIME_OUTPUT_DIRECTORY "${LUMA_DLL_FIXTURE_ROOT}/DllProbe/Binaries/Win64/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${LUMA_DLL_FIXTURE_ROOT}/DllProbe/Binaries/Win64/$<CONFIG>"
    ARCHIVE_OUTPUT_DIRECTORY "${LUMA_DLL_FIXTURE_ROOT}/DllProbe/Binaries/Win64/$<CONFIG>")

add_library(DllProbeBadApiDll SHARED Plugins/Fixtures/DllProbeBadApi.cpp)
target_link_libraries(DllProbeBadApiDll PRIVATE Luma::Plugins)
target_compile_definitions(DllProbeBadApiDll PRIVATE LUMA_MODULE_DLL=1)
set_target_properties(DllProbeBadApiDll PROPERTIES
    FOLDER "Tests/Fixtures"
    OUTPUT_NAME "LumaEditor-DllProbeBadApi"
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    RUNTIME_OUTPUT_DIRECTORY "${LUMA_DLL_FIXTURE_ROOT}/DllProbeBadApi/Binaries/Win64/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${LUMA_DLL_FIXTURE_ROOT}/DllProbeBadApi/Binaries/Win64/$<CONFIG>"
    ARCHIVE_OUTPUT_DIRECTORY "${LUMA_DLL_FIXTURE_ROOT}/DllProbeBadApi/Binaries/Win64/$<CONFIG>")
add_library(DllProbeNoExportDll SHARED Plugins/Fixtures/DllProbeNoExport.cpp)
set_target_properties(DllProbeNoExportDll PROPERTIES
    FOLDER "Tests/Fixtures"
    OUTPUT_NAME "LumaEditor-DllProbeNoExport"
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    RUNTIME_OUTPUT_DIRECTORY "${LUMA_DLL_FIXTURE_ROOT}/DllProbeNoExport/Binaries/Win64/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${LUMA_DLL_FIXTURE_ROOT}/DllProbeNoExport/Binaries/Win64/$<CONFIG>"
    ARCHIVE_OUTPUT_DIRECTORY "${LUMA_DLL_FIXTURE_ROOT}/DllProbeNoExport/Binaries/Win64/$<CONFIG>")
add_dependencies(LumaTests DllProbeDll DllProbeBadApiDll DllProbeNoExportDll)

set(LUMA_WIZARD_FIXTURE_ROOT "${CMAKE_BINARY_DIR}/WizardFixtures")
set(_wizard_engine_range ">=${PROJECT_VERSION} <1.0.0")

function(luma_wizard_fixture_dll pluginName moduleName source)
    cmake_parse_arguments(WF "" "" "INCLUDES;LIBS" ${ARGN})
    add_library(${pluginName}Dll SHARED ${source})
    if(WF_INCLUDES)
        target_include_directories(${pluginName}Dll PRIVATE ${WF_INCLUDES})
    endif()
    target_link_libraries(${pluginName}Dll PRIVATE ${WF_LIBS})
    target_compile_definitions(${pluginName}Dll PRIVATE LUMA_MODULE_DLL=1)
    set_target_properties(${pluginName}Dll PROPERTIES
        FOLDER "Tests/Fixtures"
        OUTPUT_NAME "LumaEditor-${moduleName}"
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        RUNTIME_OUTPUT_DIRECTORY "${LUMA_WIZARD_FIXTURE_ROOT}/${pluginName}/Binaries/Win64/$<CONFIG>"
        LIBRARY_OUTPUT_DIRECTORY "${LUMA_WIZARD_FIXTURE_ROOT}/${pluginName}/Binaries/Win64/$<CONFIG>"
        ARCHIVE_OUTPUT_DIRECTORY "${LUMA_WIZARD_FIXTURE_ROOT}/${pluginName}/Binaries/Win64/$<CONFIG>")
    add_dependencies(LumaTests ${pluginName}Dll)
endfunction()

luma_scaffold_plugin(Blank "${LUMA_WIZARD_FIXTURE_ROOT}/WizardBlank" WizardBlank
    FRIENDLY "Wizard Blank" AUTHOR "Luma" DESCRIPTION "Blank fixture" CATEGORY "Tests"
    ENGINE_VERSION "${_wizard_engine_range}")
luma_wizard_fixture_dll(WizardBlank WizardBlank
    "${LUMA_WIZARD_FIXTURE_ROOT}/WizardBlank/Source/WizardBlank/Private/WizardBlankModule.cpp"
    LIBS Luma::Plugins)

luma_scaffold_plugin(ContentOnly "${LUMA_WIZARD_FIXTURE_ROOT}/WizardContent" WizardContent
    FRIENDLY "Wizard Content" AUTHOR "Luma" DESCRIPTION "Content fixture" CATEGORY "Tests"
    ENGINE_VERSION "${_wizard_engine_range}")

luma_scaffold_plugin(EditorToolbarButton "${LUMA_WIZARD_FIXTURE_ROOT}/WizardToolbar" WizardToolbar
    FRIENDLY "Wizard Toolbar" AUTHOR "Luma" DESCRIPTION "Toolbar fixture" CATEGORY "Tests"
    ENGINE_VERSION "${_wizard_engine_range}")
luma_wizard_fixture_dll(WizardToolbar WizardToolbarEditor
    "${LUMA_WIZARD_FIXTURE_ROOT}/WizardToolbar/Source/WizardToolbarEditor/Private/WizardToolbarEditorModule.cpp"
    LIBS Luma::Plugins Luma::EditorFramework Luma::Slate)

luma_scaffold_plugin(EditorStandaloneWindow "${LUMA_WIZARD_FIXTURE_ROOT}/WizardWindow" WizardWindow
    FRIENDLY "Wizard Window" AUTHOR "Luma" DESCRIPTION "Window fixture" CATEGORY "Tests"
    ENGINE_VERSION "${_wizard_engine_range}")
luma_wizard_fixture_dll(WizardWindow WizardWindowEditor
    "${LUMA_WIZARD_FIXTURE_ROOT}/WizardWindow/Source/WizardWindowEditor/Private/WizardWindowEditorModule.cpp"
    LIBS Luma::Plugins Luma::EditorFramework Luma::Slate)

luma_scaffold_plugin(RuntimeComponent "${LUMA_WIZARD_FIXTURE_ROOT}/WizardComp" WizardComp
    FRIENDLY "Wizard Comp" AUTHOR "Luma" DESCRIPTION "Component fixture" CATEGORY "Tests"
    ENGINE_VERSION "${_wizard_engine_range}")
luma_wizard_fixture_dll(WizardComp WizardComp
    "${LUMA_WIZARD_FIXTURE_ROOT}/WizardComp/Source/WizardComp/Private/WizardCompModule.cpp"
    INCLUDES "${LUMA_WIZARD_FIXTURE_ROOT}/WizardComp/Source/WizardComp/Public"
    LIBS Luma::Plugins Luma::Scene)

target_compile_definitions(LumaTests PRIVATE
    LUMA_ENGINE_LINK_DUMP="${CMAKE_BINARY_DIR}/generated/LumaEngineLinkLibraries.txt")
target_compile_definitions(LumaTests PRIVATE LUMA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_definitions(LumaTests PRIVATE LUMA_DLL_FIXTURE_ROOT="${LUMA_DLL_FIXTURE_ROOT}")
target_compile_definitions(LumaTests PRIVATE LUMA_WIZARD_FIXTURE_ROOT="${LUMA_WIZARD_FIXTURE_ROOT}")
target_compile_definitions(LumaTests PRIVATE
    LUMA_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders"
    LUMA_SHADER_SOURCE_DIR="${CMAKE_SOURCE_DIR}/Engine/Rendering/Vulkan/shaders")

# Backend tests drive the Vulkan module's internal classes on a real device.
target_include_directories(LumaTests PRIVATE ${CMAKE_SOURCE_DIR}/Engine/Rendering/Vulkan/src ${CMAKE_SOURCE_DIR}/ThirdParty/stb)
target_link_libraries(LumaTests PRIVATE Vulkan::Vulkan Luma::Renderer)
