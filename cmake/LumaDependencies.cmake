# LumaDependencies.cmake
# Declares third-party dependencies via FetchContent (version-pinned).
# Dependencies are only materialized when a module asks for them, via the
# luma_require_* helpers, so configuring Core alone stays fast and offline.

include(FetchContent)

# GLFW is fetched from git so we control the exact tag; disable its extras.
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4          # pinned release
    GIT_SHALLOW    TRUE)

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1       # pinned release
    GIT_SHALLOW    TRUE)

# Fix MSVC compilation flags for exception handling (Assimp sources throw).
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
endif()

# Idempotent helpers so multiple modules can request the same dependency.
function(luma_require_glfw)
    if(NOT TARGET glfw)
        FetchContent_MakeAvailable(glfw)
    endif()
endfunction()

function(luma_require_catch2)
    if(NOT TARGET Catch2::Catch2WithMain)
        FetchContent_MakeAvailable(Catch2)
        list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
        set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} PARENT_SCOPE)
    endif()
endfunction()

function(luma_require_assimp)
    if(NOT TARGET assimp)
        set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
        set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
        set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
        set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
        set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
        set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
        set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
        foreach(fmt OBJ FBX GLTF)
            set(ASSIMP_BUILD_${fmt}_IMPORTER ON CACHE BOOL "" FORCE)
        endforeach()
        foreach(fmt FBX GLTF OBJ)   # exporters: fixture generation only
            set(ASSIMP_BUILD_${fmt}_EXPORTER ON CACHE BOOL "" FORCE)
        endforeach()
        add_subdirectory(${PROJECT_SOURCE_DIR}/ThirdParty/assimp
                         ${CMAKE_BINARY_DIR}/ThirdParty/assimp EXCLUDE_FROM_ALL)
        set_target_properties(assimp PROPERTIES FOLDER "ThirdParty")
    endif()
endfunction()

function(luma_require_miniaudio)
    if(NOT TARGET luma_miniaudio)
        enable_language(C)
        add_library(luma_miniaudio STATIC
            ${PROJECT_SOURCE_DIR}/ThirdParty/miniaudio/miniaudio.c)
        target_include_directories(luma_miniaudio PUBLIC
            ${PROJECT_SOURCE_DIR}/ThirdParty/miniaudio)
        target_compile_definitions(luma_miniaudio PUBLIC
            MA_NO_GENERATION
            MA_NO_ENCODING)
        if(MSVC)
            target_compile_options(luma_miniaudio PRIVATE /W0)
        else()
            target_compile_options(luma_miniaudio PRIVATE -w)
        endif()
        set_target_properties(luma_miniaudio PROPERTIES FOLDER "ThirdParty")
    endif()
endfunction()

function(luma_require_tinyexr)
    if(NOT TARGET luma_tinyexr)
        add_library(luma_tinyexr STATIC
            ${PROJECT_SOURCE_DIR}/ThirdParty/tinyexr_impl.cpp)
        target_include_directories(luma_tinyexr PUBLIC
            ${PROJECT_SOURCE_DIR}/ThirdParty/stb
            ${PROJECT_SOURCE_DIR}/ThirdParty)
        if(MSVC)
            target_compile_options(luma_tinyexr PRIVATE /W0)
        endif()
        set_target_properties(luma_tinyexr PROPERTIES FOLDER "ThirdParty")
    endif()
endfunction()
