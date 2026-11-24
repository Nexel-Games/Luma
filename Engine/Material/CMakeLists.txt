# Luma::Material - Unreal-style material assets: Material (.lmat), Material
# Instance (.lmatinst), resolution of instance chains, and serialization.
# Renderer-agnostic; the editor bridges resolved materials to the renderer.

add_subdirectory(Graph)

add_library(LumaMaterial ${LUMA_LIBRARY_TYPE}
    src/MaterialTypes.cpp
    src/MaterialJson.cpp
    src/MaterialSerializer.cpp
    src/MaterialInstanceSerializer.cpp
    src/MaterialLibrary.cpp
    src/MaterialFromTextures.cpp
    src/GraphAssets.cpp
    src/MaterialMigrationV4.cpp)

add_library(Luma::Material ALIAS LumaMaterial)

target_include_directories(LumaMaterial
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(LumaMaterial
    PUBLIC Luma::Core Luma::Math Luma::Asset Luma::Serialization Luma::MaterialGraph)

set_target_properties(LumaMaterial PROPERTIES FOLDER "Engine")

add_subdirectory(Compiler)
