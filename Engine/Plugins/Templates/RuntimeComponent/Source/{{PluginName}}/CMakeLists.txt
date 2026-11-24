luma_add_module({{PluginName}}
    PLUGIN {{PluginName}}
    TYPE Runtime
    SOURCES Private/{{PluginName}}Module.cpp
    PUBLIC_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/Public
    DEPENDS Luma::Plugins Luma::Scene Luma::Serialization)
