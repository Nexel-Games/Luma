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
    Serialization/SerialValueTests.cpp
    Serialization/JsonTests.cpp
    Serialization/ReflectionTests.cpp
    Scene/SceneSerializerTests.cpp
    Scene/MeshRendererSerializationTests.cpp
    Scene/AssetReferencesTests.cpp
    Scene/CameraComponentTests.cpp
    Scene/SceneTests.cpp
    Scene/HierarchyTests.cpp
    Math/MathTests.cpp
    Slate/ThemeTests.cpp
    Slate/DrawListTests.cpp
    Slate/IconTests.cpp
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
    Editor/StaticMeshSlotTests.cpp
    Editor/MeshPlacementTests.cpp
    Editor/TransformGizmoTests.cpp
    Editor/BackgroundTasksTests.cpp
    Editor/InstanceParametersTests.cpp
    GraphEditor/GraphEditorCoreTests.cpp
    GraphEditor/GraphControllerTests.cpp
    VFS/PathTests.cpp
    VFS/VFSTests.cpp
    VFS/FileWatcherTests.cpp
    Rendering/RHIAdapterTests.cpp
    Rendering/MeshGeometryTests.cpp
    Rendering/MaterialGpuDescTests.cpp
    Rendering/VulkanTextureCacheTests.cpp
    Rendering/VulkanUITextureTests.cpp
    Rendering/VulkanMaterialSystemTests.cpp
    Fixtures/GenerateMaterialShowcase.cpp)

target_link_libraries(LumaTests PRIVATE Luma::Core Luma::Jobs Luma::Tasks Luma::Project Luma::Serialization Luma::Scene Luma::Slate Luma::Asset Luma::Audio Luma::VFS Luma::Material Luma::MaterialCompiler Luma::ShaderCompiler Luma::AssetImport Luma::PanelContentBrowserForTest Luma::PanelConsole Luma::Gizmo Luma::Mesh Luma::MaterialBridge Luma::PanelMaterialEditor Luma::PanelInspector Luma::PanelBackgroundTasks Luma::PanelImportDialog Luma::PanelViewport Luma::RenderVulkan Luma::MaterialEditors Luma::GraphEditor Catch2::Catch2WithMain)

set_target_properties(LumaTests PROPERTIES FOLDER "Tests")

add_executable(LumaPreferencesTests Editor/PreferencesTests.cpp)
target_compile_definitions(LumaPreferencesTests PRIVATE LUMA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(LumaPreferencesTests PRIVATE Luma::PanelPreferences Catch2::Catch2WithMain)
set_target_properties(LumaPreferencesTests PROPERTIES FOLDER "Tests")
add_test(NAME LumaPreferencesTests COMMAND LumaPreferencesTests)

# One ctest entry that runs the whole Catch2 binary (reports per-assertion on
# failure). Fine-grained catch_discover_tests can be adopted later.
add_test(NAME LumaTests COMMAND LumaTests)

target_compile_definitions(LumaTests PRIVATE LUMA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_definitions(LumaTests PRIVATE
    LUMA_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders"
    LUMA_SHADER_SOURCE_DIR="${CMAKE_SOURCE_DIR}/Engine/Rendering/Vulkan/shaders")

# Backend tests drive the Vulkan module's internal classes on a real device.
target_include_directories(LumaTests PRIVATE ${CMAKE_SOURCE_DIR}/Engine/Rendering/Vulkan/src ${CMAKE_SOURCE_DIR}/ThirdParty/stb)
target_link_libraries(LumaTests PRIVATE Vulkan::Vulkan Luma::Renderer)
