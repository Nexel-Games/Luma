# Luma::MaterialBridge - editor glue between scene material slots / mesh
# assets and the renderer's SceneRenderer residency API.

add_library(LumaMaterialBridge STATIC
    src/MaterialSceneBridge.cpp)

add_library(Luma::MaterialBridge ALIAS LumaMaterialBridge)

target_include_directories(LumaMaterialBridge
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_SOURCE_DIR}/ThirdParty/stb)

target_link_libraries(LumaMaterialBridge
    PUBLIC Luma::Core Luma::Math Luma::RHI Luma::Scene Luma::Asset Luma::Material
    PRIVATE Luma::Mesh)

set_target_properties(LumaMaterialBridge PROPERTIES FOLDER "Editor")
