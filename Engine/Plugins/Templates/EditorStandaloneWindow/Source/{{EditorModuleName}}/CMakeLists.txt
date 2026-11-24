luma_add_module({{EditorModuleName}}
    PLUGIN {{PluginName}}
    TYPE Editor
    SOURCES Private/{{EditorModuleName}}Module.cpp
    DEPENDS Luma::Plugins Luma::EditorFramework Luma::Slate)
