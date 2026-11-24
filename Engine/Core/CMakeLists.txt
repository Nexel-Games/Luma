# Luma::Core - foundational module. Depends only on the C++ standard library.

# Sources grouped by subsystem under src/<Subsystem>/.
add_library(LumaCore ${LUMA_LIBRARY_TYPE}
    src/Foundation/Version.cpp
    src/Foundation/Assert.cpp
    src/Log/Log.cpp
    src/Log/LogBuffer.cpp
    src/Config/Config.cpp
    src/Event/Event.cpp
    src/Layer/LayerStack.cpp
    src/Application/Application.cpp
    src/Memory/Alloc.cpp
    src/Memory/Arena.cpp
    src/Memory/Pool.cpp
    src/Memory/Track.cpp)

add_library(Luma::Core ALIAS LumaCore)

# Luma/Core/VersionConfig.h is generated from project(VERSION ...) so the engine
# version has a single source of truth.
set(LUMA_GENERATED_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/include)
configure_file(cmake/VersionConfig.h.in
    ${LUMA_GENERATED_INCLUDE_DIR}/Luma/Core/VersionConfig.h @ONLY)

target_include_directories(LumaCore
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
           ${LUMA_GENERATED_INCLUDE_DIR})

target_link_libraries(LumaCore PUBLIC Luma::Flags)

set_target_properties(LumaCore PROPERTIES FOLDER "Engine")
