# Luma::Mesh - built-in primitive mesh generation (renderer-agnostic geometry).

add_library(LumaMesh ${LUMA_LIBRARY_TYPE}
    src/Mesh.cpp
    src/Tangents.cpp
    src/MeshConversion.cpp)

add_library(Luma::Mesh ALIAS LumaMesh)

target_include_directories(LumaMesh
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(LumaMesh PUBLIC Luma::Core Luma::Math Luma::RHI Luma::Asset)

set_target_properties(LumaMesh PROPERTIES FOLDER "Engine/Rendering")
