luma_add_module({{PluginName}}
    PLUGIN {{PluginName}}
    TYPE Runtime
    SOURCES Private/{{PluginName}}Module.cpp
    DEPENDS Luma::Plugins)
