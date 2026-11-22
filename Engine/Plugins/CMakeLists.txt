# Luma::Plugins — manifest discovery and (later phases) plugin lifecycle.
# Phase 1 links Project because the module owns project enablement in P3.
# This phase only parses manifests and discovers them. It does not start plugins.

add_library(LumaPlugins STATIC
    src/PluginManifest.cpp
    src/PluginDiscovery.cpp)

add_library(Luma::Plugins ALIAS LumaPlugins)

target_include_directories(LumaPlugins
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(LumaPlugins
    PUBLIC Luma::Core Luma::Serialization Luma::Project)

set_target_properties(LumaPlugins PROPERTIES FOLDER "Engine")
