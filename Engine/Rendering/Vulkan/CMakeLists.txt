# Luma::RenderVulkan - Vulkan 1.3 backend implementing Luma::RHI.
# All Vulkan types are confined to this module (Vulkan linked PRIVATE).

add_library(LumaRenderVulkan ${LUMA_LIBRARY_TYPE}
    src/VulkanRHI.cpp
    src/Vulkan/VulkanCommon.cpp
    src/Vulkan/VulkanInstance.cpp
    src/Vulkan/VulkanDevice.cpp
    src/Vulkan/VulkanSwapchain.cpp
    src/Vulkan/VulkanShader.cpp
    src/Vulkan/VulkanMemory.cpp
    src/Vulkan/VulkanTexture.cpp
    src/Vulkan/Textures/VulkanTextureCache.cpp
    src/Vulkan/Meshes/VulkanMeshCache.cpp
    src/Vulkan/Materials/VulkanMaterialLayout.cpp
    src/Vulkan/Materials/VulkanMaterialPipelines.cpp
    src/Vulkan/Materials/VulkanMaterialSystem.cpp
    src/Vulkan/Selection/VulkanSelectionOutlinePass.cpp
    src/Vulkan/UI/VulkanUIPass.cpp
    src/Vulkan/Grid/VulkanGridPass.cpp
    src/Vulkan/Overlay/VulkanOverlayLinePass.cpp
    src/Vulkan/SkyLight/VulkanSkyLight.cpp
    src/Vulkan/EyeAdaptation/VulkanEyeAdaptation.cpp
    src/Vulkan/VulkanRenderer.cpp
    src/Vulkan/Profiling/VulkanGpuTimer.cpp
    src/Vulkan/RHI/VulkanRHIResources.cpp
    src/Vulkan/RHI/VulkanRHICommandList.cpp
    src/Vulkan/RHI/VulkanRHIPipeline.cpp
    src/Vulkan/RHI/VulkanRHIShader.cpp
    src/Vulkan/RHI/VulkanRHIDevice.cpp
    src/VulkanDeferredRenderer.cpp)

add_library(Luma::RenderVulkan ALIAS LumaRenderVulkan)

# --- Shader compilation: GLSL -> SPIR-V via glslc -------------------------
set(_luma_shader_src ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(_luma_shader_out ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${_luma_shader_out})

if(NOT Vulkan_GLSLC_EXECUTABLE)
    message(FATAL_ERROR "Luma: glslc not found (needed to compile shaders)")
endif()

set(_luma_spv_outputs)
foreach(shader ui.vert ui.frag
               grid.vert grid.frag
               deferred_lighting.vert deferred_lighting.frag
               fxaa.frag
               bloom_downsample.frag bloom_upsample.frag bloom_composite.frag
               skylight_capture.comp skylight_downsample.comp
               skylight_filter.comp skylight_sh.comp
               eye_histogram.comp eye_adapt.comp
               overlay_line.vert overlay_line.frag
               selection_outline.vert selection_outline.frag)
    add_custom_command(
        OUTPUT ${_luma_shader_out}/${shader}.spv
        COMMAND ${Vulkan_GLSLC_EXECUTABLE} --target-env=vulkan1.3
                -I ${_luma_shader_src}
                ${_luma_shader_src}/${shader}
                -o ${_luma_shader_out}/${shader}.spv
        DEPENDS ${_luma_shader_src}/${shader}
                ${_luma_shader_src}/sky_atmosphere.glsl
                ${_luma_shader_src}/skylight_common.glsl
                ${_luma_shader_src}/lighting_common.glsl
        COMMENT "glslc ${shader}"
        VERBATIM)
    list(APPEND _luma_spv_outputs ${_luma_shader_out}/${shader}.spv)
endforeach()

add_custom_target(LumaShaders DEPENDS ${_luma_spv_outputs})
add_dependencies(LumaRenderVulkan LumaShaders)

target_include_directories(LumaRenderVulkan
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/ThirdParty/stb
            ${PROJECT_SOURCE_DIR}/Engine/Rendering/Renderer/include)

target_link_libraries(LumaRenderVulkan
    PUBLIC Luma::Core Luma::RHI Luma::Platform Luma::Profiling
    PRIVATE Vulkan::Vulkan Luma::Math Luma::Mesh Luma::Asset)

# Let the engine locate the SDK's validation layer even when it isn't registered
# with the loader (common on machines without VULKAN_SDK set globally).
if(DEFINED ENV{VULKAN_SDK})
    set(_luma_vk_layer_dir "$ENV{VULKAN_SDK}/Bin")
elseif(Vulkan_ROOT)
    set(_luma_vk_layer_dir "${Vulkan_ROOT}/Bin")
endif()
if(_luma_vk_layer_dir)
    # VULKAN_SDK is usually a Windows path (D:\VulkanSDK\...): normalise to
    # forward slashes so it is a valid C++ string literal.
    file(TO_CMAKE_PATH "${_luma_vk_layer_dir}" _luma_vk_layer_dir)
    target_compile_definitions(LumaRenderVulkan PRIVATE
        LUMA_VULKAN_LAYER_PATH="${_luma_vk_layer_dir}")
endif()

# Engine content (sky HDR, default textures) in this checkout.
file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/Content" _luma_content_dir)
target_compile_definitions(LumaRenderVulkan PRIVATE
    LUMA_SHADER_DIR="${_luma_shader_out}"
    LUMA_CONTENT_DIR="${_luma_content_dir}")

set_target_properties(LumaRenderVulkan PROPERTIES FOLDER "Engine")
