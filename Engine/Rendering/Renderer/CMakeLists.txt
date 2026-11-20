# Luma::Renderer - renderer-agnostic scene-view types for the deferred
# renderer (header-only): DeferredSceneView + cascaded shadow fitting.

add_library(LumaRenderer INTERFACE)
add_library(Luma::Renderer ALIAS LumaRenderer)

target_include_directories(LumaRenderer
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(LumaRenderer INTERFACE Luma::Core Luma::RHI Luma::Math)
