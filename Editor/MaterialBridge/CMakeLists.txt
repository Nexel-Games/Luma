# Luma::MaterialBridge - editor glue between scene material slots / mesh
# assets and the renderer's SceneRenderer residency API.

add_library(LumaMaterialBridge STATIC
    src/MaterialGpuCompile.cpp
    src/MaterialSceneBridge.cpp)

add_library(Luma::MaterialBridge ALIAS LumaMaterialBridge)

target_include_directories(LumaMaterialBridge
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_SOURCE_DIR}/ThirdParty/stb)

target_link_libraries(LumaMaterialBridge
    PUBLIC Luma::Core Luma::Math Luma::RHI Luma::Scene Luma::Asset Luma::Material Luma::ShaderCompiler
    PRIVATE Luma::Mesh Luma::MaterialCompiler)

# Generated material shaders #include the engine's pass templates.
target_compile_definitions(LumaMaterialBridge PRIVATE
    LUMA_SHADER_SOURCE_DIR="${CMAKE_SOURCE_DIR}/Engine/Rendering/Vulkan/shaders")

set_target_properties(LumaMaterialBridge PROPERTIES FOLDER "Editor")
