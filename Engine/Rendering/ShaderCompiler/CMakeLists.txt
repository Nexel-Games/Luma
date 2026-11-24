# Luma::ShaderCompiler - runtime GLSL -> SPIR-V through shaderc (from the
# Vulkan SDK): #include resolution, async worker jobs, an on-disk SPIR-V cache
# keyed by everything that affects the output, and SPIR-V stats. Used for
# generated material shaders; knows nothing about materials or Vulkan objects.

add_library(LumaShaderCompiler ${LUMA_LIBRARY_TYPE}
    src/ShaderCompiler.cpp
    src/ShaderCache.cpp
    src/SpirvStats.cpp)

add_library(Luma::ShaderCompiler ALIAS LumaShaderCompiler)

get_filename_component(LUMA_VULKAN_LIB_DIR "${Vulkan_LIBRARY}" DIRECTORY)
find_library(LUMA_SHADERC_LIB shaderc_combined HINTS "${LUMA_VULKAN_LIB_DIR}" NO_DEFAULT_PATH REQUIRED)
find_library(LUMA_SHADERC_LIB_DEBUG shaderc_combinedd HINTS "${LUMA_VULKAN_LIB_DIR}" NO_DEFAULT_PATH)
if(NOT LUMA_SHADERC_LIB_DEBUG)
    set(LUMA_SHADERC_LIB_DEBUG "${LUMA_SHADERC_LIB}")
endif()

target_include_directories(LumaShaderCompiler
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${Vulkan_INCLUDE_DIR})
target_link_libraries(LumaShaderCompiler
    PUBLIC Luma::Core
    PRIVATE $<IF:$<CONFIG:Debug>,${LUMA_SHADERC_LIB_DEBUG},${LUMA_SHADERC_LIB}>)

set_target_properties(LumaShaderCompiler PROPERTIES FOLDER "Engine/Rendering")
