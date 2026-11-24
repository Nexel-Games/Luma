# Luma::Plugins — manifest discovery, dependency resolution and lifecycle.

add_library(LumaPlugins ${LUMA_LIBRARY_TYPE}
    src/PluginManifest.cpp
    src/PluginDescriptor.cpp
    src/PluginDiscovery.cpp
    src/Plugin.cpp
    src/PluginManager.cpp
    src/Module.cpp
    src/ModuleManager.cpp
    src/DllModule.cpp
    src/PluginWizard.cpp
    src/PluginPackaging.cpp)

add_library(Luma::Plugins ALIAS LumaPlugins)

target_include_directories(LumaPlugins
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(LumaPlugins
    PUBLIC Luma::Core Luma::Serialization Luma::Project Luma::Scene Luma::Asset Luma::AssetImport)

set_target_properties(LumaPlugins PROPERTIES FOLDER "Engine")
