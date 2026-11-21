# Luma::Material - Unreal-style material assets: Material (.lmat), Material
# Instance (.lmatinst), resolution of instance chains, and serialization.
# Renderer-agnostic; the editor bridges resolved materials to the renderer.

add_library(LumaMaterial STATIC
    src/MaterialTypes.cpp
    src/MaterialJson.cpp
    src/MaterialSerializer.cpp
    src/MaterialInstanceSerializer.cpp
    src/MaterialLibrary.cpp
    src/MaterialFromTextures.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Asset/src/Import/Factories/FactoryCommon.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Asset/src/Import/Factories/MeshFactory.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Asset/src/Import/Factories/MaterialFactory.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Asset/src/Import/Factories/TextureFactory.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Asset/src/Import/Factories/SoundFactory.cpp)

target_include_directories(LumaMaterial PRIVATE ${CMAKE_SOURCE_DIR}/ThirdParty/stb)

target_link_libraries(LumaMaterial PRIVATE Luma::Audio)

add_library(Luma::Material ALIAS LumaMaterial)

target_include_directories(LumaMaterial
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(LumaMaterial
    PUBLIC Luma::Core Luma::Math Luma::Asset Luma::Serialization)

set_target_properties(LumaMaterial PROPERTIES FOLDER "Engine")
