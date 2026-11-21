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
    Tasks/TaskManagerTests.cpp
    Project/ProjectTests.cpp
    Serialization/SerialValueTests.cpp
    Serialization/JsonTests.cpp
    Serialization/ReflectionTests.cpp
    Scene/SceneSerializerTests.cpp
    Scene/CameraComponentTests.cpp
    Scene/SceneTests.cpp
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
    Scene/MaterialMigrationTests.cpp
    Material/MaterialModelTests.cpp
    Material/MaterialSerializerTests.cpp
    Material/MaterialLibraryTests.cpp
    Material/MaterialFromTexturesTests.cpp
    Editor/ContentBrowserTests.cpp
    Editor/ConsoleTests.cpp
    Editor/MaterialSceneBridgeTests.cpp
    Editor/MaterialEditorTests.cpp
    Editor/InspectorMaterialTests.cpp
    Editor/TransformGizmoTests.cpp
    Editor/BackgroundTasksTests.cpp
    VFS/PathTests.cpp
    VFS/VFSTests.cpp
    VFS/FileWatcherTests.cpp
    Rendering/RHIAdapterTests.cpp
    Rendering/MeshGeometryTests.cpp
    Rendering/MaterialGpuDescTests.cpp
    Rendering/VulkanTextureCacheTests.cpp
    Rendering/VulkanMaterialSystemTests.cpp
    Fixtures/GenerateMaterialShowcase.cpp)

target_link_libraries(LumaTests PRIVATE Luma::Core Luma::Jobs Luma::Tasks Luma::Project Luma::Serialization Luma::Scene Luma::Slate Luma::Asset Luma::VFS Luma::Material Luma::PanelContentBrowserForTest Luma::PanelConsole Luma::Gizmo Luma::Mesh Luma::MaterialBridge Luma::PanelMaterialEditor Luma::PanelInspector Luma::PanelBackgroundTasks Luma::RenderVulkan Catch2::Catch2WithMain)

set_target_properties(LumaTests PROPERTIES FOLDER "Tests")

# One ctest entry that runs the whole Catch2 binary (reports per-assertion on
# failure). Fine-grained catch_discover_tests can be adopted later.
add_test(NAME LumaTests COMMAND LumaTests)

target_compile_definitions(LumaTests PRIVATE LUMA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# Backend tests drive the Vulkan module's internal classes on a real device.
target_include_directories(LumaTests PRIVATE ${CMAKE_SOURCE_DIR}/Engine/Rendering/Vulkan/src ${CMAKE_SOURCE_DIR}/ThirdParty/stb)
target_link_libraries(LumaTests PRIVATE Vulkan::Vulkan)
